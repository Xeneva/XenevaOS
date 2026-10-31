/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

/*
 * Init the root process -- is a statically linked program that
 * aurora expects to get it running at very first when system
 * get started
 */

#include <stdint.h>
#include <stdio.h>
#include <_xeneva.h>
#include <sys/_keproc.h>
#include <string.h>
#include <sys/_heap.h>
#include <sys/mman.h>
#include <sys/_kefile.h>
#include <stdlib.h>
#include <sys/iocodes.h>
#include <sys/_kecred.h>
#include <sys/_ketime.h>

/** Let's hardcode,credentials
 * untill we get proper login manager
 */
#define GROUP_INPUT	  20
#define GROUP_VIDEO	  21
#define GROUP_TTY	  22
#define GROUP_AUDIO	  21
#define GROUP_NETWORK 23

/** hardcoded untill we get proper
 * login manager
 */
#define UAC_DEAMONS		40
#define UAC_NORMAL_USER 1000

int _sound;

/** Init Request msgs **/
#define INIT_REQUEST_PW_DOWN   "init.request.powerdown"
#define INIT_REQUEST_PW_REBOOT "init.request.reboot"

typedef struct _init_request_msg_ {
	char message[60];
	uint16_t fromProcessId;
	uint16_t toProcessId;
} InitRequestMsg;

void initSetupBasicEnvironmentVars() {
	setenv("HOME", "/", 1);
	setenv("USER", "xeuser", 1);
	setenv("USERNAME", "xeuser", 1);
	setenv("SHELL", "XenevaTerminal", 1);
	setenv("LANG", "en_US.UTF-8", 1); //Set it to English(United States), UTF-8
	setenv("OSNAME", "XenevaOS", 1);
}

/**
 * @brief init_run_term_command -- in TERM mode, read /shell.cnf and
 * launch the specified app with its arguments on /dev/console.
 * Falls back to xesh.exe if no config or exec fails.
 */
void init_run_term_command(int ggid_misc_world, int con) {
	char cmd[128] = {0};
	int cfg = _KeOpenFile("/shell.cnf", FILE_OPEN_READ_ONLY);
	if (cfg != -1) {
		_KeReadFile(cfg, cmd, sizeof(cmd) - 1);
		_KeCloseFile(cfg);
	}

	/* trim trailing newline/carriage-return */
	int len = strlen(cmd);
	while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r'))
		cmd[--len] = '\0';

	if (len == 0) {
		/* no command -- fall back to interactive shell */
		int proc = _KeCreateProcess(0, "xesh");
		_KeSetUID(proc, UAC_NORMAL_USER);
		_KeSetGID(proc, UAC_NORMAL_USER);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeCredAddSGroup(proc, GROUP_NETWORK);
		if (con != -1) {
			_KeSetFileToProcess(con, XENEVA_STDIN, proc);
			_KeSetFileToProcess(con, XENEVA_STDOUT, proc);
			_KeSetFileToProcess(con, XENEVA_STDERR, proc);
		}
		_KeProcessLoadExec(proc, "/xesh.exe", 0, NULL);
		return;
	}

	/* tokenize command line into argc/argv */
	char* argv[16];
	int argc = 0;
	char* tok = strtok(cmd, " ");
	while (tok && argc < 16) {
		argv[argc++] = tok;
		tok = strtok(NULL, " ");
	}

	if (argc == 0)
		return;

	/* build executable path: /<argv[0]>, plus .exe unless it is already a .elf */
	char path[64];
	path[0] = '/';
	strcpy(path + 1, argv[0]);
	/* A musl guest is named *.elf. Everything else is still a PE .exe. */
	if (strlen(argv[0]) < 4 || strcmp(argv[0] + strlen(argv[0]) - 4, ".elf") != 0)
		strcat(path, ".exe");

	int proc = _KeCreateProcess(0, argv[0]);
	_KeSetUID(proc, UAC_NORMAL_USER);
	_KeSetGID(proc, UAC_NORMAL_USER);
	_KeCredAddSGroup(proc, ggid_misc_world);
	_KeCredAddSGroup(proc, GROUP_NETWORK);
	if (con != -1) {
		_KeSetFileToProcess(con, XENEVA_STDIN, proc);
		_KeSetFileToProcess(con, XENEVA_STDOUT, proc);
		_KeSetFileToProcess(con, XENEVA_STDERR, proc);
	}
	/* skip argv[0] (command name) — kernel uses path as argv[0] */
	int ret = _KeProcessLoadExec(proc, path, argc > 1 ? argc - 1 : 0, argc > 1 ? argv + 1 : NULL);
	if (ret == -1) {
		_KePrint("[init]: failed to load %s, falling back to xesh\r\n", path);
		proc = _KeCreateProcess(0, "xesh");
		_KeSetUID(proc, UAC_NORMAL_USER);
		_KeSetGID(proc, UAC_NORMAL_USER);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeCredAddSGroup(proc, GROUP_NETWORK);
		if (con != -1) {
			_KeSetFileToProcess(con, XENEVA_STDIN, proc);
			_KeSetFileToProcess(con, XENEVA_STDOUT, proc);
			_KeSetFileToProcess(con, XENEVA_STDERR, proc);
		}
		_KeProcessLoadExec(proc, "/xesh.exe", 0, NULL);
	}
}

