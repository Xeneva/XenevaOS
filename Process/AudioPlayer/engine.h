/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

/**
 * engine.h -- Accent audio engine (private header, included once by main.cpp)
 *
 * Runs on its own thread (AcAudioThread). It owns the sound device, the open
 * file and every decode buffer; the UI talks to it only through AcShared,
 * a small block of lock-free fields:
 *
 *   UI -> engine : cmdSeq/cmd/path (handshake: the UI writes a new path only
 *                  after ackSeq caught up), paused, seekPermille, quit
 *   engine -> UI : state, position, duration, format, end-of-track sequence,
 *                  and a ring of the most recent output samples (visualizer)
 *
 * Why the engine never waits for the UI:
 *  - nothing here takes a lock; the visualizer ring is overwritten blindly
 *    and the UI tolerates a torn read (it is only drawn, never played)
 *  - every decode buffer is static: no malloc on the audio path
 *
 * Why the UI never starves the engine:
 *  - virtio-snd's write spins in the kernel with IRQs masked until the host
 *    has consumed the block, so writes pace playback by themselves. The
 *    engine yields for AC_YIELD_MS right after each write: that window is
 *    where the UI thread (and the compositor) get the CPU. The UI keeps each
 *    frame short, and the 1 ms scheduler tick preempts it if it overruns.
 *
 * File I/O contract (FAT driver): a read always consumes whole clusters and a
 * seek lands on a cluster, so this reader only ever reads 4096-byte blocks
 * from 4096-aligned offsets and does any byte skipping itself.
 */

#ifndef __ACCENT_ENGINE_H__
#define __ACCENT_ENGINE_H__

#include <stdint.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <sys/iocodes.h>
#include "accent.h"
#include "minimp3.h"

/* ------------------------------------------------------------------------ */
/*  Tunables                                                                */
/* ------------------------------------------------------------------------ */

#define AC_OUT_HZ	  48000
#define AC_OUT_FRAMES 1024 /* frames per device write: 4096 bytes, 21.3 ms */
#define AC_OUT_BYTES  (AC_OUT_FRAMES * 4)
#define AC_YIELD_MS	  3	   /* CPU handed to the UI after each blocking write */
#define AC_FILE_BLOCK 4096 /* FAT reads must be whole, aligned clusters */
#define AC_READER_CAP (20 * 1024)
#define AC_MP3_WINDOW (16 * 1024)			/* minimp3 needs a few frames to sync */
#define AC_RS_CHUNK	  256					/* resampler input frames per pass */
#define AC_RS_OUT_MAX (AC_RS_CHUNK * 6 + 8) /* worst case 8 kHz -> 48 kHz */
#define AC_VIS_RING	  2048					/* power of two */
#define AC_PATH_MAX	  256

enum AcEngineState { AC_STATE_IDLE = 0, AC_STATE_PLAYING, AC_STATE_ENDED, AC_STATE_ERROR };
enum AcEngineCmd { AC_CMD_NONE = 0, AC_CMD_PLAY, AC_CMD_STOP };
enum AcStep { AC_STEP_OK = 0, AC_STEP_END, AC_STEP_ABORT };
enum AcCodec { AC_CODEC_NONE = 0, AC_CODEC_MP3, AC_CODEC_WAV };

/* ------------------------------------------------------------------------ */
/*  Shared block                                                            */
/* ------------------------------------------------------------------------ */

typedef struct _ac_shared_ {
	/* UI -> engine */
	uint32_t cmdSeq;
	int32_t cmd;
	char path[AC_PATH_MAX];
	int32_t seekPermille; /* -1: none */
	uint32_t paused;
	uint32_t quit;

	/* engine -> UI */
	uint32_t ackSeq;   /* last cmdSeq consumed */
	uint32_t trackSeq; /* cmdSeq of the loaded track */
	uint32_t state;
	uint32_t endSeq;	  /* bumps when a track finishes or fails */
	uint32_t endTrackSeq; /* trackSeq of that track */
	uint32_t endReason;	  /* AC_STATE_ENDED / AC_STATE_ERROR */
	uint32_t posMs;
	uint32_t durMs;
	uint32_t srcHz;
	uint32_t kbps;
	uint32_t codec;
	uint32_t done;	   /* engine unregistered and parked */
	uint32_t visWrite; /* total samples written to vis[] */
	int16_t vis[AC_VIS_RING];
} AcShared;

