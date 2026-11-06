//
// i_dgsound.c -- XenevaOS (DG) sound backend for Doom.
//
// Software mixer: Doom SFX lumps (8-bit unsigned mono, ~11kHz) are mixed
// per output chunk into 48kHz stereo 16-bit PCM and streamed to
// /dev/sound (4096-byte chunks; a full device buffer returns -1 and the
// chunk is dropped, retried on the next pump).
//
// A dedicated mixer thread owns open/register/write/close (the kernel
// DSP is keyed by thread id). The game thread only touches channel
// state; publication uses release/acquire ordering.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <_xeneva.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/_ketime.h>
#include <sys/iocodes.h>

#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "m_misc.h"

#define DG_MIX_RATE 48000
#define DG_MIX_CHUNK 4096
#define DG_MIX_FRAMES (DG_MIX_CHUNK / 4)
#define DG_MAXCHAN 64
#define DG_SRC_RATE_DEFAULT 11025

// From doomgeneric_xe.cpp (doomgeneric.h can't be included here: its
// DG_Init declaration collides with this file's sound-module DG_Init).
extern uint32_t DG_GetTicksMs(void);

// Parsed lump, attached to sfxinfo_t::driver_data by CacheSounds
// (lazily on first StartSound as well).
typedef struct {
    const uint8_t* data;
    uint32_t len;   // samples
    uint32_t rate;  // Hz
} dgsfx_t;

typedef struct {
    int active;             // published with release, read with acquire
    const uint8_t* data;
    uint32_t len;
    uint32_t pos;           // 16.16 sample position
    uint32_t step;          // 16.16 per output frame
    int vol;                // 0-127
    int sep;                // 0-255, 128 center
} dgchan_t;

static dgchan_t dgchan[DG_MAXCHAN];
static int dg_fd = -1;
static volatile int dg_mixerRun = 0;
static int16_t dg_mixbuf[DG_MIX_CHUNK / 2];

int use_libsamplerate = 0;
float libsamplerate_scale = 1.0f;

int DG_GetSfxLumpNum(sfxinfo_t* sfx);

static uint16_t dg_read_le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Resolve (and cache on first use) the parsed lump for an sfx.
static dgsfx_t* DG_ResolveSfx(sfxinfo_t* sfx) {
    dgsfx_t* parsed;
    int lump;
    const uint8_t* lumpdata;
    int lumplen;
    uint32_t rate;
    uint32_t len;

    if (!sfx)
        return NULL;
    parsed = (dgsfx_t*)sfx->driver_data;
    if (parsed)
        return parsed;

    lump = DG_GetSfxLumpNum(sfx);
    if (lump < 0)
        return NULL;

    lumpdata = (const uint8_t*)W_CacheLumpNum(lump, PU_STATIC);
    lumplen = W_LumpLength(lump);
    if (!lumpdata || lumplen <= 8)
        return NULL;

    // DMX header: [0..1] format tag, [2..3] sample rate (LE),
    // samples follow at +8.
    rate = dg_read_le16(lumpdata + 2);
    if (rate == 0 || rate > 64000)
        rate = DG_SRC_RATE_DEFAULT;
    len = (uint32_t)lumplen - 8;

    parsed = (dgsfx_t*)malloc(sizeof(dgsfx_t));
    if (!parsed)
        return NULL;
    parsed->data = lumpdata + 8;
    parsed->len = len;
    parsed->rate = rate;
    sfx->driver_data = parsed;
    return parsed;
}

