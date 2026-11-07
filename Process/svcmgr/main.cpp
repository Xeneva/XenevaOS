/*
 * Process/svcmgr/main.cpp -- supervisor for user-space services (driver servers).
 *
 * In a microkernel a driver is just a process, so something has to notice when
 * it dies and bring it back. svcmgr reads a service table (/services.cnf), runs
 * one watcher thread per service, and for each one:
 *
 *   - starts it as a child (or ADOPTS an instance that is already running, which
 *     is what makes svcmgr itself restartable by init without disturbing the
 *     services it supervised)
 *   - blocks until it exits or crashes, then restarts it with exponential backoff
 *   - after more than max_restarts crashes inside window_s it stops restarting and
 *     runs the service's hand-back command, so the device returns to the in-kernel
 *     driver instead of being lost
 *   - after cooldown_s it puts the service on PROBATION: starts it again with a
 *     single strike left; if it then stays up for stable_s the slate is wiped,
 *     otherwise the cooldown doubles (up to five minutes)
 *
 * Table format, one service per line, '#' starts a comment:
 *   name  path  handback  max_restarts  window_s  cooldown_s  stable_s  groups
 * handback is a /dev/drvctl command with '_' for spaces, or '-' for none; groups
 * is a comma list of daemons,world or '-'. Without a table a built-in sndsrv
 * entry is used.
 *
 * Least privilege: init grants svcmgr only setuid|setgid|setcap, and svcmgr strips
 * ALL capabilities from every service right after creating it.
 *
 * Test hooks exist only when built with SVCTEST (-D__XENEVA_SVC_TEST__), read
 * from /svctest.cnf: restart | fallback | recover | selfcrash | gpurestart |
 * gpufallback.
 *
 * Results go to stdout and /dev/kmsg:  grep '\[svcmgr\]' run.log
 */
#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <sys/_kecred.h>
#include <sys/_ketime.h>
#include <drvctl.h>

#define UAC_NORMAL_USER   1000
#define MAX_SERVICES      8
#define MAX_COOLDOWN_MS   300000ULL
#define BACKOFF_BASE_MS   200

struct Service {
	char name[16];
	char path[48];
	char handback[72];            /* drvctl command(s) separated by ';', empty = none */
	unsigned max_restarts;
	uint64_t window_ms, cooldown_ms, stable_ms;
	bool group_daemons, group_world;
	/* runtime */
	int pid;
	unsigned starts, crashes;
	uint64_t window_start, cooldown_cur_ms;
	bool adopted;
};

static Service g_svc[MAX_SERVICES];
static int g_nsvc;
static int g_kmsg = -1;
static char g_mode[16];            /* test mode, empty in a production build */
static volatile int g_adopted_any;

/* _KeProcessSleep counts scheduler events, not milliseconds: an idle system
 * sleeps several times too long and a busy one too short. Supervision timers
 * (backoff, cool-down) must be real time, so sleep in short slices until a
 * deadline on the accurate clock. */
static void sleep_ms(uint64_t ms) {
	uint64_t end = _KeGetCurrentMS() + ms;
	for (;;) {
		uint64_t now = _KeGetCurrentMS();
		if (now >= end)
			return;
		uint64_t left = end - now;
		_KeProcessSleep((unsigned)(left < 5 ? left : 5));
	}
}

static void emit(const char* line) {
	printf("%s", line);
	if (g_kmsg < 0)
		g_kmsg = _KeOpenFile((char*)"/dev/kmsg", FILE_OPEN_WRITE);
	if (g_kmsg >= 0)
		_KeWriteFile(g_kmsg, (void*)line, strlen(line));
}

static int drvctl(const char* cmd) {
	int fd = _KeOpenFile((char*)"/dev/drvctl", FILE_OPEN_WRITE);
	if (fd < 0)
		return -100;
	AuDriverCtlRequest req;
	memset(&req, 0, sizeof req);
	size_t n = strlen(cmd);
	if (n >= sizeof(req.command))
		n = sizeof(req.command) - 1;
	memcpy(req.command, (void*)cmd, n);
	int rc = _KeFileIoControl(fd, AU_DRVCTL_IOC_EXEC, &req);
	_KeCloseFile(fd);
	return rc;
}

