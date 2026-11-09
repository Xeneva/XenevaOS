/**
* @file main.cpp
*
* monitor -- live per core CPU usage and process table for the Terminal
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
* All rights reserved.
**/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/_keproc.h>
#include <signal.h>

#include <unistd.h>
#define SLEEP_1_SECOND() sleep(1)

#define CUR_HOME   "\x1b[H"
#define CLR_SCREEN "\x1b[2J"
#define CLR_EOL	   "\x1b[K"
#define CUR_HIDE   "\x1b[?25l"
#define CUR_SHOW   "\x1b[?25h"

#define CLR_RESET  "\x1b[0m"
#define CLR_BOLD   "\x1b[1m"
#define CLR_CYAN   "\x1b[36m"
#define CLR_YELLOW "\x1b[33m"
#define CLR_WHITE  "\x1b[37m"
#define CLR_BLUE   "\x1b[34m"
#define CLR_GREEN  "\x1b[32m"
#define CLR_RED	   "\x1b[31m"

#define MAX_CPUS  8
#define BAR_WIDTH 25

static int num_process;
XEProcessList* list;

/* per core counters, previous and current sample */
static XECpuStats cpu_prev[MAX_CPUS];
static XECpuStats cpu_now[MAX_CPUS];
static int cpu_prev_count;

static const char* CpuColor(uint32_t cpu_x10) {
	if (cpu_x10 >= 700)
		return CLR_RED; /* >= 70.0% */
	else if (cpu_x10 >= 300)
		return CLR_YELLOW; /* >= 30.0% */
	else
		return CLR_GREEN;
}

/* usage in tenths of a percent from two samples of a busy/idle pair */
static uint32_t UsageX10(uint64_t busy, uint64_t idle) {
	uint64_t total = busy + idle;
	if (!total)
		return 0;
	return (uint32_t)((busy * 1000) / total);
}

static void PrintPercent(uint32_t x10) {
	printf("%s%3u.%u%%" CLR_RESET, CpuColor(x10), x10 / 10, x10 % 10);
}

static void PrintBar(uint32_t x10) {
	char bar[BAR_WIDTH + 1];
	int filled = (int)((x10 * BAR_WIDTH + 500) / 1000);
	if (filled > BAR_WIDTH)
		filled = BAR_WIDTH;
	for (int i = 0; i < BAR_WIDTH; i++)
		bar[i] = (i < filled) ? '|' : '.';
	bar[BAR_WIDTH] = '\0';
	printf(CLR_WHITE "[" CLR_RESET "%s%s" CLR_RESET CLR_WHITE "]" CLR_RESET, CpuColor(x10), bar);
}

static const XECpuStats* FindPrev(uint32_t cpu) {
	for (int i = 0; i < cpu_prev_count; i++)
		if (cpu_prev[i].cpu == cpu)
			return &cpu_prev[i];
	return NULL;
}

/**
 * RenderCpus -- one line per core plus a total, usage over the last
 * sample interval (since the core started on the first frame)
 * @return number of lines printed
 */
static int RenderCpus() {
	int n = _KeGetCPUStats(cpu_now, MAX_CPUS);
	if (n <= 0) {
		printf(CLR_RED "CPU statistics unavailable." CLR_EOL "\n" CLR_RESET);
		return 1;
	}

	printf(CLR_BOLD CLR_CYAN "%-4s %-27s %-7s %-10s %-8s" CLR_EOL "\n" CLR_RESET,
		   "CPU",
		   "USAGE",
		   "BUSY",
		   "SWITCH/s",
		   "RUNNING");

	uint64_t all_busy = 0, all_idle = 0;
	int online = 0;
	for (int i = 0; i < n; i++) {
		const XECpuStats* c = &cpu_now[i];
		const XECpuStats* p = FindPrev(c->cpu);
		uint64_t busy = c->busy_ticks - (p ? p->busy_ticks : 0);
		uint64_t idle = c->idle_ticks - (p ? p->idle_ticks : 0);
		uint64_t sw = c->context_switches - (p ? p->context_switches : 0);
		uint64_t elapsed = busy + idle;
		uint32_t sw_rate = (elapsed && c->counter_freq) ? (uint32_t)((sw * c->counter_freq) / elapsed) : 0;
		uint32_t x10 = UsageX10(busy, idle);

		printf(CLR_YELLOW "%-4u " CLR_RESET, c->cpu);
		if (!c->online) {
			printf(CLR_RED "offline" CLR_RESET CLR_EOL "\n");
			continue;
		}
		online++;
		all_busy += busy;
		all_idle += idle;
		PrintBar(x10);
		printf(" ");
		PrintPercent(x10);
		printf("  " CLR_WHITE "%-10u %-8s" CLR_RESET CLR_EOL "\n", sw_rate, c->current);
	}

	uint32_t all_x10 = UsageX10(all_busy, all_idle);
	printf(CLR_BOLD "%-4s " CLR_RESET, "ALL");
	PrintBar(all_x10);
	printf(" ");
	PrintPercent(all_x10);
	printf("  " CLR_WHITE "%d core(s) online" CLR_RESET CLR_EOL "\n", online);

	memcpy(cpu_prev, cpu_now, sizeof(XECpuStats) * n);
	cpu_prev_count = n;
	return n + 2;
}