// Mix one device chunk (1024 stereo frames) from active channels.
static void DG_MixChunk(void) {
    static int32_t mixL[DG_MIX_FRAMES];
    static int32_t mixR[DG_MIX_FRAMES];
    int i, c;

    for (i = 0; i < DG_MIX_FRAMES; i++) {
        mixL[i] = 0;
        mixR[i] = 0;
    }

    for (c = 0; c < DG_MAXCHAN; c++) {
        int active;
        const uint8_t* data;
        uint32_t len, pos, step;
        int gl, gr;
        int vol, sep;

        __atomic_load(&dgchan[c].active, &active, __ATOMIC_ACQUIRE);
        if (!active)
            continue;

        data = dgchan[c].data;
        len = dgchan[c].len;
        pos = dgchan[c].pos;
        step = dgchan[c].step;
        vol = dgchan[c].vol;
        sep = dgchan[c].sep;
        if (!data || len == 0 || vol <= 0) {
            if (!data || len == 0) {
                int z = 0;
                __atomic_store(&dgchan[c].active, &z, __ATOMIC_RELEASE);
            }
            continue;
        }

        gl = vol;
        gr = vol;
        if (sep < 128)
            gr = (gr * sep) / 128;
        else if (sep > 128)
            gl = (gl * (256 - sep)) / 128;

        for (i = 0; i < DG_MIX_FRAMES; i++) {
            uint32_t idx = pos >> 16;
            int s;
            if (idx >= len) {
                int z = 0;
                __atomic_store(&dgchan[c].active, &z, __ATOMIC_RELEASE);
                break;
            }
            s = ((int)data[idx] - 128) * 256;
            pos += step;
            mixL[i] += (s * gl) / 127;
            mixR[i] += (s * gr) / 127;
        }
        dgchan[c].pos = pos;
    }

    for (i = 0; i < DG_MIX_FRAMES; i++) {
        int32_t l = mixL[i];
        int32_t r = mixR[i];
        if (l > 32767)
            l = 32767;
        else if (l < -32768)
            l = -32768;
        if (r > 32767)
            r = 32767;
        else if (r < -32768)
            r = -32768;
        dg_mixbuf[i * 2] = (int16_t)l;
        dg_mixbuf[i * 2 + 1] = (int16_t)r;
    }
}

// Mixer thread: owns the device fd end to end.
static void DG_MixerThread(void) {
    XEFileIOControl ioctl;
    int fd;

    fd = _KeOpenFile("/dev/sound", FILE_OPEN_WRITE);
    if (fd < 0) {
        _KePrint("[dgsnd] mixer: open /dev/sound failed\n");
        return;
    }

    memset(&ioctl, 0, sizeof(ioctl));
    ioctl.uint_1 = 0;
    ioctl.syscall_magic = AURORA_SYSCALL_MAGIC;
    _KeFileIoControl(fd, SOUND_REGISTER_SNDPLR, &ioctl);
    _KePrint("[dgsnd] mixer: registered, pumping\n");
    dg_fd = fd;

    while (dg_mixerRun) {
        size_t wr;
        // Pace to the audio clock: one 1024-frame chunk at 48kHz is
        // ~21.3ms of audio. Measure the mix+write cost and sleep only the
        // remainder of a 20ms budget (slightly ahead of realtime; the 8KB
        // device buffer absorbs jitter). A fixed sleep overshoots because
        // it ignores the XFER time, drifting behind realtime -> gaps.
        // Without any sleep the mixer hogs the (single) vCPU and starves
        // the game thread.
        uint32_t t0 = DG_GetTicksMs();
        DG_MixChunk();
        wr = _KeWriteFile(fd, dg_mixbuf, DG_MIX_CHUNK);
        if ((long)wr < 0) {
            // Device full: back off briefly, retry (drop this chunk).
            _KeProcessSleep(5);
        } else {
            static int wroteOnce = 0;
            if (!wroteOnce) {
                wroteOnce = 1;
                _KePrint("[dgsnd] mixer: first write ok\n");
            }
            uint32_t dt = DG_GetTicksMs() - t0;
            if (dt < 20)
                _KeProcessSleep(20 - dt);
        }
    }

    _KeCloseFile(fd);
    dg_fd = -1;
}

int DG_GetSfxLumpNum(sfxinfo_t* sfx) {
    char namebuf[16];

    if (!sfx)
        return -1;
    if (sfx->link)
        return DG_GetSfxLumpNum(sfx->link);
    M_snprintf(namebuf, sizeof(namebuf), "ds%s", sfx->name);
    return W_CheckNumForName(namebuf);
}

static boolean DG_Init(boolean use_sfx_prefix) {
    (void)use_sfx_prefix;
    memset(dgchan, 0, sizeof(dgchan));
    dg_mixerRun = 1;
    _KePrint("[dgsnd] Init: spawning mixer thread\n");
    _KeCreateThread(DG_MixerThread, "doomsnd");
    return true;
}

static void DG_Shutdown(void) {
    dg_mixerRun = 0;
}