/* ---------------------------------------------------------------- config -- */
static int read_file(const char* path, char* buf, int cap) {
	int fd = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	if (fd < 0)
		return -1;
	memset(buf, 0, cap);
	int n = (int)_KeReadFile(fd, buf, cap - 1);
	_KeCloseFile(fd);
	return n;
}

/* split a line into whitespace-separated fields in place */
static int split(char* line, char** f, int max) {
	int n = 0;
	char* p = line;
	while (*p && n < max) {
		while (*p == ' ' || *p == '\t')
			*p++ = 0;
		if (!*p)
			break;
		f[n++] = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
	}
	return n;
}

static uint64_t num(const char* s) {
	uint64_t v = 0;
	while (*s >= '0' && *s <= '9')
		v = v * 10 + (uint64_t)(*s++ - '0');
	return v;
}

static void add_service(char** f, int nf) {
	if (g_nsvc >= MAX_SERVICES || nf < 7)
		return;
	Service* s = &g_svc[g_nsvc++];
	memset(s, 0, sizeof *s);
	strncpy(s->name, f[0], sizeof s->name - 1);
	strncpy(s->path, f[1], sizeof s->path - 1);
	if (strcmp(f[2], "-")) {
		strncpy(s->handback, f[2], sizeof s->handback - 1);
		for (char* c = s->handback; *c; c++)
			if (*c == '_')
				*c = ' ';
	}
	s->max_restarts = (unsigned)num(f[3]);
	s->window_ms = num(f[4]) * 1000;
	s->cooldown_ms = num(f[5]) * 1000;
	s->stable_ms = num(f[6]) * 1000;
	if (nf > 7) {
		s->group_daemons = strstr(f[7], "daemons") != 0;
		s->group_world = strstr(f[7], "world") != 0;
	}
	s->pid = -1;
}

static void load_config() {
	static char buf[2048];
	if (read_file("/services.cnf", buf, sizeof buf) > 0) {
		for (char* line = buf; *line;) {
			char* next = line;
			while (*next && *next != '\n')
				next++;
			if (*next)
				*next++ = 0;
			char* f[10];
			if (line[0] != '#') {
				int nf = split(line, f, 10);
				if (nf >= 7)
					add_service(f, nf);
			}
			line = next;
		}
	}
	if (!g_nsvc) {
		char def[] = "sndsrv /sndsrv.exe load_virtsnd 3 60 30 20 daemons";
		char* f[10];
		add_service(f, split(def, f, 10));
	}
#ifdef __XENEVA_SVC_TEST__
	char tm[32];
	if (read_file("/svctest.cnf", tm, sizeof tm) > 0) {
		int i = 0;
		while (i < 15 && tm[i] > ' ')
			g_mode[i] = tm[i], i++;
		g_mode[i] = 0;
	}
	if (g_mode[0])                               /* shorten the timers so a test finishes quickly */
		for (int i = 0; i < g_nsvc; i++) {
			g_svc[i].cooldown_ms = 4000;
			g_svc[i].stable_ms = 6000;
		}
#endif
}

/* -------------------------------------------------------------- services -- */
static int find_running(const char* name) {
	static XEProcessList list[96];
	int n = _KeGetNumProcessCount();
	if (n > 96)
		n = 96;
	memset(list, 0, sizeof list);
	if (_KeProcessFetch(list, n) != 0)
		return -1;
	int me = _KeGetProcessID();
	for (int i = 0; i < n; i++)
		if (list[i].proc_id != me && !strncmp(list[i].name, name, 15))
			return list[i].proc_id;
	return -1;
}