static void PrintHeader() {
	printf(CLR_BOLD CLR_CYAN "%-6s %-16s %-10s %-8s" CLR_EOL "\n" CLR_RESET,
		   "PID",
		   "NAME",
		   "FILES",
		   "CPU%");
	printf(CLR_CYAN "%-6s %-16s %-10s %-8s" CLR_EOL "\n" CLR_RESET,
		   "------",
		   "----------------",
		   "----------",
		   "--------");
}

static void PrintProcessRow(XEProcessList* p) {
	unsigned int whole = p->cpu_usage / 10;
	unsigned int frac = p->cpu_usage % 10;
	printf(CLR_YELLOW "%-6d " CLR_RESET, p->proc_id);
	printf(CLR_WHITE "%-16s " CLR_RESET, p->name);
	printf(CLR_WHITE "%-10u " CLR_RESET, p->num_file_opened);
	printf("%s%3u.%u%%" CLR_RESET CLR_EOL "\r\n", CpuColor(p->cpu_usage), whole, frac);
}

static int RenderProcesses(int prev_row_count) {
	if (num_process <= 0) {
		printf(CLR_RED "No process information available." CLR_EOL "\n" CLR_RESET);
		return 0;
	}

	if (!list) {
		printf(CLR_RED "Failed to allocate process list buffer." CLR_EOL "\n" CLR_RESET);
		return prev_row_count;
	}

	memset(list, 0, num_process * sizeof(XEProcessList));
	if (_KeProcessFetch(list, num_process) != 0) {
		/* keep the buffer, the next frame retries */
		printf(CLR_RED "Failed to fetch process information." CLR_EOL "\n" CLR_RESET);
		return prev_row_count;
	}

	PrintHeader();
	for (int i = 0; i < num_process; i++) {
		PrintProcessRow(&list[i]);
	}

	/* wipe rows left over from a longer previous frame */
	for (int i = num_process; i < prev_row_count; i++) {
		printf(CLR_EOL "\n");
	}

	return num_process;
}

void SignalHandler(int signo) {
	(void)signo;
	printf(CLR_RESET CUR_SHOW "\n");
	fflush(stdout);
	_KePrint("Monitor received signal \r\n");
	_KeProcessExit();
}

int DisplayLive() {
	printf(CLR_SCREEN CUR_HOME CUR_HIDE); /* one-time full clear at startup */

	int prev_row_count = 0;
	num_process = _KeGetNumProcessCount();
	if (num_process > 0)
		list = (XEProcessList*)malloc(sizeof(XEProcessList) * num_process);

	for (;;) {
		int new_proc_count = _KeGetNumProcessCount();
		if (new_proc_count != num_process && new_proc_count > 0) {
			XEProcessList* grown =
				(XEProcessList*)realloc(list, sizeof(XEProcessList) * new_proc_count);
			if (grown) {
				list = grown;
				num_process = new_proc_count;
			}
		}
		printf(CUR_HOME);
		RenderCpus();
		printf(CLR_EOL "\n");
		prev_row_count = RenderProcesses(prev_row_count);
		fflush(stdout);
		SLEEP_1_SECOND();
	}
	return 0; /* unreachable */
}

int main() {
	signal(SIGINT, SignalHandler);
	signal(SIGKILL, SignalHandler);
	DisplayLive();
}
