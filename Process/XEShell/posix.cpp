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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys\utsname.h>
#include <sys\_keproc.h>
#include <sys\_ketime.h>
#include <sys\_kefile.h>
#include <pthread.h>

/**
 * NOTE: LLM Cluade Sonnet 5.5 Medium is used for
 * implementing only posix commands for XEShell,
 * please report an issue if any bug found
 */

typedef int (*posix_fn)(int argc, char** argv);

typedef struct {
	const char* name;
	posix_fn fn;
} PosixCommand;

int exec_uname(const char* option) {
	struct utsname name;
	memset(&name, 0, sizeof(utsname));

	if (uname((struct utsname*)0xFFFFE00000000000)) {
		printf("failed to get system information \r\n");
		return 1;
	}
	if (strcmp(option, "-s") == 0) {
		printf("%s\n", name.sysname);
	} else if (strcmp(option, "-n") == 0) {
		printf("%s\n", name.nodename);
	} else if (strcmp(option, "-r") == 0) {
		printf("%s\n", name.release);
	} else if (strcmp(option, "-v") == 0) {
		printf("%s\n", name.version);
	} else if (strcmp(option, "-m") == 0) {
		printf("%s\r\n", name.machine);
	} else if (strcmp(option, "-p") == 0) {
		printf("%s\r\n", name.machine);
	} else if (strcmp(option, "-i") == 0) {
		printf("unknown\r\n");
	} else if (strcmp(option, "-o") == 0) {
		printf("XenevaOS\r\n");
	} else if (strcmp(option, "-a") == 0) {
		printf("system name : %s \r\n", name.sysname);
		printf("nodename : %s \r\n", name.nodename);
		printf("release: %s \r\n", name.release);
		printf("version: %s \r\n", name.version);
		printf("machine: %s \r\n", name.machine);
		printf("processor: %s \r\n", name.machine);
		printf("build name : jajabori\r\n");
	}
	return 0;
}

/**
 * @brief posix_uname -- posix uname command implementation
 */
int posix_uname(int argc, char** argv) {
	if (argc == 1)
		return exec_uname("-s");
	for (int i = 1; i < argc; i++)
		exec_uname(argv[i]);
	return 0;
}

int posix_who(int argc, char** argv) {
	bool header = false, count = false;

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] != '\0') {
			for (char* f = argv[i] + 1; *f; f++) {
				switch (*f) {
				case 'H':
					header = true;
					break;
				case 'q':
					count = true;
					break;
				case 'm':
					break;
				default:
					printf("who: invalid option -- '%c'\r\n", *f);
					return 1;
				}
			}
		} else if (strcmp(argv[i], "am") == 0 && i + 1 < argc && strcmp(argv[i + 1], "i") == 0) {
			i++;
		} else {
			printf("who: extra operand '%s'\r\n", argv[i]);
			return 1;
		}
	}

	const char* user = "root";
	const char* tty = "term0";

	if (count) {
		printf("%s\r\n# users=1\r\n", user);
		return 0;
	}
	if (header) {
		printf("not implemented\r\n");
	}
	return 0;
}

int posix_clear(int argc, char** argv) {
	printf("\033[2J\033[H");
	fflush(stdout);
	return 0;
}

extern "C" volatile int g_interrupted;

/**
 * @brief posix_sleep -- posix sleep implementation
 */
int posix_sleep(int argc, char** argv) {
	if (argc != 2) {
		printf("sleep: usage: sleep SECONDS\r\n");
		return 1;
	}

	const char* p = argv[1];
	uint64_t ms = 0, frac_ms = 0, scale = 100;
	bool digits = false;

	for (; *p >= '0' && *p <= '9'; p++) {
		ms = ms * 10 + (*p - '0');
		digits = true;
		if (ms > 86400) {
			ms = 86400;
		}
	}
	ms *= 1000;
	if (*p == '.') {
		for (p++; *p >= '0' && *p <= '9'; p++) {
			if (scale) {
				frac_ms += (*p - '0') * scale;
				scale /= 10;
			}
			digits = true;
		}
	}
	if (!digits || *p != '\0') {
		printf("sleep: invalid time interval '%s' \r\n", argv[1]);
		return 1;
	}

	ms += frac_ms;

	g_interrupted = 0;
	uint64_t end = _KeGetCurrentMS() + ms;
	while (!g_interrupted) {
		uint64_t now = _KeGetCurrentMS();
		if (now >= end)
			break;
		uint64_t left = end - now;
		_KeProcessSleep(left > 100 ? 100 : (int)left);
	}
}