static int start_service(Service* s) {
	char arg[40];
	char* argv[3];
	int argc = 0;
	argv[argc++] = s->name;
	arg[0] = 0;
#ifdef __XENEVA_SVC_TEST__
	if (!strcmp(s->name, "sndsrv")) {
		if (!strcmp(g_mode, "restart") && s->starts == 0)
			strcpy(arg, "crash-after=40");
		else if (!strcmp(g_mode, "fallback"))
			strcpy(arg, "crash-after=12");
		else if (!strcmp(g_mode, "recover") && s->starts < 4)
			strcpy(arg, "crash-after=12");
	}
	if (!strcmp(s->name, "inputsrv")) {
		if (!strcmp(g_mode, "inrestart") && s->starts == 0)
			strcpy(arg, "crash-after=4");              /* dies after 4 injected events, once */
		else if (!strcmp(g_mode, "infallback"))
			strcpy(arg, "crash-after=2");              /* dies on every start */
	}
	if (!strcmp(s->name, "netsrv")) {
		if (!strcmp(g_mode, "netrestart") && s->starts == 0)
			strcpy(arg, "crash-after=24");             /* dies after 24 frames, once */
		else if (!strcmp(g_mode, "netfallback"))
			strcpy(arg, "crash-after=2");              /* dies on every start, fast */
	}
	if (!strcmp(s->name, "gpusrv")) {
		if (!strcmp(g_mode, "gpurestart") && s->starts == 0)
			strcpy(arg, "crash-after=8");              /* crash after a real frame, once */
		else if (!strcmp(g_mode, "gpufallback"))
			strcpy(arg, "crash-after=2");              /* crash on every start */
	}
#endif
	if (arg[0])
		argv[argc++] = arg;

	int pid = _KeCreateProcess(0, s->name);
	if (pid < 0)
		return -1;
	_KeSetUID(pid, UAC_NORMAL_USER);
	_KeSetGID(pid, UAC_NORMAL_USER);
	if (s->group_daemons)
		_KeCredAddSGroup(pid, _KeGetGlobalGroupID(AURORA_GID_MISC_DAEMONS));
	if (s->group_world)
		_KeCredAddSGroup(pid, _KeGetGlobalGroupID(AURORA_GID_MISC_WORLD));
	_KeCredSetCap(pid, 0);                       /* a service needs no credential powers */
	if (_KeProcessLoadExec(pid, s->path, argc, argv) == -1) {
		char line[120];
		sprintf(line, "[svcmgr] %s: could not load %s\n", s->name, s->path);
		emit(line);
		return -1;
	}
	s->pid = pid;
	s->starts++;
	char line[160];
	sprintf(line, "[svcmgr] started %s pid=%d (start #%u%s%s)\n", s->name, pid, s->starts,
			arg[0] ? ", test hook: " : "", arg[0] ? arg : "");
	emit(line);
	return pid;
}

static void service_loop(Service* s) {
	char line[200];
	int existing = find_running(s->name);
	if (existing > 0) {
		s->pid = existing;
		s->adopted = true;
		g_adopted_any = 1;
		sprintf(line, "[svcmgr] adopted running %s pid=%d\n", s->name, existing);
		emit(line);
	} else if (start_service(s) < 0) {
		sprintf(line, "[svcmgr] %s: first start failed\n", s->name);
		emit(line);
	}
	s->window_start = _KeGetCurrentMS();
	s->cooldown_cur_ms = s->cooldown_ms;
	uint64_t down_at = 0;

	for (;;) {
		uint64_t up_since = _KeGetCurrentMS();
		if (s->pid > 0)
			_KeProcessWaitForTermination(s->pid);     /* blocks until it exits or crashes */
		uint64_t now = _KeGetCurrentMS();
		sprintf(line, "[svcmgr] %s (pid %d) is gone after %u ms\n", s->name, s->pid, (unsigned)(now - up_since));
		emit(line);
		sprintf(line, "[svcmgr] >>> %s is DOWN, the rest of the system keeps running; supervisor is recovering it\n", s->name);
		emit(line);
		down_at = now;

		if (now - up_since >= s->stable_ms) {       /* it ran long enough to be forgiven */
			s->crashes = 0;
			s->cooldown_cur_ms = s->cooldown_ms;
		}
		if (now - s->window_start > s->window_ms) { /* old crashes age out of the budget */
			s->window_start = now;
			s->crashes = 0;
		}
		s->crashes++;

		if (s->crashes > s->max_restarts) {
			sprintf(line, "[svcmgr] %s crashed %u times in %u s: giving up for now\n",
					s->name, s->crashes, (unsigned)(s->window_ms / 1000));
			emit(line);
			if (s->handback[0]) {
				sprintf(line, "[svcmgr] >>> %s keeps failing: FALLING BACK to the in-kernel driver so the device stays usable\n", s->name);
				emit(line);
				char cmds[72];
				strncpy(cmds, s->handback, sizeof cmds - 1);
				cmds[sizeof cmds - 1] = 0;
				bool handback_ok = true;
				for (char* c = cmds; *c;) {
					char* end = c;
					while (*end && *end != ';')
						end++;
					char saved = *end;
					*end = 0;
					int rc = drvctl(c);
					if (rc != AU_UNLOAD_OK)
						handback_ok = false;
					sprintf(line, "[svcmgr] %s: drvctl '%s' -> %d\n", s->name, c, rc);
					emit(line);
					if (!saved)
						break;
					c = end + 1;
				}
				if (handback_ok) {
					sprintf(line, "[svcmgr] %s: handed back to the kernel\n", s->name);
					emit(line);
				} else {
					sprintf(line, "[svcmgr] %s: kernel handback FAILED; retrying service recovery\n", s->name);
					emit(line);
				}
			}
			sprintf(line, "[svcmgr] %s: probation in %u ms\n", s->name, (unsigned)s->cooldown_cur_ms);
			emit(line);
			sleep_ms(s->cooldown_cur_ms);
			if (s->cooldown_cur_ms < MAX_COOLDOWN_MS)
				s->cooldown_cur_ms *= 2;               /* a repeat offender waits longer */
			s->crashes = s->max_restarts;              /* one strike left on probation */
			s->window_start = _KeGetCurrentMS();
			sprintf(line, "[svcmgr] %s: probation, trying it again\n", s->name);
			emit(line);
		} else {
			unsigned delay = BACKOFF_BASE_MS << (s->crashes - 1);
			sprintf(line, "[svcmgr] >>> %s will RESTART automatically in %u ms (attempt %u of %u)\n",
					s->name, delay, s->crashes, s->max_restarts);
			emit(line);
			sleep_ms(delay);
		}
		if (start_service(s) < 0) {
			s->pid = -1;
			sprintf(line, "[svcmgr] >>> %s restart FAILED, retrying in 1000 ms\n", s->name);
			emit(line);
			sleep_ms(1000);
		} else {
			sprintf(line, "[svcmgr] >>> %s is BACK (pid %d), service restored after %u ms of downtime\n",
					s->name, s->pid, (unsigned)(_KeGetCurrentMS() - down_at));
			emit(line);
		}
	}
}

