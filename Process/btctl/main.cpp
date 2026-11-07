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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <sys/iocodes.h>
#include <_xeneva.h>
#include <Bt/bt.h>

typedef struct _sound_card_list {
	char name[32];
	int cardID;
	struct _sound_card_list* next;
} aurora_snd_card_list;

/* Same path as init's _play_startup_sound: register on /dev/sound and
 * write PCM frames directly. play.exe goes through the Deodhai daemon,
 * which is a separate process and is not required here. */
static void play_demo_pcm(int card_id) {
	XEFileIOControl ioctl;
	aurora_snd_card_list* list;
	int sound;
	int num_card_count;
	int song;
	int i;
	void* songbuf;
	const char* path;
	XEFileStatus fs;

	sound = _KeOpenFile((char*)"/dev/sound", FILE_OPEN_READ_ONLY);
	if (sound < 0) {
		printf("no sound card\n");
		return;
	}
	memset(&ioctl, 0, sizeof ioctl);
	ioctl.uint_1 = 0;
	ioctl.syscall_magic = AURORA_SYSCALL_MAGIC;
	ioctl.uint_2 = (uint32_t)-1;
	num_card_count = _KeFileIoControl(sound, SOUND_GET_CARD_TOTALNUM, &ioctl);
	if (num_card_count <= 0) {
		printf("no sound card\n");
		_KeCloseFile(sound);
		return;
	}
	ioctl.uint_1 = (uint32_t)num_card_count;
	list = (aurora_snd_card_list*)malloc(sizeof(aurora_snd_card_list) * (size_t)num_card_count);
	if (!list) {
		_KeCloseFile(sound);
		return;
	}
	memset(list, 0, sizeof(aurora_snd_card_list) * (size_t)num_card_count);
	ioctl.ulong_1 = (uint64_t)list;
	if (_KeFileIoControl(sound, SOUND_GET_CARD_LIST, &ioctl)) {
		printf("no sound card\n");
		free(list);
		_KeCloseFile(sound);
		return;
	}
	ioctl.uint_2 = list[0].cardID;
	if (card_id >= 0) {
		for (i = 0; i < num_card_count; i++) {
			if (list[i].cardID == card_id) {
				ioctl.uint_2 = (uint32_t)card_id;
				break;
			}
		}
	}
	_KeFileIoControl(sound, SOUND_REGISTER_SNDPLR, &ioctl);

	path = "/demo.wav";
	song = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	if (song < 0) {
		path = "/Ss.wav";
		song = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	}
	if (song < 0) {
		printf("no demo file\n");
		free(list);
		_KeCloseFile(sound);
		return;
	}
	printf("playing %s\n", path);
	songbuf = malloc(4096);
	if (!songbuf) {
		_KeCloseFile(song);
		free(list);
		_KeCloseFile(sound);
		return;
	}
	memset(songbuf, 0, 4096);
	/* First chunk may hold the RIFF header: skip to the data samples
	 * the way play.exe does, so the card never hears the header. */
	{
		size_t n = _KeReadFile(song, songbuf, 4096);
		uint8_t* b = (uint8_t*)songbuf;
		if (n >= 12 && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F') {
			int off = 12;
			while (off + 8 <= (int)n) {
				int csz = b[off + 4] | (b[off + 5] << 8) | (b[off + 6] << 16) |
						  (b[off + 7] << 24);
				if (b[off] == 'd' && b[off + 1] == 'a' && b[off + 2] == 't' &&
					b[off + 3] == 'a') {
					off += 8;
					break;
				}
				off += 8 + csz;
				if (csz & 1)
					off++;
			}
			if (off > 0 && off < (int)n) {
				size_t keep = n - (size_t)off;
				memmove(b, b + off, keep);
				memset(b + keep, 0, (size_t)off);
			}
		}
		_KeWriteFile(sound, songbuf, 4096);
	}
	for (;;) {
		size_t n;
		int tries;
		memset(&fs, 0, sizeof fs);
		_KeFileStat(song, &fs);
		if (fs.eof)
			break;
		n = _KeReadFile(song, songbuf, 4096);
		if (!n)
			break;
		/* 4096 bytes of stereo 16-bit is ~21 ms at 48 kHz. bredr0's
		 * write blocks until the SBC packet is queued, and an extra
		 * sleep on top of that fed the headset at about two thirds
		 * of real time (256 frames/s, 375 required). Retry only when
		 * the DSP reports a full buffer. */
		for (tries = 0; tries < 50; tries++) {
			n = _KeWriteFile(sound, songbuf, n > 4096 ? 4096 : n);
			if (n <= 4096)
				break;
			_KeProcessSleep(20);
			n = 4096;
		}
	}
	free(songbuf);
	free(list);
	_KeCloseFile(song);
	_KeCloseFile(sound);
}

