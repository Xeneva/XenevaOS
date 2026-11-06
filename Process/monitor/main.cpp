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

static int num_process;
XEProcessList* list;

static const char* CpuColor(uint32_t cpu_x10) {
	if (cpu_x10 >= 700)
		return CLR_RED; /* >= 70.0% */
	else if (cpu_x10 >= 300)
		return CLR_YELLOW; /* >= 30.0% */
	else
		return CLR_GREEN;
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
	fflush(stdout);
	printf(CLR_YELLOW "%-6d " CLR_RESET, p->proc_id);
	printf(CLR_WHITE "%-16s " CLR_RESET, p->name);
	printf(CLR_WHITE "%-10u " CLR_RESET, p->num_file_opened);
	printf("%s%3u.%u%%" CLR_RESET CLR_EOL "\r\n", CpuColor(p->cpu_usage), whole, frac);
}

static int RenderFrame(int prev_row_count) {
	if (num_process <= 0) {
		printf(CUR_HOME CLR_RED "No process information available." CLR_EOL "\n" CLR_RESET);
		return 0;
	}

	if (!list) {
		printf(CUR_HOME CLR_RED "Failed to allocate process list buffer." CLR_EOL "\n" CLR_RESET);
		return prev_row_count;
	}

	memset(list, 0, num_process * sizeof(XEProcessList));
	if (_KeProcessFetch(list, num_process) != 0) {
		printf(CUR_HOME CLR_RED "Failed to fetch process information." CLR_EOL "\n" CLR_RESET);
		free(list);
		return prev_row_count;
	}

	printf(CUR_HOME);
	PrintHeader();
	for (int i = 0; i < num_process; i++) {
		PrintProcessRow(&list[i]);
	}

	for (int i = num_process; i < prev_row_count; i++) {
		printf(CLR_EOL "\n");
	}

	return num_process;
}

void SignalHandler(int signo) {
	_KePrint("Monitor received signal \r\n");
	_KeProcessExit();
}

int DisplayProcessListLive() {
	printf(CLR_SCREEN CUR_HOME CUR_HIDE); /* one-time full clear at startup */

	int prev_row_count = 0;
	num_process = _KeGetNumProcessCount();
	list = (XEProcessList*)malloc(sizeof(XEProcessList) * num_process);

	for (;;) {
		int new_proc_count = _KeGetNumProcessCount();
		if (new_proc_count != num_process) {
			num_process = new_proc_count;
			list = (XEProcessList*)realloc(list, sizeof(XEProcessList) * num_process);
		}
		prev_row_count = RenderFrame(prev_row_count);
		fflush(stdout);
		SLEEP_1_SECOND();
	}
	return 0; /* unreachable */
}

int main() {
	signal(SIGINT, SignalHandler);
	signal(SIGKILL, SignalHandler);
	DisplayProcessListLive();
}