typedef struct _sound_card_list {
	char name[32];
	int cardID;
	struct _sound_card_list* next;
} aurora_snd_card_list;

extern void SplashScreenShow();

void init_basic_gid_to_dev() {
	int fd = _KeOpenFile("/dev/graph", FILE_OPEN_READ_ONLY);
	if (fd != -1) {
		_KeCredChangeID(fd, 0, GROUP_VIDEO);
	}
	fd = _KeOpenFile("/dev/mice", FILE_OPEN_READ_ONLY);
	if (fd != -1) {
		_KeCredChangeID(fd, 0, GROUP_INPUT);
	}
	fd = _KeOpenFile("/dev/kybrd", FILE_OPEN_READ_ONLY);
	if (fd != -1) {
		_KeCredChangeID(fd, 0, GROUP_INPUT);
	}
	/* The input-ring carries the same mouse/keyboard events as mice/kybrd,
	 * so it needs the same group or the compositor (uid 1000) is denied and
	 * falls back to the legacy per-frame devices. --axiss */
	fd = _KeOpenFile("/dev/input-ring", FILE_OPEN_READ_ONLY);
	if (fd != -1) {
		_KeCredChangeID(fd, 0, GROUP_INPUT);
	}
	fd = _KeOpenFile("/dev/sound", FILE_OPEN_READ_ONLY);
	if (fd != -1) {
		_KeCredChangeID(fd, 0, GROUP_AUDIO);
		_sound = fd;
	}
	fd = _KeOpenFile("/dev/virtiogpu", FILE_OPEN_READ_ONLY);
	if (fd != -1) {
		_KeCredChangeID(fd, 0, GROUP_VIDEO);
	}
	fd = _KeOpenFile("/dev/net/virtio-net", FILE_OPEN_READ_ONLY);
	if (fd != -1) {
		_KeCredChangeID(fd, 0, GROUP_NETWORK);
	}
	/* The desktop terminal is started by the compositor, which does not
	 * have GROUP_NETWORK or GROUP_AUDIO. Every normal user process does
	 * have the world group, so btctl and wifictl in that terminal can
	 * open these nodes the same way this process does. */
	fd = _KeOpenFile("/dev/net/wlan0", FILE_OPEN_READ_ONLY);
	if (fd != -1) {
		_KeCredChangeID(fd, 0, _KeGetGlobalGroupID(AURORA_GID_MISC_WORLD));
	}
	fd = _KeOpenFile("/dev/bt0", FILE_OPEN_READ_ONLY);
	if (fd != -1) {
		_KeCredChangeID(fd, 0, _KeGetGlobalGroupID(AURORA_GID_MISC_WORLD));
	}
	if (_sound != -1)
		_KeCredChangeID(_sound, 0, _KeGetGlobalGroupID(AURORA_GID_MISC_WORLD));
}