static int DG_StartSound(sfxinfo_t* sfxinfo, int channel, int vol, int sep) {
    dgsfx_t* sfx;
    int v;

    if (channel < 0 || channel >= DG_MAXCHAN)
        return -1;
    sfx = DG_ResolveSfx(sfxinfo);
    if (!sfx)
        return -1;

    if (vol < 0)
        vol = 0;
    if (vol > 127)
        vol = 127;
    if (sep < 0)
        sep = 0;
    if (sep > 255)
        sep = 255;

    v = 0;
    __atomic_store(&dgchan[channel].active, &v, __ATOMIC_RELEASE);
    dgchan[channel].data = sfx->data;
    dgchan[channel].len = sfx->len;
    dgchan[channel].pos = 0;
    dgchan[channel].step = (uint32_t)(((uint64_t)sfx->rate << 16) / DG_MIX_RATE);
    if (dgchan[channel].step == 0)
        dgchan[channel].step = 1;
    dgchan[channel].vol = vol;
    dgchan[channel].sep = sep;
    v = 1;
    __atomic_store(&dgchan[channel].active, &v, __ATOMIC_RELEASE);
    return channel;
}

static void DG_StopSound(int channel) {
    int v = 0;
    if (channel < 0 || channel >= DG_MAXCHAN)
        return;
    __atomic_store(&dgchan[channel].active, &v, __ATOMIC_RELEASE);
}

static boolean DG_SoundIsPlaying(int channel) {
    int active = 0;
    if (channel < 0 || channel >= DG_MAXCHAN)
        return false;
    __atomic_load(&dgchan[channel].active, &active, __ATOMIC_ACQUIRE);
    return active ? true : false;
}

static void DG_UpdateSoundParams(int channel, int vol, int sep) {
    if (channel < 0 || channel >= DG_MAXCHAN)
        return;
    if (vol < 0)
        vol = 0;
    if (vol > 127)
        vol = 127;
    if (sep < 0)
        sep = 0;
    if (sep > 255)
        sep = 255;
    dgchan[channel].vol = vol;
    dgchan[channel].sep = sep;
}

static void DG_Update(void) {
    // Mixing is driven by the dedicated mixer thread; nothing to do here.
}

static void DG_CacheSounds(sfxinfo_t* sounds, int num_sounds) {
    int i;
    int ok = 0;
    if (!sounds || num_sounds <= 0)
        return;
    for (i = 0; i < num_sounds; i++) {
        if (DG_ResolveSfx(&sounds[i]))
            ok++;
    }
    _KePrint("[dgsnd] cached %d/%d sfx\n", ok, num_sounds);
}

static snddevice_t dg_sound_devices[] = {
    SNDDEVICE_SB,
};

sound_module_t DG_sound_module = {
    dg_sound_devices,
    sizeof(dg_sound_devices) / sizeof(dg_sound_devices[0]),
    DG_Init,
    DG_Shutdown,
    DG_GetSfxLumpNum,
    DG_Update,
    DG_UpdateSoundParams,
    DG_StartSound,
    DG_StopSound,
    DG_SoundIsPlaying,
    DG_CacheSounds,
};

// Silent music stub: MUS/MIDI synthesis is a separate project.
// Present so enabling FEATURE_SOUND links and music calls stay safe.
static boolean DG_MusicInit(void) {
    return false;
}

static void DG_MusicShutdown(void) {
}

static void DG_SetMusicVolume(int volume) {
    (void)volume;
}

static void DG_PauseMusic(void) {
}

static void DG_ResumeMusic(void) {
}

static void* DG_RegisterSong(void* data, int len) {
    (void)data;
    (void)len;
    return NULL;
}

static void DG_UnRegisterSong(void* handle) {
    (void)handle;
}

static void DG_PlaySong(void* handle, boolean looping) {
    (void)handle;
    (void)looping;
}

static void DG_StopSong(void) {
}

static boolean DG_MusicIsPlaying(void) {
    return false;
}

static snddevice_t dg_music_devices[] = {
    SNDDEVICE_SB,
};

music_module_t DG_music_module = {
    dg_music_devices,
    sizeof(dg_music_devices) / sizeof(dg_music_devices[0]),
    DG_MusicInit,
    DG_MusicShutdown,
    DG_SetMusicVolume,
    DG_PauseMusic,
    DG_ResumeMusic,
    DG_RegisterSong,
    DG_UnRegisterSong,
    DG_PlaySong,
    DG_StopSong,
    DG_MusicIsPlaying,
    NULL,
};