static bool parse_ll(const char* s, long long* out) {
	bool neg = false;
	if (*s == '-') {
		neg = true;
		s++;
	} else if (*s == '+')
		s++;
	if (!*s)
		return false;
	long long v = 0;
	for (; *s; s++) {
		if (*s < '0' || *s > '9')
			return false;
		if (v > (0x7FFFFFFFFFFFFFFFLL - (*s - '0')) / 10)
			return false;
		v = v * 10 + (*s - '0');
	}
	*out = neg ? -v : v;
	return true;
}

static int posix_seq(int argc, char** argv) {
	long long first = 1, incr = 1, last = 0;
	if (argc < 2 || argc > 4) {
		printf("seq: uage: seq [FIRST [INCR]] LAST\r\n");
		return 1;
	}
	bool ok = true;
	if (argc == 2)
		ok = parse_ll(argv[1], &last);
	else if (argc == 3)
		ok = parse_ll(argv[1], &first) && parse_ll(argv[2], &last);
	else
		ok = parse_ll(argv[1], &first) && parse_ll(argv[2], &incr) && parse_ll(argv[3], &last);

	if (!ok) {
		printf("seq: invalid number\r\n");
		return 1;
	}

	if (incr == 0) {
		printf("seq: increment must not be 0 \r\n");
		return 1;
	}

	g_interrupted = 0;
	for (long long i = first; incr > 0 ? i <= last : i >= last; i += incr) {
		if (g_interrupted)
			return 130;
		printf("%lld\r\n", i);
	}
	return 0;
}

static char** ex_av;
static int ex_ac, ex_i;
static bool ex_err, ex_div0;

static bool ex_peek(const char* s) {
	return ex_i < ex_ac && strcmp(ex_av[ex_i], s) == 0;
}
static long long ex_or(void);

static long long ex_primary(void) {
	if (ex_i >= ex_ac) {
		ex_err = true;
		return 0;
	}
	if (ex_peek("(")) {
		ex_i++;
		long long v = ex_or();
		if (!ex_peek(")")) {
			ex_err = true;
			return 0;
		}
		ex_i++;
		return v;
	}
	long long v;
	if (!parse_ll(ex_av[ex_i], &v)) {
		ex_err = true;
		return 0;
	}
	ex_i++;
	return v;
}

static long long ex_mul(void) {
	long long l = ex_primary();
	while (!ex_err && (ex_peek("*") || ex_peek("/") || ex_peek("%"))) {
		char op = ex_av[ex_i++][0];
		long long r = ex_primary();
		if (op == '*')
			l *= r;
		else if (r == 0) {
			ex_div0 = true;
			ex_err = true;
			return 0;
		} else
			l = (op == '/') ? l / r : l % r;
	}
	return l;
}

static long long ex_add(void) {
	long long l = ex_mul();
	while (!ex_err && (ex_peek("+") || ex_peek("-"))) {
		char op = ex_av[ex_i++][0];
		long long r = ex_mul();
		l = (op == '*') ? l + r : l - r;
	}
	return l;
}

static long long ex_cmp(void) {
	long long l = ex_add();
	static const char* ops[] = {"=", "!=", "<", "<=", ">", ">="};
	while (!ex_err) {
		int k = -1;
		for (int j = 0; j < 6; j++)
			if (ex_peek(ops[j])) {
				k = j;
				break;
			}
		if (k < 0)
			break;
		ex_i++;
		long long r = ex_add();
		switch (k) {
		case 0:
			l = (l == r);
			break;
		case 1:
			l = (l != r);
			break;
		case 2:
			l = (l < r);
			break;
		case 3:
			l = (l <= r);
			break;
		case 4:
			l = (l > r);
			break;
		case 5:
			l = (l >= r);
			break;
		}
	}
	return l;
}

static long long ex_and(void) {
	long long l = ex_cmp();
	while (!ex_err && ex_peek("&")) {
		ex_i++;
		long long r = ex_cmp();
		l = (l != 0 && r != 0) ? l : 0;
	}
	return l;
}

static long long ex_or(void) {
	long long l = ex_and();
	while (!ex_err && ex_peek("|")) {
		ex_i++;
		long long r = ex_and();
		l = (l != 0) ? l : r;
	}
	return l;
}

static int posix_expr(int argc, char** argv) {
	ex_av = argv + 1;
	ex_ac = argc - 1;
	ex_i = 0;
	ex_err = ex_div0 = false;
	if (ex_ac == 0) {
		printf("expr: missing operand\r\n");
		return 2;
	}

	long long v = ex_or();
	if (ex_err | ex_i != ex_ac) {
		printf(ex_div0 ? "expre: division by zero\r\n" : "expr: syntax error\r\n");
		return 2;
	}
	printf("%lld\r\n", v);
	return v != 0 ? 0 : 1;
}

