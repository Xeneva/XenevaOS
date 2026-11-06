/*
 * Process/svcdummy/main.cpp -- a deliberately short-lived service, test only.
 *
 * svcmgr supervises it next to sndsrv to prove that services have independent
 * restart budgets and timers: it lives ~1.5 s and exits, so it burns through its
 * restart budget, goes on probation, and so on, while sndsrv is unaffected.
 * Only built and packed with SVCTEST set.
 */
#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;
	const char* line = "[svcdummy] alive, exiting in 1.5 s\n";
	int k = _KeOpenFile((char*)"/dev/kmsg", FILE_OPEN_WRITE);
	if (k >= 0) {
		_KeWriteFile(k, (void*)line, strlen(line));
		_KeCloseFile(k);
	}
	_KeProcessSleep(1500);
	return 3;
}