static AcShared g_sh;

static inline uint32_t AcLoad(const uint32_t* p) {
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
static inline void AcStore(uint32_t* p, uint32_t v) {
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}
static inline int32_t AcLoadI(const int32_t* p) {
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
static inline void AcStoreI(int32_t* p, int32_t v) {
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}

/* ------------------------------------------------------------------------ */
/*  Little helpers                                                          */
/* ------------------------------------------------------------------------ */

static inline uint16_t AcLE16(const uint8_t* p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t AcLE32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint32_t AcSynchsafe(const uint8_t* p) {
	return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
		   ((uint32_t)(p[2] & 0x7F) << 7) | (uint32_t)(p[3] & 0x7F);
}

static int AcLowerC(int c) {
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

/* case-insensitive "name ends with .ext" */
static bool AcHasExt(const char* name, const char* ext) {
	size_t n = strlen(name), e = strlen(ext);
	if (n <= e || name[n - e - 1] != '.')
		return false;
	for (size_t i = 0; i < e; i++) {
		if (AcLowerC((unsigned char)name[n - e + i]) != ext[i])
			return false;
	}
	return true;
}

static bool AcIsAudioName(const char* name) {
	return AcHasExt(name, "mp3") || AcHasExt(name, "wav");
}

/* ------------------------------------------------------------------------ */
/*  Block reader                                                            */
/* ------------------------------------------------------------------------ */

typedef struct _ac_reader_ {
	int fd;
	uint32_t fileSize;
	uint32_t nextBlock; /* file offset of the next 4096-byte block to read */
	uint32_t bufBase;	/* file offset of buf[0] */
	uint8_t* buf;
	int len; /* valid bytes in buf */
	int pos; /* consumed bytes */
	bool eof;
} AcReader;

static uint8_t g_readBuf[AC_READER_CAP];

static bool AcReaderOpen(AcReader* r, const char* path) {
	memset(r, 0, sizeof(*r));
	r->buf = g_readBuf;
	r->fd = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	if (r->fd < 0)
		return false;
	XEFileStatus st;
	memset(&st, 0, sizeof(st));
	_KeFileStat(r->fd, &st);
	r->fileSize = (uint32_t)st.size;
	if (r->fileSize == 0) {
		_KeCloseFile(r->fd);
		r->fd = -1;
		return false;
	}
	return true;
}

static void AcReaderClose(AcReader* r) {
	if (r->fd >= 0)
		_KeCloseFile(r->fd);
	r->fd = -1;
	r->len = r->pos = 0;
	r->eof = true;
}

static inline int AcReaderAvail(const AcReader* r) {
	return r->len - r->pos;
}

/* make at least `want` unread bytes available, unless the file ends first */
static void AcReaderFill(AcReader* r, int want) {
	if (want > AC_READER_CAP - AC_FILE_BLOCK)
		want = AC_READER_CAP - AC_FILE_BLOCK;
	if (AcReaderAvail(r) >= want)
		return;
	if (r->pos > 0) {
		memmove(r->buf, r->buf + r->pos, (size_t)(r->len - r->pos));
		r->bufBase += (uint32_t)r->pos;
		r->len -= r->pos;
		r->pos = 0;
	}
	while (!r->eof && AcReaderAvail(r) < want && r->len + AC_FILE_BLOCK <= AC_READER_CAP) {
		if (r->nextBlock >= r->fileSize) {
			r->eof = true;
			break;
		}
		size_t n = _KeReadFile(r->fd, r->buf + r->len, AC_FILE_BLOCK);
		if (n == 0 || n == (size_t)-1) {
			r->eof = true;
			break;
		}
		/* the last cluster comes back whole: trust the file size, not n */
		uint32_t valid = r->fileSize - r->nextBlock;
		if (valid > AC_FILE_BLOCK)
			valid = AC_FILE_BLOCK;
		if (n < valid)
			valid = (uint32_t)n;
		r->len += (int)valid;
		r->nextBlock += AC_FILE_BLOCK;
		if (r->nextBlock >= r->fileSize)
			r->eof = true;
	}
}

/* position the reader at an arbitrary byte offset */
static bool AcReaderSeek(AcReader* r, uint32_t off) {
	if (off > r->fileSize)
		off = r->fileSize;
	/* already buffered (e.g. walking WAV chunk headers): no I/O */
	if (off >= r->bufBase && off < r->bufBase + (uint32_t)r->len) {
		r->pos = (int)(off - r->bufBase);
		return true;
	}
	uint32_t aligned = off & ~(uint32_t)(AC_FILE_BLOCK - 1);
	if (_KeFileSetOffset(r->fd, aligned) != 0)
		return false;
	r->nextBlock = aligned;
	r->bufBase = aligned;
	r->len = r->pos = 0;
	r->eof = (aligned >= r->fileSize);
	AcReaderFill(r, AC_FILE_BLOCK);
	int skip = (int)(off - aligned);
	r->pos = skip < r->len ? skip : r->len;
	return true;
}

/* ------------------------------------------------------------------------ */
/*  Engine state (owned by the audio thread only)                           */
/* ------------------------------------------------------------------------ */

typedef struct _ac_track_ {
	int codec;
	AcReader rd;
	uint32_t dataStart;
	uint32_t dataSize;

	/* WAV */
	int wavFormat; /* 1 = PCM, 3 = IEEE float */
	int wavChannels;
	int wavBits;
	uint32_t wavRate;
	int wavBlockAlign;
	uint32_t wavConsumed;

	/* MP3 duration estimate (exact for CBR, converges for VBR) */
	uint64_t mp3Bytes;
	uint64_t mp3Samples;
	uint32_t mp3Hz;

	/* output clock */
	AccentResampler rs;
	uint32_t rsHz;
	uint32_t baseMs;
	uint64_t framesOut;
	uint32_t durMs;
} AcTrack;

static AcTrack g_track;
static int g_snd = -1;
static mp3dec_t g_mp3;
static mp3d_sample_t g_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static int16_t g_stereo[1152 * 2];
static int16_t g_rsOut[AC_RS_OUT_MAX * 2];
static int16_t g_out[AC_OUT_FRAMES * 2];
static int g_outFill;

static void AcEngineRegister(void) {
	g_snd = _KeOpenFile((char*)"/dev/sound", FILE_OPEN_WRITE);
	XEFileIOControl ioctl;
	memset(&ioctl, 0, sizeof(ioctl));
	ioctl.uint_1 = 0; /* no per-frame sleep in the kernel */
	ioctl.syscall_magic = AURORA_SYSCALL_MAGIC;
	/* the kernel keys the DSP by thread id: this must run on this thread */
	_KeFileIoControl(g_snd, SOUND_REGISTER_SNDPLR, &ioctl);
}

static void AcEngineUnregister(void) {
	if (g_snd < 0)
		return;
	XEFileIOControl ioctl;
	memset(&ioctl, 0, sizeof(ioctl));
	ioctl.syscall_magic = AURORA_SYSCALL_MAGIC;
	_KeFileIoControl(g_snd, SOUND_UNREGISTER_SNDPLR, &ioctl);
	_KeCloseFile(g_snd);
	g_snd = -1;
}

static bool AcInterrupted(void) {
	return AcLoad(&g_sh.cmdSeq) != AcLoad(&g_sh.ackSeq) || AcLoadI(&g_sh.seekPermille) >= 0 ||
		   AcLoad(&g_sh.quit);
}

static void AcPublishPosition(AcTrack* t) {
	uint64_t ms = t->baseMs + (t->framesOut * 1000) / AC_OUT_HZ;
	if (t->durMs && ms > t->durMs)
		ms = t->durMs;
	AcStore(&g_sh.posMs, (uint32_t)ms);
}

/* copy what was just accepted by the device into the visualizer ring */
static void AcPublishVis(const int16_t* stereo, int frames) {
	uint32_t w = g_sh.visWrite; /* only this thread writes it */
	for (int i = 0; i < frames; i++) {
		int m = ((int)stereo[i * 2] + (int)stereo[i * 2 + 1]) >> 1;
		g_sh.vis[(w + (uint32_t)i) & (AC_VIS_RING - 1)] = (int16_t)m;
	}
	AcStore(&g_sh.visWrite, w + (uint32_t)frames);
}

/*
 * AcDeviceWrite -- hand one full block to the device. Returns false when the
 * UI asked for something else (new track, seek, quit): the caller drops the
 * rest of its decoded chunk.
 */
static bool AcDeviceWrite(AcTrack* t) {
	size_t r = _KeWriteFile(g_snd, g_out, AC_OUT_BYTES);
	if (r == 0 || r == (size_t)-1) {
		/* no pacing device behind us: keep real time ourselves */
		_KeProcessSleep((AC_OUT_FRAMES * 1000) / AC_OUT_HZ);
	}
	AcPublishVis(g_out, AC_OUT_FRAMES);
	t->framesOut += AC_OUT_FRAMES;
	AcPublishPosition(t);
	g_outFill = 0;

	_KeProcessSleep(AC_YIELD_MS);

	while (AcLoad(&g_sh.paused) && !AcInterrupted())
		_KeProcessSleep(10);
	return !AcInterrupted();
}

static bool AcOutPush(AcTrack* t, const int16_t* stereo, int frames) {
	while (frames > 0) {
		int k = AC_OUT_FRAMES - g_outFill;
		if (k > frames)
			k = frames;
		memmove(g_out + g_outFill * 2, stereo, (size_t)k * 4);
		g_outFill += k;
		stereo += k * 2;
		frames -= k;
		if (g_outFill == AC_OUT_FRAMES && !AcDeviceWrite(t))
			return false;
	}
	return true;
}

/* source-rate stereo -> 48 kHz stereo -> device */
static bool AcEmit(AcTrack* t, const int16_t* stereo, int frames, uint32_t hz) {
	if (hz == AC_OUT_HZ || hz == 0)
		return AcOutPush(t, stereo, frames);
	if (hz != t->rsHz) {
		AccentResamplerInitialize(&t->rs, hz, AC_OUT_HZ);
		t->rsHz = hz;
	}
	while (frames > 0) {
		int n = frames < AC_RS_CHUNK ? frames : AC_RS_CHUNK;
		size_t out = AccentResamplerProcess(&t->rs, stereo, (size_t)n, g_rsOut);
		if (out > AC_RS_OUT_MAX)
			out = AC_RS_OUT_MAX;
		if (!AcOutPush(t, g_rsOut, (int)out))
			return false;
		stereo += n * 2;
		frames -= n;
	}
	return true;
}

/* ------------------------------------------------------------------------ */
/*  WAV                                                                     */
/* ------------------------------------------------------------------------ */

static bool AcWavOpen(AcTrack* t) {
	AcReader* r = &t->rd;
	AcReaderFill(r, 12);
	if (AcReaderAvail(r) < 12 || memcmp(r->buf, "RIFF", 4) != 0 ||
		memcmp(r->buf + 8, "WAVE", 4) != 0)
		return false;

	bool haveFmt = false;
	uint32_t p = 12;
	for (int guard = 0; guard < 64 && p + 8 <= r->fileSize; guard++) {
		if (!AcReaderSeek(r, p))
			return false;
		AcReaderFill(r, 8 + 40);
		if (AcReaderAvail(r) < 8)
			return false;
		const uint8_t* h = r->buf + r->pos;
		uint32_t size = AcLE32(h + 4);
		if (memcmp(h, "fmt ", 4) == 0 && size >= 16 && AcReaderAvail(r) >= 24) {
			int fmt = AcLE16(h + 8);
			if (fmt == 0xFFFE && size >= 40 && AcReaderAvail(r) >= 8 + 26)
				fmt = AcLE16(h + 8 + 24); /* WAVE_FORMAT_EXTENSIBLE sub-format */
			t->wavFormat = fmt;
			t->wavChannels = AcLE16(h + 10);
			t->wavRate = AcLE32(h + 12);
			t->wavBlockAlign = AcLE16(h + 20);
			t->wavBits = AcLE16(h + 22);
			haveFmt = true;
		} else if (memcmp(h, "data", 4) == 0) {
			if (!haveFmt)
				return false;
			t->dataStart = p + 8;
			t->dataSize = size;
			if (t->dataStart + t->dataSize > r->fileSize || size == 0xFFFFFFFF)
				t->dataSize = r->fileSize - t->dataStart;
			break;
		}
		p += 8 + size + (size & 1);
	}
	if (!haveFmt || !t->dataStart)
		return false;

	bool pcm = (t->wavFormat == 1 &&
				(t->wavBits == 8 || t->wavBits == 16 || t->wavBits == 24 || t->wavBits == 32));
	bool flt = (t->wavFormat == 3 && t->wavBits == 32);
	if ((!pcm && !flt) || t->wavChannels < 1 || t->wavRate < 4000 || t->wavRate > 192000)
		return false;
	if (t->wavBlockAlign != t->wavChannels * (t->wavBits / 8))
		t->wavBlockAlign = t->wavChannels * (t->wavBits / 8);

	t->durMs =
		(uint32_t)(((uint64_t)(t->dataSize / (uint32_t)t->wavBlockAlign) * 1000) / t->wavRate);
	t->wavConsumed = 0;
	return AcReaderSeek(r, t->dataStart);
}

static inline int16_t AcWavSample(const AcTrack* t, const uint8_t* s) {
	switch (t->wavBits) {
	case 8:
		return (int16_t)(((int)s[0] - 128) * 256); /* not <<: negative shift is UB */
	case 16:
		return (int16_t)AcLE16(s);
	case 24:
		return (int16_t)AcLE16(s + 1);
	default:
		if (t->wavFormat == 3) {
			float f;
			memmove(&f, s, 4);
			if (f > 1.0f)
				f = 1.0f;
			if (f < -1.0f)
				f = -1.0f;
			return (int16_t)(f * 32767.0f);
		}
		return (int16_t)AcLE16(s + 2);
	}
}

static int AcWavStep(AcTrack* t) {
	AcReader* r = &t->rd;
	uint32_t left = t->dataSize - t->wavConsumed;
	if (left < (uint32_t)t->wavBlockAlign)
		return AC_STEP_END;
	int ba = t->wavBlockAlign;
	int maxFrames = 1024;
	uint32_t want = (uint32_t)(maxFrames * ba);
	if (want > left)
		want = left;
	AcReaderFill(r, (int)want);
	uint32_t avail = (uint32_t)AcReaderAvail(r);
	if (avail > left)
		avail = left;
	int frames = (int)(avail / (uint32_t)ba);
	if (frames > maxFrames)
		frames = maxFrames;
	if (frames == 0)
		return AC_STEP_END;

	const uint8_t* src = r->buf + r->pos;
	int bps = t->wavBits / 8;
	bool stereo = t->wavChannels > 1;
	for (int i = 0; i < frames; i++) {
		const uint8_t* f = src + i * ba;
		int16_t l = AcWavSample(t, f);
		g_stereo[i * 2] = l;
		g_stereo[i * 2 + 1] = stereo ? AcWavSample(t, f + bps) : l;
	}
	r->pos += frames * ba;
	t->wavConsumed += (uint32_t)(frames * ba);
	return AcEmit(t, g_stereo, frames, t->wavRate) ? AC_STEP_OK : AC_STEP_ABORT;
}

/* ------------------------------------------------------------------------ */
/*  MP3                                                                     */
/* ------------------------------------------------------------------------ */

static bool AcMp3Open(AcTrack* t) {
	AcReader* r = &t->rd;
	AcReaderFill(r, 10);
	t->dataStart = 0;
	if (AcReaderAvail(r) >= 10 && memcmp(r->buf, "ID3", 3) == 0) {
		t->dataStart = 10 + AcSynchsafe(r->buf + 6);
		if (r->buf[5] & 0x10)
			t->dataStart += 10; /* footer */
		if (t->dataStart >= r->fileSize)
			return false;
		if (!AcReaderSeek(r, t->dataStart))
			return false;
	}
	t->dataSize = r->fileSize - t->dataStart;
	t->mp3Bytes = t->mp3Samples = 0;
	t->mp3Hz = 0;
	t->durMs = 0;
	mp3dec_init(&g_mp3);
	return true;
}

static int AcMp3Step(AcTrack* t) {
	AcReader* r = &t->rd;
	AcReaderFill(r, AC_MP3_WINDOW);
	int avail = AcReaderAvail(r);
	if (avail <= 0)
		return AC_STEP_END;

	mp3dec_frame_info_t info;
	memset(&info, 0, sizeof(info));
	int samples = mp3dec_decode_frame(&g_mp3, r->buf + r->pos, avail, g_pcm, &info);
	if (info.frame_bytes == 0) {
		if (r->eof)
			return AC_STEP_END;
		r->pos = r->len; /* no frame anywhere in a full window: skip it */
		return AC_STEP_OK;
	}
	r->pos += info.frame_bytes;
	if (samples <= 0)
		return AC_STEP_OK; /* ID3/garbage frame: skipped */

	if ((uint32_t)info.hz != t->mp3Hz) {
		t->mp3Hz = (uint32_t)info.hz;
		AcStore(&g_sh.srcHz, t->mp3Hz);
	}
	if (info.bitrate_kbps > 0)
		AcStore(&g_sh.kbps, (uint32_t)info.bitrate_kbps);

	/* duration = data bytes * (decoded time / decoded bytes) */
	t->mp3Bytes += (uint64_t)info.frame_bytes;
	t->mp3Samples += (uint64_t)samples;
	if (t->mp3Bytes >= 16 * 1024 || r->eof) {
		uint64_t decodedMs = (t->mp3Samples * 1000) / (t->mp3Hz ? t->mp3Hz : AC_OUT_HZ);
		uint32_t dur = (uint32_t)(((uint64_t)t->dataSize * decodedMs) / t->mp3Bytes);
		if (dur != t->durMs) {
			t->durMs = dur;
			AcStore(&g_sh.durMs, dur);
		}
	}

	const int16_t* stereo = g_pcm;
	if (info.channels == 1) {
		for (int i = 0; i < samples; i++)
			g_stereo[i * 2] = g_stereo[i * 2 + 1] = g_pcm[i];
		stereo = g_stereo;
	}
	return AcEmit(t, stereo, samples, (uint32_t)info.hz) ? AC_STEP_OK : AC_STEP_ABORT;
}

/* ------------------------------------------------------------------------ */
/*  Track lifetime                                                          */
/* ------------------------------------------------------------------------ */

static void AcResetOutput(AcTrack* t) {
	g_outFill = 0;
	t->framesOut = 0;
	t->rsHz = 0; /* resampler restarts on the next chunk */
}

static bool AcTrackOpen(AcTrack* t, const char* path) {
	memset(t, 0, sizeof(*t));
	t->rd.fd = -1;
	if (!AcReaderOpen(&t->rd, path))
		return false;

	bool ok;
	if (AcHasExt(path, "wav")) {
		t->codec = AC_CODEC_WAV;
		ok = AcWavOpen(t);
	} else {
		t->codec = AC_CODEC_MP3;
		ok = AcMp3Open(t);
	}
	if (!ok) {
		AcReaderClose(&t->rd);
		return false;
	}
	AcResetOutput(t);
	t->baseMs = 0;
	AcStore(&g_sh.codec, (uint32_t)t->codec);
	AcStore(&g_sh.durMs, t->durMs);
	AcStore(&g_sh.posMs, 0);
	AcStore(&g_sh.srcHz, t->codec == AC_CODEC_WAV ? t->wavRate : 0);
	AcStore(&g_sh.kbps,
			t->codec == AC_CODEC_WAV
				? (uint32_t)(((uint64_t)t->wavRate * (uint32_t)t->wavBlockAlign * 8) / 1000)
				: 0);
	return true;
}

static void AcTrackSeek(AcTrack* t, int permille) {
	if (permille < 0)
		permille = 0;
	if (permille > 1000)
		permille = 1000;
	if (t->codec == AC_CODEC_WAV) {
		uint32_t frames = t->dataSize / (uint32_t)t->wavBlockAlign;
		uint32_t target = (uint32_t)(((uint64_t)frames * (uint32_t)permille) / 1000);
		t->wavConsumed = target * (uint32_t)t->wavBlockAlign;
		AcReaderSeek(&t->rd, t->dataStart + t->wavConsumed);
		t->baseMs = (uint32_t)(((uint64_t)target * 1000) / t->wavRate);
	} else {
		if (!t->durMs)
			return; /* not enough decoded yet to know where to go */
		uint32_t off =
			t->dataStart + (uint32_t)(((uint64_t)t->dataSize * (uint32_t)permille) / 1000);
		AcReaderSeek(&t->rd, off);
		mp3dec_init(&g_mp3); /* resyncs on the next frame header */
		t->baseMs = (uint32_t)(((uint64_t)t->durMs * (uint32_t)permille) / 1000);
	}
	AcResetOutput(t);
	AcPublishPosition(t);
}

/* last partial block: pad with silence so the tail is heard */
static void AcFlushTail(AcTrack* t) {
	if (g_outFill == 0)
		return;
	memset(g_out + g_outFill * 2, 0, (size_t)(AC_OUT_FRAMES - g_outFill) * 4);
	g_outFill = AC_OUT_FRAMES;
	AcDeviceWrite(t);
}

static void AcFinishTrack(AcTrack* t, uint32_t reason) {
	AcReaderClose(&t->rd);
	t->codec = AC_CODEC_NONE;
	AcStore(&g_sh.state, reason);
	AcStore(&g_sh.endReason, reason);
	AcStore(&g_sh.endTrackSeq, AcLoad(&g_sh.trackSeq));
	AcStore(&g_sh.endSeq, AcLoad(&g_sh.endSeq) + 1);
}

/*
 * AcAudioThread -- engine entry point (created with _KeCreateThread)
 */
static void AcAudioThread() {
	AcEngineRegister();
	AcTrack* t = &g_track;
	bool open = false;

	for (;;) {
		if (AcLoad(&g_sh.quit))
			break;

		uint32_t seq = AcLoad(&g_sh.cmdSeq);
		if (seq != AcLoad(&g_sh.ackSeq)) {
			char path[AC_PATH_MAX];
			int cmd = AcLoadI(&g_sh.cmd);
			memmove(path, g_sh.path, sizeof(path));
			path[AC_PATH_MAX - 1] = '\0';
			AcStore(&g_sh.ackSeq, seq); /* the UI may now reuse g_sh.path */
			AcStoreI(&g_sh.seekPermille, -1);

			if (open) {
				AcReaderClose(&t->rd);
				open = false;
			}
			if (cmd == AC_CMD_PLAY) {
				AcStore(&g_sh.trackSeq, seq);
				if (AcTrackOpen(t, path)) {
					open = true;
					AcStore(&g_sh.state, AC_STATE_PLAYING);
				} else {
					AcFinishTrack(t, AC_STATE_ERROR);
				}
			} else {
				AcStore(&g_sh.state, AC_STATE_IDLE);
			}
			continue;
		}

		if (!open) {
			_KeProcessSleep(15);
			continue;
		}

		int seek = __atomic_exchange_n(&g_sh.seekPermille, -1, __ATOMIC_ACQ_REL);
		if (seek >= 0)
			AcTrackSeek(t, seek);

		if (AcLoad(&g_sh.paused)) {
			_KeProcessSleep(10);
			continue;
		}

		int step = (t->codec == AC_CODEC_WAV) ? AcWavStep(t) : AcMp3Step(t);
		if (step == AC_STEP_END) {
			AcFlushTail(t);
			AcFinishTrack(t, AC_STATE_ENDED);
			open = false;
		}
	}

	if (open)
		AcReaderClose(&t->rd);
	AcEngineUnregister();
	AcStore(&g_sh.done, 1);
	for (;;)
		_KeProcessSleep(1000); /* parked until the process exits */
}

#endif