/* one watcher thread per service; the entry point takes no argument */
static void svc_thread_0() { sleep_ms(1);   service_loop(&g_svc[0]); }
static void svc_thread_1() { sleep_ms(300); service_loop(&g_svc[1]); }
static void svc_thread_2() { sleep_ms(600); service_loop(&g_svc[2]); }
static void svc_thread_3() { sleep_ms(900); service_loop(&g_svc[3]); }
static void svc_thread_4() { sleep_ms(1200); service_loop(&g_svc[4]); }
static void svc_thread_5() { sleep_ms(1500); service_loop(&g_svc[5]); }
static void svc_thread_6() { sleep_ms(1800); service_loop(&g_svc[6]); }
static void svc_thread_7() { sleep_ms(2100); service_loop(&g_svc[7]); }
static void (*const g_entry[MAX_SERVICES])() = {
	svc_thread_0, svc_thread_1, svc_thread_2, svc_thread_3,
	svc_thread_4, svc_thread_5, svc_thread_6, svc_thread_7
};

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;
	load_config();
	char line[200];
	sprintf(line, "[svcmgr] up with %d service(s), test mode '%s'\n", g_nsvc, g_mode);
	emit(line);
	for (int i = 0; i < g_nsvc; i++) {
		sprintf(line, "[svcmgr]   %s %s: restarts %u in %us, cooldown %us, stable %us, handback '%s'\n",
				g_svc[i].name, g_svc[i].path, g_svc[i].max_restarts, (unsigned)(g_svc[i].window_ms / 1000),
				(unsigned)(g_svc[i].cooldown_ms / 1000), (unsigned)(g_svc[i].stable_ms / 1000), g_svc[i].handback);
		emit(line);
	}
	for (int i = 0; i < g_nsvc; i++)
		_KeCreateThread(g_entry[i], g_svc[i].name);

	/* the main thread only keeps the process alive */
	uint64_t t0 = _KeGetCurrentMS();
	for (;;) {
		sleep_ms(1000);
#ifdef __XENEVA_SVC_TEST__
		/* test hook: the supervisor itself dies once, init must bring it back and the
		 * new instance must ADOPT the running services (it does not crash if it did) */
		if (!strcmp(g_mode, "selfcrash") && !g_adopted_any && _KeGetCurrentMS() - t0 > 12000) {
			emit("[svcmgr] test hook: the supervisor crashes on purpose\n");
			*(volatile int*)0 = 1;
		}
#else
		(void)t0;
#endif
	}
	return 0;
}