static void usage(void);
static int parse_addr(const char* s, uint8_t* addr, uint8_t* type);
static int run(int fd, int code, BtInfo* info);

static void usage(void) {
	printf("usage: btctl info|scan|connect <addr|index>|disconnect|name|pair [addr|index]|passkey <6 digits>|confirm|audio|a2dp|forget <addr|index>\n");
}

static int ends_le_only(const char* n) {
	size_t l = strlen(n);
	char a, b;
	if (l < 3 || n[l - 3] != '-')
		return 0;
	a = n[l - 2];
	b = n[l - 1];
	if (a >= 'a' && a <= 'z')
		a = (char)(a - 32);
	if (b >= 'a' && b <= 'z')
		b = (char)(b - 32);
	return a == 'L' && (b == 'E' || b == 'I');
}

/* Only named adverts are shown. Unnamed (flags-only) reports still
 * exist in info->scan but are hidden from the list, so display numbers
 * are mapped to kernel indices below. */
static int scan_visible(const BtScanEnt* e) {
	return e->name[0] != '\0';
}

static int scan_visible_count(BtInfo* info) {
	int c = 0;
	int i;
	for (i = 0; i < info->nscan; i++)
		if (scan_visible(&info->scan[i]))
			c++;
	return c;
}

/* 1-based display number -> kernel index, or -1. */
static int scan_display_to_kernel(BtInfo* info, int disp) {
	int c = 0;
	int i;
	for (i = 0; i < info->nscan; i++) {
		if (!scan_visible(&info->scan[i]))
			continue;
		if (++c == disp)
			return i;
	}
	return -1;
}

/* kernel index -> 1-based display number, or -1 if hidden. */
static int scan_kernel_to_display(BtInfo* info, int kidx) {
	int c = 0;
	int i;
	for (i = 0; i < info->nscan; i++) {
		if (!scan_visible(&info->scan[i]))
			continue;
		c++;
		if (i == kidx)
			return c;
	}
	return -1;
}

/* The "-LE" advert is the assistant side. Prefer a classic result whose
 * name is that device without the suffix. */
static int pick_audio_entry(BtInfo* info, int idx) {
	char base[BT_NAME_LEN];
	int bl;
	int i;
	if (idx < 0 || idx >= info->nscan)
		return idx;
	if (info->scan[idx].addr_type == BT_ADDR_BREDR || !ends_le_only(info->scan[idx].name))
		return idx;
	bl = (int)strlen(info->scan[idx].name) - 3;
	if (bl < 0)
		bl = 0;
	if (bl >= BT_NAME_LEN)
		bl = BT_NAME_LEN - 1;
	memcpy(base, info->scan[idx].name, (size_t)bl);
	base[bl] = 0;
	for (i = 0; i < info->nscan; i++) {
		if (info->scan[i].addr_type == BT_ADDR_BREDR &&
			!strncmp(info->scan[i].name, base, (size_t)bl))
			return i;
	}
	printf("%s is the LE assistant advert\n", info->scan[idx].name);
	return idx;
}