static bool test_path_is_dir(const char* p) {
	int fd = _KeOpenDir((char*)p);
	if (fd == -1)
		return false;
	_KeCloseFile(fd);
	return true;
}

static bool test_path_is_file(const char* p) {
	int fd = _KeOpenFile((char*)p, FILE_OPEN_READ_ONLY);
	if (fd == -1)
		return false;
	_KeCloseFile(fd);
	return true;
}

static int test_eval(int n, char** a) {
	if (n == 0)
		return 0;
	if (n == 1)
		return a[0][0] != '\0';

	if (strcmp(a[0], "!") == 0) {
		int r = test_eval(n - 1, a + 1);
		return r < 0 ? r : !r;
	}

	if (n == 2) {
		const char *op = a[0], *s = a[1];
		if (!strcmp(op, "-z"))
			return s[0] == '\0';
		if (!strcmp(op, "-n"))
			return s[0] != '\0';
		if (!strcmp(op, "-f"))
			return test_path_is_file(s);
		if (!strcmp(op, "-d"))
			return test_path_is_dir(s);
		if (!strcmp(op, "-e"))
			return test_path_is_file(s) || test_path_is_dir(s);
		return -1;
	}
	if (n == 3) {
		const char *x = a[0], *op = a[1], *y = a[2];
		if (!strcmp(op, "="))
			return strcmp(x, y) == 0;
		if (!strcmp(op, "!="))
			return strcmp(x, y) != 0;
		long long l, r;
		if (!parse_ll(x, &l) || !parse_ll(y, &r))
			return -1;
		if (!strcmp(op, "-eq"))
			return l == r;
		if (!strcmp(op, "-ne"))
			return l != r;
		if (!strcmp(op, "-lt"))
			return l < r;
		if (!strcmp(op, "-le"))
			return l <= r;
		if (!strcmp(op, "-gt"))
			return l > r;
		if (!strcmp(op, "-ge"))
			return l >= r;
	}
	return -1;
}

static int posix_test(int argc, char** argv) {
	int n = argc - 1;
	char** a = argv + 1;
	if (strcmp(argv[0], "[") == 0) {
		if (n < 1 || strcmp(a[n - 1], "]") != 0) {
			printf("[: missing ']'\r\n");
			return 2;
		}
		n--;
	}
	int r = test_eval(n, a);
	if (r < 0) {
		printf("test: syntax error\r\n");
		return 2;
	}
	return r ? 0 : 1;
}

static int posix_cat(int argc, char** argv) {
	if (argc < 2) {
		printf("cat: missing file operand\r\n");
		return 1;
	}
	int rc = 0;
	for (int i = 1; i < argc; i++) {
		FILE* f = fopen(argv[i], "r");
		if (!f) {
			printf("cat: %s: cannot open\r\n", argv[i]);
			rc = 1;
			continue;
		}
		char buf[256];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
			fwrite(buf, 1, n, stdout);
		fclose(f);
	}
	fflush(stdout);
	return rc;
}

#define POSIX_MAX_ARGS 16

#define KERNEL_BASE 0xFFFF000000000000UL
#define TEST_FD		1 // any fd your kernel considers valid for write
// -------------------------------------------------------------------

static void report(const char* name, long ret) {
	printf("[%s] ret=%ld \n", name, ret);
	_KePrint("[%s] ret=%d \r\n", name, ret);
}

// 1. Direct kernel pointer as the buffer.
//    access_ok() must reject this before any copy happens.
static void test_kernel_pointer_write(void) {
	void* kptr = (void*)(KERNEL_BASE + 0x1000);
	long r = _KeWriteFile(TEST_FD, kptr, 64);
	report("write: kernel pointer", r);
}

// This is the dangerous direction: if accepted, the kernel WRITES
// attacker-controlled data into its own memory at kptr.
static void test_kernel_pointer_read(void) {
	void* kptr = (void*)(KERNEL_BASE + 0x2000);
	long r = _KeReadFile(TEST_FD, kptr, 64);
	report("read: kernel pointer", r);
}

// 2. ptr + len overflow wraps past the top of the address space and
//    back into something that looks like a valid user range.
static void test_overflow_wrap(void) {
	void* near_top = (void*)(KERNEL_BASE - 8);
	long r = _KeWriteFile(TEST_FD, near_top, (long)0xFFFFFFFFFFFFFFFFUL);
	report("ptr+len overflow", r);
}