/**
 * _play_startup_sound -- play the startup sound
 */
void _play_startup_sound() {
	if (_sound == -1)
		return;

	XEFileIOControl ioctl;
	memset(&ioctl, 0, sizeof(XEFileIOControl));

	/* uint_1 holds the millisecond to sleep after
	* one frame playback */
	ioctl.uint_1 = 0;
	ioctl.syscall_magic = AURORA_SYSCALL_MAGIC;
	/** uint_2 must hold the sound card number to use **/
	ioctl.uint_2 = -1;
	int num_card_count = _KeFileIoControl(_sound, SOUND_GET_CARD_TOTALNUM, &ioctl);
	if (num_card_count == 0)
		return;

	ioctl.uint_1 = num_card_count;
	aurora_snd_card_list* list =
		(aurora_snd_card_list*)malloc(sizeof(aurora_snd_card_list) * num_card_count);
	ioctl.ulong_1 = (uint64_t)list;
	if (_KeFileIoControl(_sound, SOUND_GET_CARD_LIST, &ioctl)) {
		_KePrint("[init]: failed to get sound card list \r\n");
		_KePauseThread();
	}

	/** just print all card name once **/
	for (int i = 0; i < num_card_count; i++) {
		_KePrint("[init]: %s sound is installed, id : %d \r\n", list[i].name, list[i].cardID);
	}

	/** let's use default first sound card here **/
	ioctl.uint_2 = list->cardID;

	_KeFileIoControl(_sound, SOUND_REGISTER_SNDPLR, &ioctl);

	int song = _KeOpenFile("/Ss.wav", FILE_OPEN_READ_ONLY);
	if (song == -1) {
		return;
	}

	void* songbuf = malloc(4096);
	memset(songbuf, 0, 4096);
	_KeReadFile(song, songbuf, 4096);
	uint8_t* alignedSongBuf = (uint8_t*)songbuf;

	XEFileStatus fs;
	_KeFileStat(song, &fs);
	bool finished = 0;
	while (1) {
		/* with each frame read the sound, write it
		* to sound device, the sound device will automatically
		* put the app to sleep for smooth playback for some
		* milli-seconds
		*/
		_KeFileStat(song, &fs);

		if (fs.eof) {
			finished = 1;
			return;
		}

		if (!finished) {
			_KeReadFile(song, songbuf, 4096);
			_KeWriteFile(_sound, songbuf, 4096);
		}
		_KeProcessSleep(4);
	}
}

/**
 * @brief _init_handle_request -- handle request
 * msgs
 */
void _init_handle_request(InitRequestMsg* msg) {
	if (strcmp(msg->message, INIT_REQUEST_PW_DOWN) == 0) {
		_KePrint("[init]: shutting down xeneva \r\n");
		//clean up all init resources

		/* here also , we need to verify if the request
		 * came from active-local user id, or else deny request
		 */
		_KePowerDown();
		// no return
	}

	if (strcmp(msg->message, INIT_REQUEST_PW_REBOOT) == 0) {
		_KePrint("[init]: restarting xeneva \r\n");
		/* here also , we need to verify if the request
		 * came from active-local user id, or else deny request
		 */
		_KePowerReset();
		// no return
	}
}

/*
 * _main -- main entry point
 */
/* ---- supervising the supervisor ------------------------------------------
 * init is the process reaper, so it cannot block on one pid; instead it checks
 * from inside its reaper loop whether svcmgr still exists and relaunches it
 * (at most 5 times in 60 s). A relaunched svcmgr adopts the services that are
 * still running, so nothing it supervised is disturbed. */
static int g_svcmgr_pid = -1;
static unsigned g_svcmgr_restarts;
static uint64_t g_svcmgr_window, g_svcmgr_last_check;