static int load_scan_target(int fd, BtInfo* info, const char* a) {
	int want = 0;
	int idx;
	int kidx;
	int disp;
	const char* s;
	if (!strchr(a, ':')) {
		s = a;
		while (*s >= '0' && *s <= '9') {
			want = want * 10 + (*s - '0');
			s++;
		}
		if (want < 1 || *s) {
			usage();
			return -1;
		}
		run(fd, BT_GET_INFO, info);
		if (info->nscan <= 0)
			run(fd, BT_SCAN, info);
		if (info->nscan < 0)
			info->nscan = 0;
		if (info->nscan > BT_MAX_SCAN)
			info->nscan = BT_MAX_SCAN;
		kidx = scan_display_to_kernel(info, want);
		if (kidx < 0) {
			printf("no scan entry %d (%d found); run btctl scan\n", want,
				   scan_visible_count(info));
			return -1;
		}
		idx = pick_audio_entry(info, kidx);
		if (idx != kidx) {
			disp = scan_kernel_to_display(info, idx);
			printf("using %d %s instead of %s\n", disp > 0 ? disp : idx + 1,
				   info->scan[idx].name, info->scan[kidx].name);
		}
		memcpy(info->peer, info->scan[idx].addr, 6);
		info->peer_type = info->scan[idx].addr_type;
		return 0;
	}
	if (parse_addr(a, info->peer, &info->peer_type)) {
		usage();
		return -1;
	}
	return 0;
}

