/*
 * Process/sndplay/main.cpp -- plays a test tone through the real /dev/sound
 * path (DSP client -> sound core -> card write()), exactly like an application.
 * Used to prove that audio reaches the hardware through the user-space sound
 * server: every write goes to the kernel proxy card, into the shared ring, and
 * out through the server's virtqueue.
 *
 * Output goes to stdout and /dev/kmsg:  grep '\[sndplay\]' run.log
 */
#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_ketime.h>
#include <sys/_keproc.h>
#include <sys/iocodes.h>

#define PERIOD   4096
#ifdef __XENEVA_SVC_TEST__
#define WRITES   900                    /* ~19 s: long enough to span a crash, a fallback and the return */
#else
#define WRITES   100                    /* ~2.1 s of 48 kHz stereo S16 */
#endif

static int g_kmsg = -1;

static void emit(const char* line) {
	printf("%s", line);
	if (g_kmsg < 0)
		g_kmsg = _KeOpenFile((char*)"/dev/kmsg", FILE_OPEN_WRITE);
	if (g_kmsg >= 0)
		_KeWriteFile(g_kmsg, (void*)line, strlen(line));
}

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;
	char line[160];
	/* give init and the sound server time to finish their own setup */
	_KeProcessSleep(6000);

	int snd = _KeOpenFile((char*)"/dev/sound", FILE_OPEN_WRITE);
	if (snd < 0) {
		emit("[sndplay] FAIL open /dev/sound\n");
		return 1;
	}
	int cards = _KeFileIoControl(snd, SOUND_GET_CARD_TOTALNUM, 0);
	sprintf(line, "[sndplay] sound cards: %d\n", cards);
	emit(line);

	XEFileIOControl ioctl;
	memset(&ioctl, 0, sizeof ioctl);
	ioctl.syscall_magic = AURORA_SYSCALL_MAGIC;
	ioctl.uint_1 = 0;
	ioctl.uint_2 = 0;                    /* card 0 */
	_KeFileIoControl(snd, SOUND_REGISTER_SNDPLR, &ioctl);

	static int16_t buf[PERIOD / 2];
	int phase = 0, bad = 0, retries = 0;
	unsigned sum = 0;
	uint64_t t0 = _KeGetCurrentMS();
	for (int w = 0; w < WRITES; w++) {
		/* 440 Hz triangle, both channels: period 109 samples at 48 kHz */
		for (int i = 0; i < PERIOD / 4; i++) {
			int p = phase % 109;
			int v = (p < 55 ? p : 109 - p) * 300 - 8000;
			buf[i * 2] = (int16_t)v;
			buf[i * 2 + 1] = (int16_t)v;
			phase++;
		}
		for (int i = 0; i < PERIOD; i++)
			sum += ((uint8_t*)buf)[i];
		/* a write fails while the sound card is gone (driver restarting); a real
		 * player waits and retries, so do the same for up to ~3 s per period */
		size_t r;
		int tries = 0;
		for (;;) {
			r = _KeWriteFile(snd, buf, PERIOD);
			if (r != 0 && r != (size_t)-1)
				break;
			if (++tries > 600) {
				bad++;
				break;
			}
			retries++;
			_KeProcessSleep(5);
		}
	}
	uint64_t ms = _KeGetCurrentMS() - t0;
	sprintf(line, "[sndplay] wrote %d periods (%d bytes) sum=%x in %d ms, %d failed writes, %d retries\n",
			WRITES, WRITES * PERIOD, sum, (int)ms, bad, retries);
	emit(line);
	emit(bad ? "[sndplay] FAIL\n" : "[sndplay] DONE\n");
	_KeFileIoControl(snd, SOUND_UNREGISTER_SNDPLR, &ioctl);
	_KeCloseFile(snd);
	return bad;
}