static int launch_svcmgr() {
	int daemons = _KeGetGlobalGroupID(AURORA_GID_MISC_DAEMONS);
	int world = _KeGetGlobalGroupID(AURORA_GID_MISC_WORLD);
	int p = _KeCreateProcess(0, "svcmgr");
	if (p < 0)
		return -1;
	_KeSetUID(p, UAC_NORMAL_USER);
	_KeSetGID(p, UAC_NORMAL_USER);
	_KeCredAddSGroup(p, daemons);
	_KeCredAddSGroup(p, world); /* to read /services.cnf and /svctest.cnf */
	_KeCredSetCap(p, 0x23);		/* CRED_CAP_SETUID | SETGID | SETCAP */
	if (_KeProcessLoadExec(p, "/svcmgr.exe", 0, NULL) == -1)
		return -1;
	return p;
}

static bool init_pid_present(int pid) {
	static XEProcessList list[96];
	int n = _KeGetNumProcessCount();
	if (n > 96)
		n = 96;
	memset(list, 0, sizeof list);
	if (_KeProcessFetch(list, n) != 0)
		return true; /* cannot tell: assume it is there */
	for (int i = 0; i < n; i++)
		if (list[i].proc_id == pid)
			return true;
	return false;
}

static void init_watch_svcmgr() {
	if (g_svcmgr_pid <= 0)
		return;
	uint64_t now = _KeGetCurrentMS();
	if (now - g_svcmgr_last_check < 500)
		return;
	g_svcmgr_last_check = now;
	if (init_pid_present(g_svcmgr_pid))
		return;
	if (now - g_svcmgr_window > 60000) {
		g_svcmgr_window = now;
		g_svcmgr_restarts = 0;
	}
	if (++g_svcmgr_restarts > 5) {
		_KePrint("[init]: svcmgr keeps dying, no longer restarting it\r\n");
		g_svcmgr_pid = -1;
		return;
	}
	_KePrint("[init]: svcmgr is gone, restarting it\r\n");
	g_svcmgr_pid = launch_svcmgr();
}