static int hexval(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int parse_addr(const char* s, uint8_t* addr, uint8_t* type) {
	int i;
	*type = 0;
	for (i = 0; i < 6; i++) {
		int hi, lo;
		const char* p = s + (5 - i) * 3;
		hi = hexval(p[0]);
		lo = hexval(p[1]);
		if (hi < 0 || lo < 0)
			return -1;
		addr[i] = (uint8_t)((hi << 4) | lo);
		/* p walks the string backwards: every octet except the last
		 * displayed one (i == 0) is followed by ':'. The old i != 5 check
		 * demanded a ':' after the final octet, so every canonical address
		 * failed and `btctl connect <addr>` always hit usage. */
		if (i != 0 && p[2] != ':')
			return -1;
	}
	if (s[17] == 'r')
		*type = 1;
	return 0;
}

static void print_addr(const uint8_t* a) {
	int i;
	for (i = 5; i >= 0; i--) {
		printf("%02x", a[i]);
		if (i)
			printf(":");
	}
}

static int run(int fd, int code, BtInfo* info) {
	int st = _KeFileIoControl(fd, code, info);
	if (info->msg[0])
		printf("%s\n", info->msg);
	return st;
}

/* XEShell passes only the trailing words, so "btctl scan" arrives as
 * argv[0]=="scan". A loader that still puts the path in argv[0] is skipped. */
static int cmd_index(int argc, char** argv) {
	int i;
	for (i = 0; i < argc; i++) {
		const char* a = argv[i];
		if (!a || a[0] == '\0')
			continue;
		if (a[0] == '/' || strstr(a, ".exe") || !strcmp(a, "btctl"))
			continue;
		return i;
	}
	return -1;
}

int main(int argc, char* argv[]) {
	int fd;
	int idx;
	int argi;
	BtInfo info;
	const char* cmd;
	idx = cmd_index(argc, argv);
	if (idx < 0) {
		usage();
		return 1;
	}
	cmd = argv[idx];
	argi = idx + 1;
	fd = _KeOpenFile((char*)"/dev/bt0", FILE_OPEN_READ_ONLY | FILE_OPEN_WRITE);
	if (fd < 0) {
		printf("no adapter\n");
		return 1;
	}
	memset(&info, 0, sizeof info);
	if (!strcmp(cmd, "info")) {
		run(fd, BT_GET_INFO, &info);
		print_addr(info.bd_addr);
		printf(" iso=%u\n", info.iso);
		printf("connected=%u encrypted=%u", info.connected, info.encrypted);
		if (info.connected) {
			printf(" peer=");
			print_addr(info.peer);
			printf(" type=%u %s", info.peer_type, info.peer_name);
		}
		printf("\n");
	} else if (!strcmp(cmd, "scan")) {
		int i;
		int disp = 0;
		run(fd, BT_SCAN, &info);
		if (info.nscan < 0)
			info.nscan = 0;
		if (info.nscan > BT_MAX_SCAN)
			info.nscan = BT_MAX_SCAN;
		for (i = 0; i < info.nscan; i++) {
			if (!scan_visible(&info.scan[i]))
				continue;
			printf("%d: ", ++disp);
			print_addr(info.scan[i].addr);
			if (info.scan[i].addr_type == BT_ADDR_BREDR)
				printf(" bredr");
			else
				printf(" type=%u", info.scan[i].addr_type);
			printf(" rssi=%d %s\n", info.scan[i].rssi, info.scan[i].name);
		}
	} else if (!strcmp(cmd, "connect") && argi < argc) {
		if (load_scan_target(fd, &info, argv[argi]))
			return 1;
		run(fd, BT_CONNECT, &info);
	} else if (!strcmp(cmd, "disconnect")) {
		run(fd, BT_DISCONNECT, &info);
	} else if (!strcmp(cmd, "name")) {
		run(fd, BT_READ_NAME, &info);
		printf("%s\n", info.peer_name);
	} else if (!strcmp(cmd, "pair")) {
		int st;
		int chained = argi < argc;
		if (chained) {
			if (load_scan_target(fd, &info, argv[argi]))
				return 1;
			printf("connect ");
			print_addr(info.peer);
			printf(" %s\n", info.peer_type == BT_ADDR_BREDR ? "bredr" : "le");
			st = run(fd, BT_CONNECT, &info);
			if (st != BT_OK && info.status != BT_OK) {
				printf("connect failed\n");
				_KeCloseFile(fd);
				return 1;
			}
		}
		st = run(fd, BT_PAIR, &info);
		if (st == BT_NEED_CONFIRM || info.status == BT_NEED_CONFIRM) {
			printf("passkey %06u\n", info.passkey);
			if (chained)
				run(fd, BT_CONFIRM, &info);
			else
				printf("btctl confirm\n");
		}
		if (chained) {
			int ok = 0;
			if (info.peer_type == BT_ADDR_BREDR) {
				st = run(fd, BT_A2DP, &info);
				if (st == BT_NO_AUDIO || info.status == BT_NO_AUDIO)
					printf("no A2DP\n");
				else if (info.card_id >= 0) {
					printf("card %d\n", info.card_id);
					ok = 1;
				}
			} else {
				st = run(fd, BT_AUDIO, &info);
				if (st == BT_NO_AUDIO || info.status == BT_NO_AUDIO)
					printf("no LE Audio\n");
				else if (info.card_id >= 0) {
					printf("card %d\n", info.card_id);
					ok = 1;
				}
			}
			if (ok)
				play_demo_pcm(info.card_id);
		}
	} else if (!strcmp(cmd, "passkey") && argi < argc) {
		unsigned v = 0;
		const char* s = argv[argi];
		while (*s) {
			if (*s < '0' || *s > '9')
				break;
			v = v * 10u + (unsigned)(*s - '0');
			s++;
		}
		info.passkey = v;
		run(fd, BT_PASSKEY, &info);
	} else if (!strcmp(cmd, "confirm")) {
		run(fd, BT_CONFIRM, &info);
	} else if (!strcmp(cmd, "audio")) {
		int st = run(fd, BT_AUDIO, &info);
		if (st == BT_NO_AUDIO || info.status == BT_NO_AUDIO)
			printf("no LE Audio\n");
		else if (info.card_id >= 0) {
			printf("card %d\n", info.card_id);
			/* Only stream the demo when the transport is actually up.
			 * The old code always played locally, hiding failures. */
			play_demo_pcm(info.card_id);
		}
	} else if (!strcmp(cmd, "a2dp")) {
		int st = run(fd, BT_A2DP, &info);
		if (st == BT_NO_AUDIO || info.status == BT_NO_AUDIO)
			printf("no A2DP\n");
		else if (info.card_id >= 0) {
			printf("card %d\n", info.card_id);
			/* Classic SBC audio for BREDR headsets (ULT WEAR). Only
			 * push PCM when bredr0 is streaming. */
			play_demo_pcm(info.card_id);
		}
	} else if (!strcmp(cmd, "forget") && argi < argc) {
		if (load_scan_target(fd, &info, argv[argi]))
			return 1;
		run(fd, BT_FORGET, &info);
	} else {
		usage();
		return 1;
	}
	_KeCloseFile(fd);
	return 0;
}