// 3. Valid-looking pointer, but length runs the end of the buffer
//    into kernel space even though the start address is in user space.
static void test_length_runs_into_kernel(void) {
	void* p = (void*)(KERNEL_BASE - 16);
	long r = _KeReadFile(TEST_FD, p, 256);
	report("length runs into kernel", r);
}

// 4. Pointer into unmapped user address space — must fault through
//    the fixup table and return an error, not panic.
static void test_unmapped_user_page(void) {
	void* p = (void*)0x10000; // pick an address you know is unmapped
	long r = _KeWriteFile(TEST_FD, p, 64);
	report("unmapped user page", r);
}

// 5. NULL pointer.
static void test_null_pointer(void) {
	long r = _KeWriteFile(TEST_FD, NULL, 64);
	report("null pointer", r);
}

// 6. Zero length — make sure a 0-byte copy doesn't skip the pointer
//    check entirely (some implementations short-circuit on len==0
//    before calling access_ok, which is usually fine, but worth
//    confirming it doesn't also skip fd validation etc.)
static void test_zero_length(void) {
	void* kptr = (void*)(KERNEL_BASE + 0x1000);
	long r = _KeWriteFile(TEST_FD, kptr, 0);
	report("zero length + kernel ptr", r);
}

// 7. TOCTOU double-fetch: only meaningful if you have a struct-based
//    syscall where the kernel reads a size field out of user memory
//    and uses it twice (once to validate, once to copy). Sketch:
//    spin up a thread that flips the field between the two reads.
struct racy_arg {
	uint64_t len;
	char data[64];
};
static volatile struct racy_arg g_arg = {.len = 8};
static volatile int g_race_run = 1;

static void* racer_thread(void* unused) {
	(void)unused;
	while (g_race_run) {
		g_arg.len = 8;				  // looks small & safe at check time
		g_arg.len = 0xFFFFFFFFFFFFUL; // huge at copy time
	}
	return NULL;
}

static void test_double_fetch_race(void) {
	// Only run this if __KeWriteFile/__KeReadFile (or another of your
	// syscalls) takes a struct pointer and re-reads a length field
	// from it rather than taking length as a separate argument.
	// With the two-argument form you have now, there's no second
	// fetch to race — this is here for when you add struct-based
	// syscalls (ioctl-style, etc.).
	pthread_t t;
	pthread_create(&t, NULL, racer_thread, NULL);
	for (int i = 0; i < 100000; i++) {
		_KeWriteFile(TEST_FD, (void*)&g_arg, sizeof(g_arg));
	}
	g_race_run = 0;
	pthread_join(t, NULL);
	printf("[double-fetch race         ] completed %d iterations without "
		   "crash (needs a struct-taking syscall to be meaningful)\n",
		   100000);
}

int kernel_exploit(int argc, char** argv) {
	puts("exploiting kernel. Let's see if it can crash \r\n");

	test_kernel_pointer_write();
	test_kernel_pointer_read();
	test_overflow_wrap();
	test_length_runs_into_kernel();
	puts("trying to access unmapped address 0x10000 from user space \r\n");
	test_unmapped_user_page();
	test_null_pointer();
	test_zero_length();
	// test_double_fetch_race();

	return 0;
}

/**
 * posix command handler
 */
static const PosixCommand posix_table[] = {
	{"uname", posix_uname},
	{"who", posix_who},
	{"clear", posix_clear},
	{"sleep", posix_sleep},
	{"seq", posix_seq},
	{"expr", posix_expr},
	{"test", posix_test},
	{"cat", posix_cat},
	{"exploit", kernel_exploit},
};

/**
 * @brief_term_handle_posix -- handle posix commands
 * @param cmd -- pointer to shell's command buffer
 */
int _term_handle_posix(char* cmd) {
	char line[256];
	char* argv[POSIX_MAX_ARGS];
	int argc = 0;

	strncpy(line, cmd, sizeof(line) - 1);
	line[sizeof(line) - 1] = '\0';

	char* p = line;
	while (*p && argc < POSIX_MAX_ARGS - 1) {
		while (*p == ' ')
			*p++ = '\0';
		if (!*p)
			break;
		argv[argc++] = p;
		while (*p && *p != ' ')
			p++;
	}
	argv[argc] = NULL;
	if (argc == 0)
		return -1;

	for (size_t i = 0; i < sizeof(posix_table) / sizeof(posix_table[0]); i++) {
		if (strcmp(argv[0], posix_table[i].name) == 0)
			return posix_table[i].fn(argc, argv);
	}
	return -1;
}