extern "C" void main(int argc, char* argv[]) {
	int pid = _KeGetProcessID();

	_KePrint("Init Process running ii %d\n", pid);
	int threadID = _KeGetThreadID();
	_KePrint("ThreadID : %d \n", threadID);

	if (strcmp(argv[0], "-about") == 0) {
#ifdef ARCH_X64
		_KePrint("Xeneva v1.0 x86_64 !! Copyright (C) Manas Kamal Choudhury 2020-2023 \r\n");
#elif ARCH_ARM64
		_KePrint("Xeneva v1.0 ARM64 !! Copyright (C) Manas Kamal Choudhury 2020-2025 \r\n");
#endif
	}

#if !defined(__XENEVA_TERM__)
	SplashScreenShow();
#endif
	_sound = -1;
	init_basic_gid_to_dev();

	/** play the startup sound, for better experience */
#if !defined(__XENEVA_TERM__)
	_play_startup_sound();
#endif

	int ggid_misc_world = _KeGetGlobalGroupID(AURORA_GID_MISC_WORLD);
	int ggid_misc_postbox = _KeGetGlobalGroupID(AURORA_GID_IPC_POSTBOX);

	/* open up a pipe, to get request from normal application */
	int pipe = _KeCreatePipe("init", (sizeof(InitRequestMsg) * 4));
	if (pipe == -1)
		_KePrint("[init]: pipe creation failed \r\n");
	else
		_KeCredChangeID(pipe, 0, ggid_misc_world);

	/** allocate a memory for init request msgs */
	char* init_msg_buff = (char*)malloc(sizeof(InitRequestMsg) + 1);
	memset(init_msg_buff, 0, sizeof(InitRequestMsg) + 1);

	/** TODO: add IPC system to track real system progress and animate the logo accordingly **/
	_KeProcessSleep(100);

	int proc = 0;

	/* DCL self-test on every boot. Runs only while dcltest.exe is in the
	 * image, so the binary itself is the opt-out -- same absence rule as
	 * deoaud.exe below: a removed component stays absent and init does not
	 * leak a process slot. It is spawned and never awaited, so desktop
	 * startup is unaffected (the app finishes in ~40 ms).
	 *
	 * No console fd is handed out on purpose. Results go through /dev/kmsg
	 * onto the serial log rather than painting over the boot screen, which
	 * is also what the checkers grep for.
	 *
	 * UAC_NORMAL_USER + ggid_misc_world is the credential set a real
	 * application gets; the DCL nodes are uid0/gid-world, so this proves
	 * the permission path on every boot instead of only by hand. */
	int dtest = _KeOpenFile("/dcltest.exe", FILE_OPEN_READ_ONLY);
	if (dtest != -1) {
		_KeCloseFile(dtest);
		proc = _KeCreateProcess(0, "dcltest");
		_KeSetUID(proc, UAC_NORMAL_USER);
		_KeSetGID(proc, UAC_NORMAL_USER);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeProcessLoadExec(proc, "/dcltest.exe", 0, NULL);
	} else {
		_KePrint("[init]: dcltest.exe absent, skipping DCL self-test\r\n");
	}

	/* hwtest: user-space driver access test for /dev/hwdev. Only present when
	 * the image was built with HWTEST=1. It is the one process that gets the
	 * MISC_DAEMONS group, which is what /dev/hwdev and /dev/drvctl require --
	 * dcltest and clipd above deliberately do not have it. */
	int htest = _KeOpenFile("/hwtest.exe", FILE_OPEN_READ_ONLY);
	if (htest != -1) {
		_KeCloseFile(htest);
		int ggid_daemons = _KeGetGlobalGroupID(AURORA_GID_MISC_DAEMONS);
		proc = _KeCreateProcess(0, "hwtest");
		_KeSetUID(proc, UAC_NORMAL_USER);
		_KeSetGID(proc, UAC_NORMAL_USER);
		_KeCredAddSGroup(proc, ggid_daemons);
		_KeProcessLoadExec(proc, "/hwtest.exe", 0, NULL);
	}

	/* sndsrv: user-space virtio-sound driver, and sndplay: a client that plays a
	 * tone through it. Both absent unless the image was built with SNDSRV=1.
	 * sndsrv is the daemons-group process; sndplay is an ordinary application. */
	/* With svcmgr present, the supervisor owns sndsrv: it restarts it when it
	 * dies and hands the device back to the in-kernel driver if it keeps
	 * crashing. svcmgr gets only the credential capabilities it needs to set a
	 * child up (setuid | setgid | setcap) and strips them from every child. */
	int svc = _KeOpenFile("/svcmgr.exe", FILE_OPEN_READ_ONLY);
	if (svc != -1) {
		_KeCloseFile(svc);
		g_svcmgr_pid = launch_svcmgr();
		g_svcmgr_window = _KeGetCurrentMS();
	}
	int ssrv = (svc == -1) ? _KeOpenFile("/sndsrv.exe", FILE_OPEN_READ_ONLY) : -1;
	if (ssrv != -1) {
		_KeCloseFile(ssrv);
		int ggid_daemons = _KeGetGlobalGroupID(AURORA_GID_MISC_DAEMONS);
		proc = _KeCreateProcess(0, "sndsrv");
		_KeSetUID(proc, UAC_NORMAL_USER);
		_KeSetGID(proc, UAC_NORMAL_USER);
		_KeCredAddSGroup(proc, ggid_daemons);
		_KeProcessLoadExec(proc, "/sndsrv.exe", 0, NULL);
	}
	int splay = _KeOpenFile("/sndplay.exe", FILE_OPEN_READ_ONLY);
	if (splay != -1) {
		_KeCloseFile(splay);
		proc = _KeCreateProcess(0, "sndplay");
		_KeSetUID(proc, UAC_NORMAL_USER);
		_KeSetGID(proc, UAC_NORMAL_USER);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeProcessLoadExec(proc, "/sndplay.exe", 0, NULL);
	}

	/* ipcsrv + ipctest: the generic IPC test (/dev/ipc). Present only when the image
	 * was built with IPCTEST=1. The server is a daemons-group process (it creates
	 * endpoints) that also holds the world group the /dev/ipc node requires; the
	 * client is an ordinary application and must be refused everything privileged. */
	int isrv = _KeOpenFile("/ipcsrv.exe", FILE_OPEN_READ_ONLY);
	if (isrv != -1) {
		_KeCloseFile(isrv);
		int ggid_daemons = _KeGetGlobalGroupID(AURORA_GID_MISC_DAEMONS);
		proc = _KeCreateProcess(0, "ipcsrv");
		_KeSetUID(proc, UAC_NORMAL_USER);
		_KeSetGID(proc, UAC_NORMAL_USER);
		_KeCredAddSGroup(proc, ggid_daemons);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeProcessLoadExec(proc, "/ipcsrv.exe", 0, NULL);
	}
	int itest = _KeOpenFile("/ipctest.exe", FILE_OPEN_READ_ONLY);
	if (itest != -1) {
		_KeCloseFile(itest);
		proc = _KeCreateProcess(0, "ipctest");
		_KeSetUID(proc, UAC_NORMAL_USER);
		_KeSetGID(proc, UAC_NORMAL_USER);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeProcessLoadExec(proc, "/ipctest.exe", 0, NULL);
	}

	/* fssrv + srvtest: a user-space file system on /srv. fssrv is a daemons-group
	 * process (it owns the "fs" IPC endpoint the kernel forwards /srv requests to);
	 * two srvtest instances run as different users to exercise server-side
	 * permissions. All three exist only when the image was built with SRVTEST=1. */
	/* with svcmgr present it owns fssrv (restarts it); otherwise start it directly */
	int fsrv = (svc == -1) ? _KeOpenFile("/fssrv.exe", FILE_OPEN_READ_ONLY) : -1;
	if (fsrv != -1) {
		_KeCloseFile(fsrv);
		int ggid_daemons = _KeGetGlobalGroupID(AURORA_GID_MISC_DAEMONS);
		proc = _KeCreateProcess(0, "fssrv");
		_KeSetUID(proc, UAC_NORMAL_USER);
		_KeSetGID(proc, UAC_NORMAL_USER);
		_KeCredAddSGroup(proc, ggid_daemons);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeProcessLoadExec(proc, "/fssrv.exe", 0, NULL);
	}
	int stest = _KeOpenFile("/srvtest.exe", FILE_OPEN_READ_ONLY);
	if (stest != -1) {
		_KeCloseFile(stest);
		proc = _KeCreateProcess(0, "srvtest");
		_KeSetUID(proc, UAC_NORMAL_USER);
		_KeSetGID(proc, UAC_NORMAL_USER);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeProcessLoadExec(proc, "/srvtest.exe", 0, NULL);
		static char arg_other[] = "other";
		static char* argv_other[] = {arg_other};
		proc = _KeCreateProcess(0, "srvtest2");
		_KeSetUID(proc, UAC_NORMAL_USER + 1);
		_KeSetGID(proc, UAC_NORMAL_USER + 1);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeProcessLoadExec(proc, "/srvtest.exe", 1, argv_other);
	}

	/* nettest: pings the gateway every 2 s and logs each result; only present
	 * when the image was built with NETSRV=1. An ordinary networked app. */
	int ntest = _KeOpenFile("/nettest.exe", FILE_OPEN_READ_ONLY);
	if (ntest != -1) {
		_KeCloseFile(ntest);
		proc = _KeCreateProcess(0, "nettest");
		_KeSetUID(proc, UAC_NORMAL_USER);
		_KeSetGID(proc, UAC_NORMAL_USER);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeCredAddSGroup(proc, GROUP_NETWORK);
		_KeProcessLoadExec(proc, "/nettest.exe", 0, NULL);
	}

	/* clipd: the guest half of the host<->VM clipboard. Same absence rule
	 * as dcltest.exe above -- no binary, no process slot, no leaked entry
	 * -- and the same credential set, because it opens the same class of
	 * node: /dev/clipboard and /dev/vport* are uid0/gid-world, so it gets
	 * world-readable misc and nothing more. It is spawned and never
	 * awaited, so a boot with no host on the other end is unaffected. */
	int cdaemon = _KeOpenFile("/clipd.exe", FILE_OPEN_READ_ONLY);
	if (cdaemon != -1) {
		_KeCloseFile(cdaemon);
		proc = _KeCreateProcess(0, "clipd");
		_KeSetUID(proc, UAC_NORMAL_USER);
		_KeSetGID(proc, UAC_NORMAL_USER);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeProcessLoadExec(proc, "/clipd.exe", 0, NULL);
	} else {
		_KePrint("[init]: clipd.exe absent, no clipboard bridge\r\n");
	}

#ifdef ARCH_ARM64
#ifdef __XENEVA_TERM__
	_KePrint("[init]: tty, skipping compositor \r\n");
	proc = _KeCreateProcess(0, "netmngr");
	_KeSetUID(proc, UAC_DEAMONS);
	_KeSetGID(proc, UAC_DEAMONS);
	_KeCredAddSGroup(proc, ggid_misc_world);
	_KeCredAddSGroup(proc, GROUP_NETWORK);
	int ret_nm = _KeProcessLoadExec(proc, "/netmngr.exe", 0, NULL);
	if (ret_nm != -1)
		_KeProcessSleep(500);
	/* --no-network drops ntpd.exe from the image, so check it exists before
	 * creating the process, otherwise I leave a half-created ntpd behind --axiss */
	int ntpd = _KeOpenFile("/ntpd.exe", FILE_OPEN_READ_ONLY);
	if (ntpd != -1) {
		_KeCloseFile(ntpd);
		proc = _KeCreateProcess(0, "ntpd");
		_KeSetUID(proc, UAC_DEAMONS);
		_KeSetGID(proc, UAC_DEAMONS);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeCredAddSGroup(proc, GROUP_NETWORK);
		if (_KeProcessLoadExec(proc, "/ntpd.exe", 0, NULL) == -1)
			_KePrint("[init]: ntpd not started\r\n");
	}
	int con = _KeOpenFile("/dev/console", FILE_OPEN_READ_ONLY);
	if (con == -1) {
		_KePrint("[init]: failed to open /dev/console \r\n");
	} else {
		/* TERM owns the display: move the kernel console back onto the
		 * boot framebuffer (ramfb tab). The virtio-gpu driver repoints it
		 * at its own backing for the compositor, which never runs here,
		 * so without this the shell would be alive but invisible. --axiss */
		_KeFileIoControl(con, SCREEN_RESTORE_BOOT_FB, NULL);
	}
	init_run_term_command(ggid_misc_world, con);
#else
	proc = _KeCreateProcess(0, "netmngr");
	_KeSetUID(proc, UAC_DEAMONS);
	_KeSetGID(proc, UAC_DEAMONS);
	_KeCredAddSGroup(proc, ggid_misc_world);
	_KeCredAddSGroup(proc, GROUP_NETWORK);
	int ret_nm = _KeProcessLoadExec(proc, "/netmngr.exe", 0, NULL);
	if (ret_nm != -1)
		_KeProcessSleep(500);
	/* --no-network drops ntpd.exe from the image, so check it exists before
	 * creating the process, otherwise I leave a half-created ntpd behind --axiss */
	int ntpd = _KeOpenFile("/ntpd.exe", FILE_OPEN_READ_ONLY);
	if (ntpd != -1) {
		_KeCloseFile(ntpd);
		proc = _KeCreateProcess(0, "ntpd");
		_KeSetUID(proc, UAC_DEAMONS);
		_KeSetGID(proc, UAC_DEAMONS);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeCredAddSGroup(proc, GROUP_NETWORK);
		if (_KeProcessLoadExec(proc, "/ntpd.exe", 0, NULL) == -1)
			_KePrint("[init]: ntpd not started\r\n");
	}

	/** actually, design should be like that, each process after
	 * finish its initialization, it should send a signal to 
	 * init, so that it can continue next proccesses spawning
	 */

	/** from now, normal user's won't get system access */
	proc = _KeCreateProcess(0, "deodhaixr");
	_KeSetUID(proc, UAC_NORMAL_USER);
	_KeSetGID(proc, UAC_NORMAL_USER);
	_KeCredAddSGroup(proc, ggid_misc_world);
	_KeCredAddSGroup(proc, ggid_misc_postbox);
	_KeCredAddSGroup(proc, GROUP_VIDEO);
	_KeCredAddSGroup(proc, GROUP_INPUT);
	_KeCredSetCap(proc, 0);
	_KeProcessLoadExec(proc, "/deodxr.exe", 0, NULL);

	_KeProcessSleep(800);

	/* Microkernel rule: a removed daemon is an absent binary, and init
	 * must skip it without leaking a process slot. --no-audio drops
	 * deoaud.exe from the image; without this guard init would create
	 * the process and fail the exec anyway. --axiss */
	int daud = _KeOpenFile("/deoaud.exe", FILE_OPEN_READ_ONLY);
	if (daud != -1) {
		_KeCloseFile(daud);
		proc = _KeCreateProcess(0, "deoaud");
		_KePrint("deoaud proc id : %d \r\n", proc);
		_KeSetUID(proc, UAC_DEAMONS);
		_KeSetGID(proc, UAC_DEAMONS);
		_KeCredAddSGroup(proc, ggid_misc_world);
		_KeCredAddSGroup(proc, GROUP_AUDIO);
		_KeCredAddSGroup(proc, ggid_misc_postbox);
		_KeProcessLoadExec(proc, "/deoaud.exe", 0, NULL);
	} else {
		_KePrint("[init]: deoaud.exe absent, skipping audio\r\n");
	}
#endif /* non-TERM: TERM builds run console-only, no window manager --axiss */

#elif ARCH_X64
	proc = _KeCreateProcess(0, "deodhai");
	_KeProcessLoadExec(proc, "/deodhai.exe", 0, NULL);
#endif

	/*_KeProcessSleep(1000);
	proc = _KeCreateProcess(0, "calc");
	_KeProcessLoadExec(proc, "/calc.exe", 0, NULL);*/

	_KePrint("Setting up env variable \r\n");
	initSetupBasicEnvironmentVars();
	int sz = 0;
	while (1) {
		sz = _KeReadFile(pipe, init_msg_buff, sizeof(InitRequestMsg) + 1);
		if (sz > 0) {
			_init_handle_request((InitRequestMsg*)init_msg_buff);
			memset(init_msg_buff, 0, sizeof(InitRequestMsg) + 1);
		}
		if (pid == 1)
			if (_KeProcessWaitForTermination(-1) == -1) {
				_KeProcessSleep(1000);
			}
		init_watch_svcmgr();
	}

	///* just load all the background services */
	int child = _KeCreateProcess(0, "deoaud");
	_KeProcessLoadExec(child, "/deoaud.exe", 0, NULL);

	_KeProcessSleep(1);

	child = _KeCreateProcess(0, "deodhai");
	_KeProcessLoadExec(child, "/deodhai.exe", 0, NULL);

	_KeProcessSleep(100);

	_KePrint("Deodhai spawned \r\n");

	/* launch the session manager directly from here */
	child = _KeCreateProcess(0, "xelnch");
	_KeProcessLoadExec(child, "/xelnch.exe", 0, NULL);

	_KeProcessSleep(100);

	child = _KeCreateProcess(0, "nmdapha");
	_KeProcessLoadExec(child, "/nmdapha.exe", 0, NULL);

	_KeProcessSleep(100);

	while (1) {
		/* from here, enter as a killer of all dead processes*/
		if (pid == 1) {
			_KeProcessWaitForTermination(-1);
		}
		init_watch_svcmgr();
		_KeProcessSleep(100);
	}
}
