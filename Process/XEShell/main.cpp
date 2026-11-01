/**
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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

#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/_ketime.h>
#include <sys/_kesignal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <keycode.h>
#include <unistd.h>
#include "xe_script.h"

/**
 * NOTE: Please do note that, some functions have been generated using LLM
 * LLM generated functions have been marked in each comment description
 */

char* cmdBuf;
int index;
bool _process_needed;
bool _draw_shell_curdir;
bool _spawnable_process;
int job;
bool _sig_handled = false;
char* currentDirectory;
char* lastDirectory;
int g_console_fd = -1;

/* True when name already carries suffix.  Keeps "netsock.exe" from
 * turning into "/netsock.exe.exe" while still expanding bare "netsock". */
static bool xe_name_has_suffix(const char* name, const char* suffix) {
	size_t name_len = strlen(name);
	size_t suffix_len = strlen(suffix);

	return name_len >= suffix_len &&
		   strcmp(name + name_len - suffix_len, suffix) == 0;
}

void XEShellSigInterrupt(int signo) {
	g_interrupted = 1;
	/* Handle SIGINT (CTRL+C) properly */
	printf("\n[xeshell]: Received signal %d (SIGINT - CTRL+C)\r\n", signo);
	_draw_shell_curdir = true;
	if (job > 0) {
		int result = _KeSendSignal(job, signo);
		if (result == 0) {
			printf("[xeshell]: Signal sent to process %d\r\n", job);
		} else {
			printf("[xeshell]: Failed to send signal to process %d (error: %d)\r\n", job, result);
		}
		job = 0;
	}
	fflush(stdout);
}

volatile int g_interrupted = 0;

void XEShellSignalTest(int signo) {
	g_interrupted++;
	//printf("[xeshell]: Signal %d received (SIGINT - CTRL+C)\r\n", signo);
	fflush(stdout);

	if (job > 0) {
		printf("[xeshell]: Sending signal to process %d\r\n", job);
		fflush(stdout);
		_KeSendSignal(job, signo);
		job = 0;
	}

	//printf("[xeshell]: Signal handler completed\r\n");
	fflush(stdout);
}

int timercount;
// Timer callback disabled - not available in XenevaOS
void XEShellTimerCallback(int signo) {
	// Not implemented in XenevaOS
}

/*Write the current directory string
 * for now, only root directory is
 * current directory '/'
 */
void XEShellWriteCurrentDir() {
	if (_draw_shell_curdir) {
		printf("\033[32mXEShell %s$:\033[37m\033]133;B\007",
			   currentDirectory ? currentDirectory : "/");
		fflush(stdout);
		_draw_shell_curdir = false;
	}
}

void XEShellSpawn(char* string) {
	if ((strlen(string)) > 0) {
		/* allocate separate memories for each strings */
		char filename[128];
		char arguments[128];
		char execname[32];
		memset(arguments, 0, sizeof(arguments));
		memset(execname, 0, sizeof(execname));

		/* _first_string_skipped is only for executable
		 * name thats why we skip early character counting */
		bool _first_string_skipped = false;
		int argcount = 0;
		int j = 0;
		/* iptables-style cmds need >9 trailing args; match Init's headroom */
		const int max_args = 32;
		char** argv = (char**)malloc(max_args * sizeof(char*));
		memset(argv, 0, max_args * sizeof(char*));

		/* Validate input string length */
		if (strlen(string) > 127) {
			printf("\n[xeshell]: Command too long (max 127 characters)\r\n");
			return;
		}

		/* here we mainly prepare for the arguments to pass */
		for (int i = 0; i < strlen(string) + 1; i++) {
			if (string[i] == ' ' || string[i] == '\0') {
				if (_first_string_skipped && arguments[0] != '\0') {
					if (argcount >= max_args) {
						printf("\n[xeshell]: Too many arguments (max %d)\r\n", max_args);
						for (int k = 0; k < argcount; k++)
							free(argv[k]);
						free(argv);
						return;
					}
					char* str = (char*)malloc(strlen(arguments) + 1);
					memset(str, 0, strlen(arguments) + 1);
					strcpy(str, arguments);
					argv[argcount] = str;
					argcount += 1;
				}
				j = 0;
				memset(arguments, 0, sizeof(arguments));

				if (!_first_string_skipped)
					_first_string_skipped = true;

				continue;
			}
			if (_first_string_skipped) {
				if (j < (int)sizeof(arguments) - 1) {
					arguments[j] = string[i];
					j++;
				}
			} else {
				if (i < (int)sizeof(execname) - 1)
					execname[i] = string[i];
			}
		}

		memset(filename, 0, sizeof(filename));
		strcpy(filename, currentDirectory);
		if (strlen(currentDirectory) != 1 && currentDirectory[strlen(currentDirectory) - 1] != '/')
			strcat(filename, "/");

		strcat(filename, execname);
		/* A musl guest is named *.elf. Everything else is still a PE .exe,
		 * unless the caller already typed the suffix. */
		if (!xe_name_has_suffix(execname, ".exe") &&
			!xe_name_has_suffix(execname, ".elf"))
			strcat(filename, ".exe");

		/* before spawning the process, make an entry to
		 * shell's file descriptors */
		int file = _KeOpenFile(filename, FILE_OPEN_READ_ONLY);
		if (file == -1) {
			printf("\n[xeshell]: No command or program found: %s\r\n", filename);
			return;
		}

		int proc_id = _KeCreateProcess(0, string);
		if (proc_id == -1) {
			printf("\n[xeshell]: Failed to create process for: %s\r\n", string);
			_KeCloseFile(file);
			return;
		}

		/* Set file descriptors for the process */
		if (_KeSetFileToProcess(XENEVA_STDIN, XENEVA_STDIN, proc_id) != 0) {
			printf("\n[xeshell]: Failed to set stdin for process %d\r\n", proc_id);
			_KeCloseFile(file);
			return;
		}
		if (_KeSetFileToProcess(XENEVA_STDOUT, XENEVA_STDOUT, proc_id) != 0) {
			printf("\n[xeshell]: Failed to set stdout for process %d\r\n", proc_id);
			_KeCloseFile(file);
			return;
		}
		if (_KeSetFileToProcess(XENEVA_STDERR, XENEVA_STDERR, proc_id) != 0) {
			printf("\n[xeshell]: Failed to set stderr for process %d\r\n", proc_id);
			_KeCloseFile(file);
			return;
		}

		int status = _KeProcessLoadExec(proc_id, filename, argcount, argv);
		if (status != 0) {
			printf("\n[xeshell]: Failed to load executable %s (error: %d)\r\n", filename, status);
			_KeCloseFile(file);
			/* Free allocated memory */
			for (int k = 0; k < argcount; k++) {
				free(argv[k]);
			}
			free(argv);
			return;
		}

		job = proc_id;
		printf("\n[xeshell]: Started process %d for: %s\r\n", proc_id, string);
		_KeProcessWaitForTermination(proc_id);
		printf("\n[xeshell]: Process %d terminated\r\n", proc_id);
		_KeCloseFile(file);

		/* Free allocated memory */
		for (int k = 0; k < argcount; k++) {
			free(argv[k]);
		}
		free(argv);
		printf("\n");
		job = 0;
	}
}

/*
 * XEShellReadLine -- reads a line until it gets
 * and end-of-line or new line character
 */
void XEShellReadLine() {
	// Only process if we have a command to process
	// Note: We don't check _process_needed here - we always try to read
	// The flag is used in the main loop to know when to process the command

	// Don't clear buffer if we're in the middle of reading
	if (index == 0) {
		// Clear the command buffer only at start of new command
		memset(cmdBuf, 0, 1024);
	}

	// Use stdin provided by init. Init already opened /dev/console
	// and set it as XENEVA_STDIN for the shell.
	int read_fd = g_console_fd;

	// Read one character from console (blocks in kernel until a key arrives)
	char buf[2];
	memset(buf, 0, 2);
	int bytes_read = _KeReadFile(read_fd, buf, 1);
	if (bytes_read <= 0) {
		// No data (should be rare since kernel blocks); just return
		// and let the main loop sleep briefly before retrying.
		cmdBuf[index] = '\0';
		return;
	}

	char c = buf[0];

	/* Ctrl+C -- copy the line being typed. Ctrl+V is handled by the kernel
	 * console: it types the clipboard into stdin, so no shell code sees it. */
	if (c == 3) {
		int clip_fd = _KeOpenFile((char*)"/dev/clipboard", FILE_OPEN_WRITE);
		if (clip_fd >= 0) {
			int copied = 0;
			if (index > 0) {
				_KeWriteFile(clip_fd, (void*)cmdBuf, index);
				copied = index;
			}
			_KeCloseFile(clip_fd);
			printf("\r\n[xeshell]: copied %d bytes\r\n", copied);
			/* redraw prompt and the text still in the buffer */
			XEShellWriteCurrentDir();
			printf("%s", cmdBuf);
			fflush(stdout);
		}
		return;
	}

	if (c == '\n' || c == '\r') {
		// End of line - process the command
		printf("\n");
		fflush(stdout);
		_process_needed = true;
		return;
	}

	if (c > 0) {
		// Check bounds before writing to buffer
		if (index >= 1023) {
			printf("\n[xeshell]: Command too long (max 1023 characters)\r\n");
			fflush(stdout);
			// Clear the buffer
			memset(cmdBuf, 0, 1024);
			index = 0;
			_process_needed = true;
			return;
		}

		if (c == KEY_BACKSPACE) {
			if (index > 0) {
				printf("%c", c);
				fflush(stdout);
				cmdBuf[--index] = '\0';
			}
			return;
		}

		if (c == KEY_SPACE) {
			printf("%c", c);
			fflush(stdout);
			cmdBuf[index] = ' ';
			index++;
			return;
		}

		// Only print printable characters
		if (c >= 32 && c <= 126) {
			printf("%c", c);
			fflush(stdout);
			cmdBuf[index++] = c;
		}
	}

	// Null-terminate the current buffer
	cmdBuf[index] = '\0';
	return;
}

/*
 * XEShellCD -- Change working Directory command
 * @param path -- path to change (supported only 
 * relative path)
 * absolute path changing not implemented
 */
void XEShellCD(char* path) {
	if (!currentDirectory || !path) {
		printf("\n[xeshell]: Invalid path or current directory\r\n");
		return;
	}

	char* newPath = NULL;
	int prevPathLen = strlen(currentDirectory);
	/* navigate back to parent directory */
	if ((strcmp(path, "..") == 0)) {
		if (prevPathLen == 1 && (strcmp(currentDirectory, "/") == 0)) {
			printf("\n[xeshell]: Already at root directory\r\n");
			return;
		}

		// Find the last '/' and truncate
		int last_slash = -1;
		for (int i = prevPathLen - 1; i >= 0; i--) {
			if (currentDirectory[i] == '/') {
				last_slash = i;
				break;
			}
		}

		if (last_slash != 0) {
			currentDirectory[last_slash] = '\0';
		} else {
			strcpy(currentDirectory, "/");
		}

		/* last check if the current directory became totally \0 */

		printf("\n[xeshell]: Changed to parent directory: %s \r\n", currentDirectory);
		// Update environment variable
		_XESetEnvironmentVariable("PWD", currentDirectory, 1);
		printf("[xeshell]: PWD environment variable updated to: %s\r\n", currentDirectory);
		return;
	}

	/** handle large relative path */
	if (strncmp(path, "../", 3) == 0) {
		int i = 0;
		int pathLen = strlen(path);
		int maxLen = 4096;
		while (i < pathLen) {
			while (i < pathLen && path[i] == '/')
				i++;
			if (i >= pathLen)
				break;

			int start = i;
			while (i < pathLen && path[i] != '/')
				i++;
			int compLen = i - start;
			if (compLen == 1 && path[start] == '.') {
				continue;
			} else if (compLen == 2 && path[start] == '.' && path[start + 1] == '.') {
				int len = strlen(currentDirectory);
				if (len > 1) {
					int lastSlash = -1;
					for (int j = len - 1; j >= 0; j--) {
						if (currentDirectory[j] == '/') {
							lastSlash = j;
							break;
						}
					}
					if (lastSlash == 0)
						currentDirectory[1] = '\0';
					else if (lastSlash > 0)
						currentDirectory[lastSlash] = '\0';
				}
			} else {
				int len = strlen(currentDirectory);
				int isRoot = (len == 1 && currentDirectory[0] == '/');
				int needed = len + (isRoot ? 0 : 1) + compLen + 1;

				if (needed > maxLen)
					return;

				if (!isRoot)
					currentDirectory[len++] = '/';
				memcpy(currentDirectory + len, path + start, compLen);
				currentDirectory[len + compLen] = '\0';
			}
		}
		return;
	}

	/*
	 * nothing to do, this simple switch to current directory
	 */
	if (strcmp(path, "./") == 0) {
		printf("\n[xeshell]: Already in current directory\r\n");
		return;
	}

	/*
	 * This should be the home directory that is being set on
	 * environment variable or Xeneva configured home directory
	 * but due to no implementation of environment variable
	 * passing, "~" this simple navigates back to root directory 
	 */
	if (strcmp(path, "~") == 0) {
		free(currentDirectory);
		currentDirectory = (char*)malloc(2);
		strcpy(currentDirectory, "/");
		printf("\n[xeshell]: Changed to home directory: %s\r\n", currentDirectory);
		// Update environment variable
		_XESetEnvironmentVariable("PWD", currentDirectory, 1);
		printf("[xeshell]: PWD environment variable updated to: %s\r\n", currentDirectory);
		return;
	}

	// Validate path length
	if (strlen(path) > 127) {
		printf("\n[xeshell]: Path too long (max 127 characters)\r\n");
		return;
	}

	// Check if the path exists
	char testPath[256];
	if (currentDirectory[prevPathLen - 1] == '/' && prevPathLen > 1) {
		snprintf(testPath, sizeof(testPath), "%s%s", currentDirectory, path);
	} else {
		snprintf(testPath, sizeof(testPath), "%s/%s", currentDirectory, path);
	}

	int testDirfd = _KeOpenDir(testPath);
	if (testDirfd == -1) {
		printf("\n[xeshell]: Directory not found: %s\r\n", testPath);
		return;
	}
	_KeCloseFile(testDirfd);

	// Construct new path
	if (currentDirectory[prevPathLen - 1] == '/' && prevPathLen > 1) {
		newPath = (char*)malloc(prevPathLen + strlen(path) + 1);
		strcpy(newPath, currentDirectory);
		strcat(newPath, path);
	} else {
		newPath = (char*)malloc(prevPathLen + strlen(path) + 2);
		strcpy(newPath, currentDirectory);
		// check if the currentDirectory is only "/" which means root
		if (strlen(currentDirectory) != 1 && currentDirectory[0] != '/')
			strcat(newPath, "/");

		while (*path == '/')
			path++;

		size_t len = strlen(path);
		while (len > 0 && path[len - 1] == '/') {
			len--;
		}
		path[len] = '\0';

		if (currentDirectory[strlen(currentDirectory) - 1] != '/')
			strcat(newPath, "/");

		strcat(newPath, path);
	}

	free(currentDirectory);
	currentDirectory = newPath;

	// Remove trailing slash if not root
	if (strlen(currentDirectory) > 1 && currentDirectory[strlen(currentDirectory) - 1] == '/') {
		currentDirectory[strlen(currentDirectory) - 1] = '\0';
	}

	printf("\n[xeshell]: Changed directory to: %s\r\n", currentDirectory);

	// Update environment variable
	_XESetEnvironmentVariable("PWD", currentDirectory, 1);
	printf("[xeshell]: PWD environment variable updated to: %s\r\n", currentDirectory);
}

/*
 * XEShellLS -- list the files inside current directory
 */
void XEShellLS() {
	int dirfd = _KeOpenDir(currentDirectory);
	if (dirfd == -1) {
		printf("\n[xeshell]: Failed to open directory: %s\r\n", currentDirectory);
		return;
	}

	XEDirectoryEntry* dirent = (XEDirectoryEntry*)malloc(sizeof(XEDirectoryEntry));
	if (!dirent) {
		printf("\n[xeshell]: Memory allocation failed for directory entry\r\n");
		_KeCloseFile(dirfd);
		return;
	}
	memset(dirent, 0, sizeof(XEDirectoryEntry));

	int file_count = 0;
	printf("\n[xeshell]: Contents of %s:\r\n", currentDirectory);

	while (1) {
		memset(dirent->filename, 0, 32);
		int code = _KeReadDir(dirfd, dirent);
		if (code == -1) {
			break;
		}
		if (dirent->index == -1) {
			break;
		}

		file_count++;
		if (dirent->flags & FILE_DIRECTORY) {
			printf("\033[36m%s/\033[0m\r\n", dirent->filename);
		} else {
			printf("%s\r\n", dirent->filename);
		}
	}

	printf("\n[xeshell]: %d items found\r\n", file_count);
	_KeCloseFile(dirfd);
	free(dirent);
}

/*
 * XEShellClipCommand -- copy text into the system clipboard, or show it
 *
 * `clip <text>` copies, bare `clip` reports. Paste is Ctrl+V, handled by the
 * kernel console, which types the clipboard into whoever holds stdin -- so it
 * works in any console program, not just this shell.
 */
void XEShellClipCommand() {
	if (strncmp(cmdBuf, "clip ", 5) == 0 && cmdBuf[5] != '\0') {
		int fd = _KeOpenFile((char*)"/dev/clipboard", FILE_OPEN_WRITE);
		if (fd < 0) {
			printf("\n[xeshell]: /dev/clipboard unavailable\n");
			return;
		}
		int n = (int)_KeWriteFile(fd, (void*)(cmdBuf + 5), strlen(cmdBuf + 5));
		_KeCloseFile(fd);
		printf("\n[xeshell]: copied %d bytes\n", n);
		return;
	}
	int fd = _KeOpenFile((char*)"/dev/clipboard", FILE_OPEN_READ_ONLY);
	if (fd < 0) {
		printf("\n[xeshell]: /dev/clipboard unavailable\n");
		return;
	}
	char buf[128];
	memset(buf, 0, sizeof buf);
	int n = (int)_KeReadFile(fd, buf, sizeof buf - 1);
	_KeCloseFile(fd);
	if (n > 0)
		printf("\n[xeshell]: clipboard (%d): %s\n", n, buf);
	else
		printf("\n[xeshell]: clipboard empty\n");
}

void XEShellPrintHelp() {
	printf("\n");
	printf("                    Xeneva Shell v1.1 Help                  \n");
	printf(" Command     Description                                    \n");
	printf(" cd [path]   Change current working directory               \n");
	printf(" ls          List files and folders in current directory    \n");
	printf(" echo [text]  Display text or write to file (echo text>file)\n");
	printf(" pwd         Display current working directory             \n");
	printf(" clrscr      Clear entire terminal screen                   \n");
	printf(" help        Show this help message                        \n");
	printf(" systeminfo  Display system information                    \n");
	printf(" time        Display current time                           \n");
	printf(" modinfo     Display DCL module status (/dev/dcl)           \n");
	printf(" rngtest     Read entropy from /dev/hwrng                   \n");
	printf(" rngguess    Guess the number virtio-rng picked             \n");
	printf(" clip [text] Copy text to clipboard, or show it             \n");
	printf(" exit        Exit the shell                                 \n");
	printf(" clear       Clear the terminal screen                      \n");
	printf(" uname       Display system information                     \n");
	printf(" who         Display current user information               \n");
	printf(" sleep       Suspend shell for specified number seconds     \n");
	printf(" seq         Generate a sequence of number                  \n");
	printf(" expr        Evaluates expressions                          \n");
	printf(" test        Evaluates conditional expressions              \n");
	printf(" neofetch    Display system information with XenevaOS logo  \n");
	printf("\n");
	printf("Examples:\n");
	printf("  cd /usr           - Change to /usr directory\n");
	printf("  cd ..             - Go to parent directory\n");
	printf("  echo Hello World   - Display Hello World\n");
	printf("  echo Test>file.txt - Write 'Test' to file.txt\n");
	printf("  ls -              - List current directory\n");
	printf("\n");
}

/*
 * XEShellPrintWorkingDirectory -- displays the current working
 * directory
 */
void XEShellPrintWorkingDirectory() {
	printf("\n[xeshell]: Current working directory: %s\r\n", currentDirectory);
}

/*
 * XEShellEcho -- outputs texts
 * @param msg -- text to output
 */
void XEShellEcho(char* msg) {
	if (!msg) {
		printf("\n[xeshell]: Missing message for echo command\r\n");
		return;
	}

	printf("\n");

	// Handle output redirection
	char* filename = strchr(msg, '>');
	if (filename) {
		filename++; // Skip '>'

		// Skip whitespace after '>'
		while (*filename == ' ') {
			filename++;
		}

		if (*filename == '\0') {
			printf("[xeshell]: Missing filename after '>'\r\n");
			return;
		}

		// Extract filename (until space or end of string)
		char file[128];
		int i = 0;
		while (filename[i] != ' ' && filename[i] != '\0' && i < 127) {
			file[i] = filename[i];
			i++;
		}
		file[i] = '\0';

		// Extract message text (before '>')
		char* msg_end = msg;
		while (*msg_end != '>' && *msg_end != '\0') {
			msg_end++;
		}
		*msg_end = '\0'; // Terminate message at '>'

		// Trim leading whitespace from message
		while (*msg == ' ') {
			msg++;
		}

		if (*msg == '\0') {
			printf("[xeshell]: No message to write to file\r\n");
			return;
		}

		// Write to file
		FILE* f = fopen(file, "w+");
		if (f) {
			fprintf(f, "%s", msg);
			fclose(f);
			printf("[xeshell]: Text written to file: %s\r\n", file);
		} else {
			printf("[xeshell]: Failed to write to file: %s\r\n", file);
		}
		return;
	}

	// Simple echo - just print the message
	printf("%s\r\n", msg);
}

/*
 * XEShellRngReadLine -- read one input line straight from the console
 * for the rngguess game. Same key handling as XEShellReadLine (echo,
 * backspace, enter) but with a private buffer, so the main command
 * buffer and index state are untouched while the game is prompting.
 * The kernel blocks in _KeReadFile until a key arrives.
 */
void XEShellRngReadLine(char* buf, int max) {
	int len = 0;
	buf[0] = '\0';
	while (1) {
		char b[2];
		memset(b, 0, 2);
		int n = (int)_KeReadFile(g_console_fd, b, 1);
		if (n <= 0)
			continue;
		char c = b[0];
		if (c == '\n' || c == '\r') {
			printf("\n");
			fflush(stdout);
			return;
		}
		if (c == KEY_BACKSPACE) {
			if (len > 0) {
				printf("%c", c);
				fflush(stdout);
				buf[--len] = '\0';
			}
			continue;
		}
		if (c >= 32 && c <= 126 && len < max - 1) {
			printf("%c", c);
			fflush(stdout);
			buf[len++] = c;
			buf[len] = '\0';
		}
	}
}

/*
 * XEShellModInfo -- print the DCL module loader status read from
 * the /dev/dcl device node
 */
void XEShellModInfo() {
	int dclfd = _KeOpenFile("/dev/dcl", FILE_OPEN_READ_ONLY);
	if (dclfd < 0) {
		printf("[xeshell]: /dev/dcl not available\r\n");
		return;
	}
	char dclbuf[512];
	memset(dclbuf, 0, sizeof(dclbuf));
	size_t dclread = _KeReadFile(dclfd, dclbuf, sizeof(dclbuf) - 1);
	_KeCloseFile(dclfd);
	if (dclread > 0)
		printf("%s", dclbuf);
	else
		printf("[xeshell]: empty DCL status\r\n");
}

/*
 * XEShellRngTest -- pull two 16-byte samples from virtio-rng via
 * /dev/hwrng and check the RNG actually varies between reads
 */
void XEShellRngTest() {
	int rngfd = _KeOpenFile("/dev/hwrng", FILE_OPEN_READ_ONLY);
	if (rngfd < 0) {
		printf("[xeshell]: /dev/hwrng not available\r\n");
		return;
	}
	static const char hexd[] = "0123456789abcdef";
	unsigned char s1[16], s2[16];
	memset(s1, 0, sizeof(s1));
	memset(s2, 0, sizeof(s2));
	size_t r1 = _KeReadFile(rngfd, s1, sizeof(s1));
	size_t r2 = _KeReadFile(rngfd, s2, sizeof(s2));
	_KeCloseFile(rngfd);
	printf("[xeshell]: rng sample 1 (%d bytes):", (int)r1);
	for (size_t i = 0; i < r1 && i < sizeof(s1); i++)
		printf(" %c%c", hexd[s1[i] >> 4], hexd[s1[i] & 0xF]);
	printf("\r\n[xeshell]: rng sample 2 (%d bytes):", (int)r2);
	for (size_t i = 0; i < r2 && i < sizeof(s2); i++)
		printf(" %c%c", hexd[s2[i] >> 4], hexd[s2[i] & 0xF]);
	printf("\r\n");
	if (r1 == 0 || r2 == 0)
		printf("[xeshell]: rngtest FAILED - no entropy returned\r\n");
	else if (memcmp(s1, s2, sizeof(s1)) == 0)
		printf("[xeshell]: rngtest FAILED - both samples identical\r\n");
	else
		printf("[xeshell]: rngtest OK - samples differ, entropy is live\r\n");
}

/*
 * XEShellRngGuess -- number guessing game (rngguess): the secret is
 * drawn from the hardware RNG (/dev/hwrng -> virtio-rng loaded via
 * the DCL module loader), so every play uses fresh device entropy.
 */
void XEShellRngGuess() {
	int rngfd = _KeOpenFile("/dev/hwrng", FILE_OPEN_READ_ONLY);
	if (rngfd < 0) {
		printf("[xeshell]: /dev/hwrng not available\r\n");
		return;
	}
	unsigned char entropy = 0;
	size_t got = _KeReadFile(rngfd, &entropy, 1);
	_KeCloseFile(rngfd);
	if (got != 1) {
		printf("[xeshell]: no entropy from the device\r\n");
		return;
	}
	int secret = (entropy % 100) + 1;
	printf("\r\n[xeshell]: virtio-rng drew a number from 1 to 100\r\n");
	printf("[xeshell]: type 'quit' to give up\r\n");
	int attempts = 0;
	while (1) {
		printf("Guess (%d): ", attempts + 1);
		fflush(stdout);
		char line[32];
		XEShellRngReadLine(line, sizeof(line));
		if (line[0] == '\0')
			continue;
		if (strcmp(line, "quit") == 0) {
			printf("[xeshell]: gave up -- the number was %d\r\n", secret);
			return;
		}
		int val = 0, valid = 1;
		for (char* p = line; *p; p++) {
			if (*p < '0' || *p > '9') {
				valid = 0;
				break;
			}
			val = val * 10 + (*p - '0');
			if (val > 100) {
				valid = 0;
				break;
			}
		}
		if (!valid || val < 1) {
			printf("[xeshell]: enter a whole number from 1 to 100\r\n");
			continue;
		}
		attempts++;
		if (val == secret) {
			printf("[xeshell]: correct! guessed in %d %s\r\n",
				   attempts,
				   attempts == 1 ? "try" : "tries");
			return;
		}
		if (val > secret)
			printf("[xeshell]: too high\r\n");
		else
			printf("[xeshell]: too low\r\n");
	}
}
#define XS_PARENT_MAX_FD 64

/* NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium) */
const char* XsHostCwd(void) {
	return currentDirectory ? currentDirectory : "/";
}

/**
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */
void XsHostSetEnv(const char* name, const char* value) {
	_XESetEnvironmentVariable((char*)name, (char*)value, 0);
}

/**
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */
void XsHostSleepMs(int ms) {
	_KeProcessSleep(ms);
}

/**
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */
void XsHostWinSize(int* cols, int* rows) {
	/* Terminal does not export its size yet; keep the caller's defaults. */
	(void)cols;
	(void)rows;
}

/* NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium) */
int XsHostOpen(const char* path, int mode) {
	/* FILE_OPEN_WRITE is assumed to create + truncate (that is what the
         * clipboard path in this shell relies on). Append falls back to the
         * same call; scripts that need `>>` on an existing file should be
         * tested on your VFS. */
	int fd = _KeOpenFile((char*)path, mode == 0 ? FILE_OPEN_READ_ONLY : FILE_OPEN_WRITE);
	return fd < 0 ? -1 : fd;
}

/**
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */
void XsHostClose(int fd) {
	if (fd >= 0)
		_KeCloseFile(fd);
}

/**
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */
int XsHostRead(int fd, void* buf, int n) {
	return (int)_KeReadFile(fd, buf, n);
}

/**
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */
int XsHostWrite(int fd, const void* buf, int n) {
	return (int)_KeWriteFile(fd, (void*)buf, n);
}

/**
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */
int XsHostExists(const char* path, int* isDir) {
	int fd = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	if (fd < 0)
		return 0;
	_KeCloseFile(fd);
	if (isDir)
		*isDir = 0; /* no stat call available: directories are reported as files */
	return 1;
}

/**
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */
void XsHostRemove(const char* path) {
	/* No unlink in the userland API yet. Temp pipe/capture files are
         * truncated on next use, so leaving them behind is harmless. */
	(void)path;
}

/* NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)*/
int XsHostReadKey(void) {
	char c = 0;
	for (;;) {
		int n = (int)_KeReadFile(g_console_fd, &c, 1);
		if (n > 0 && c != 0) /* the terminal also writes a 0 byte for key-release events */
			return (unsigned char)c;
		if (g_interrupted)
			return -1;
		_KeProcessSleep(5);
	}
}

/*
 *NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */
int XsHostReadLine(char* buf, int max) {
	int n = 0;
	for (;;) {
		int c = XsHostReadKey();
		if (c < 0) {
			buf[n] = 0;
			return n ? n : -1;
		}
		if (c == '\n' || c == '\r') {
			printf("\n");
			fflush(stdout);
			break;
		}
		if (c == KEY_BACKSPACE) {
			if (n > 0) {
				n--;
				printf("%c", KEY_BACKSPACE);
				fflush(stdout);
			}
			continue;
		}
		if (c >= 32 && c <= 126 && n < max - 1) {
			buf[n++] = (char)c;
			printf("%c", c);
			fflush(stdout);
		}
	}
	buf[n] = 0;
	return n;
}

/* ---- running commands -------------------------------------------------- */
/* xesh's own commands (the ones XEShellProcessLine handles inline).
 * Returns 1 if handled. 
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */
static int XEShellRunInternal(int argc, char** argv) {
	const char* c = argv[0];
	if (!strcmp(c, "help")) {
		XEShellPrintHelp();
		return 1;
	}
	if (!strcmp(c, "ls")) {
		XEShellLS();
		return 1;
	}
	if (!strcmp(c, "clrscr")) {
		printf("\033[2J\033[H");
		return 1;
	}
	if (!strcmp(c, "cd")) {
		if (argc < 2)
			XEShellPrintWorkingDirectory();
		else
			XEShellCD(argv[1]);
		return 1;
	}
	if (!strcmp(c, "systeminfo")) {
		printf("Xeneva Shell v1.1\r\n");
		return 1;
	}
	if (!strcmp(c, "modinfo")) {
		XEShellModInfo();
		return 1;
	}
	if (!strcmp(c, "clip")) {
		XEShellClipCommand();
		return 1;
	}
#ifdef XS_HAS_NEOFETCH
	if (!strcmp(c, "neofetch")) {
		XEShellNeofetch();
		return 1;
	}
#endif
	return 0;
}

/* Run an internal command or an executable. fd < 0 = inherit the console.
 * Returns the exit status; 127 when nothing was found. 
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */
int XsHostRun(int argc, char** argv, int fdIn, int fdOut, int fdErr) {
	if (argc <= 0)
		return 0;

	/* Internal commands print with printf, so they can't be redirected
         * or captured -- that is a limitation of how they are written. */
	if (XEShellRunInternal(argc, argv))
		return 0;

	char filename[160];
	const char* exec = argv[0];
	size_t el = strlen(exec);
	memset(filename, 0, sizeof filename);
	strncpy(filename, currentDirectory ? currentDirectory : "/", 100);
	if (strlen(filename) != 1 && filename[strlen(filename) - 1] != '/')
		strcat(filename, "/");
	if (strlen(filename) + el + 5 >= sizeof filename) {
		printf("[xeshell]: command too long\r\n");
		return 126;
	}
	strcat(filename, exec);
	if (!xe_name_has_suffix(exec, ".exe") && !xe_name_has_suffix(exec, ".elf"))
		strcat(filename, ".exe");

	int probe = _KeOpenFile(filename, FILE_OPEN_READ_ONLY);
	if (probe < 0) {
		printf("[xeshell]: No command or program found: %s\r\n", exec);
		return 127;
	}
	_KeCloseFile(probe);

	char line[512];
	line[0] = 0;
	for (int i = 0; i < argc; i++) {
		if (strlen(line) + strlen(argv[i]) + 2 >= sizeof line)
			break;
		if (i)
			strcat(line, " ");
		strcat(line, argv[i]);
	}

	int pid = _KeCreateProcess(0, line);
	if (pid == -1) {
		printf("[xeshell]: Failed to create process for: %s\r\n", line);
		return 126;
	}

	/* parent fd -> child fd. Redirected streams hand the child our open
         * file; -1 means "same console as the shell". */
	int pin = fdIn >= 0 ? fdIn : XENEVA_STDIN;
	int pout = fdOut >= 0 ? fdOut : XENEVA_STDOUT;
	int perr = fdErr >= 0 ? fdErr : XENEVA_STDERR;
	if (_KeSetFileToProcess(pin, XENEVA_STDIN, pid) != 0 ||
		_KeSetFileToProcess(pout, XENEVA_STDOUT, pid) != 0 ||
		_KeSetFileToProcess(perr, XENEVA_STDERR, pid) != 0) {
		printf("[xeshell]: Failed to set up stdio for process %d\r\n", pid);
		return 126;
	}

	/* argv for LoadExec excludes the program name, like XEShellSpawn */
	int status = _KeProcessLoadExec(pid, filename, argc - 1, argv + 1);
	if (status != 0) {
		printf("[xeshell]: Failed to load %s (error: %d)\r\n", filename, status);
		return 126;
	}
	job = pid;
	_KeProcessWaitForTermination(pid);
	job = 0;
	return 0; /* the kernel API does not return the child's exit code */
}

/* ---- entry from XEShellProcessLine -------------------------------------- */
/* If the first word is a *.sh / *.bat / *.cmd file, run it. Returns 1 if the
 * line was a script invocation (and has been executed). 
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 * */
int XEShellTryScript(char* line) {
	/* Do not write NULs into the line until we know it is a script.
	 * XEShellSpawn still needs the original text ("ping 1.1.1.1"). */
	char first[128];
	char* p = line;
	int n = 0;
	while (*p == ' ')
		p++;
	while (*p && *p != ' ' && n < (int)sizeof(first) - 1)
		first[n++] = *p++;
	first[n] = '\0';
	if (n == 0)
		return 0;

	bool bat = false;
	if (!XsScriptKind(first, &bat))
		return 0;

	char* argv[32];
	int argc = 0;
	p = line;
	while (*p && argc < 31) {
		while (*p == ' ')
			p++;
		if (!*p)
			break;
		argv[argc++] = p;
		while (*p && *p != ' ')
			p++;
		if (*p)
			*p++ = 0;
	}
	argv[argc] = NULL;
	if (argc == 0)
		return 0;

	g_interrupted = 0;
	/* xesh reads keys one at a time and the terminal echoes them, so the
         * script starts on a fresh line. */
	int st = XsRunFile(argv[0], argc, argv);
	if (st != 0)
		printf("[xeshell]: script exited with status %d\r\n", st);
	return 1;
}

extern int _term_handle_posix(char* cmd);
extern void XEShellNeofetch();

/*
 *XEShellProcessLine -- processes a line by looking
 * the cmdBuf 
 */
void XEShellProcessLine() {
	if (_process_needed) {
		if (strlen(cmdBuf) == 0) {
			// Empty command, just show prompt again
			_draw_shell_curdir = true;
			goto cleanup;
		}

		if (strcmp(cmdBuf, "help") == 0) {
			XEShellPrintHelp();
			_spawnable_process = false;
		} else if (strcmp(cmdBuf, "systeminfo") == 0) {
			printf("\nXeneva Shell v1.0\n");
			printf("Copyright (C) Xeneva Private Limited 2023-2026\n");
			printf("Operating System : Xeneva v1.1 -Genuine copy\n");
			printf("Kernel Version: v1.1 \n");
#ifdef ARCH_X64
			printf("Platform: x86_64\n");
#elif ARCH_ARM64
			printf("Platform: ARMv8-A \n");
#endif
			printf("Terminal: Xeneva Terminal v1.0\n");
			printf("Window Manager: Deodhai Compositor\n");
			printf("Xeneva is made in Assam with Love \n");
			_spawnable_process = false;
		} else if (strcmp(cmdBuf, "neofetch") == 0) {
			XEShellNeofetch();
		} else if (strcmp(cmdBuf, "clrscr") == 0) {
			printf("\033[2J\033[H"); // Clear screen and move cursor to home
			printf("[xeshell]: Screen cleared\r\n");
			_spawnable_process = false;
		} else if (strcmp(cmdBuf, "time") == 0) {
			printf("\nCurrent time is : ");
			XETime time;
			memset(&time, 0, sizeof(XETime));
			if (_KeGetCurrentTime(&time) == 0) {
				uint8_t hour = time.hour;
				char* pmam = "AM";
				if (hour > 12) {
					hour -= 12;
					pmam = "PM";
				} else if (hour == 0) {
					hour = 12;
				} else if (hour == 12) {
					pmam = "PM";
				}
				printf("%d:%02d %s\r\n", hour, time.minute, pmam);
			} else {
				printf("[xeshell]: Failed to get current time\r\n");
			}
			_spawnable_process = false;
		} else if (strcmp(cmdBuf, "ls") == 0) {
			XEShellLS();
			_spawnable_process = false;
		} else if (strncmp(cmdBuf, "cd ", 3) == 0 || strcmp(cmdBuf, "cd") == 0) {
			char* path = cmdBuf + 2;
			if (*path == ' ') {
				while (*path == ' ')
					path++;
			}
			if (*path == '\0') {
				// No path provided, show current directory
				XEShellPrintWorkingDirectory();
			} else {
				XEShellCD(path);
			}
			printf("\n");
			_spawnable_process = false;
		} else if (strcmp(cmdBuf, "pwd") == 0) {
			XEShellPrintWorkingDirectory();
			_spawnable_process = false;
		} else if (strncmp(cmdBuf, "echo ", 5) == 0 || strcmp(cmdBuf, "echo") == 0) {
			char* msg = cmdBuf + 4;
			if (*msg == ' ') {
				while (*msg == ' ')
					msg++;
			}
			XEShellEcho(msg);
			_spawnable_process = false;
		} else if (strcmp(cmdBuf, "clip") == 0 || strncmp(cmdBuf, "clip ", 5) == 0) {
			XEShellClipCommand();
			_spawnable_process = false;
		} else if (strcmp(cmdBuf, "exit") == 0) {
			printf("[xeshell]: Exiting shell...\r\n");
			_KeProcessExit();
		} else if (strcmp(cmdBuf, "modinfo") == 0) {
			XEShellModInfo();
			_spawnable_process = false;
		} else if (strcmp(cmdBuf, "rngtest") == 0) {
			XEShellRngTest();
			_spawnable_process = false;
		} else if (strcmp(cmdBuf, "rngguess") == 0) {
			XEShellRngGuess();
			_spawnable_process = false;
		} else if (XEShellTryScript(cmdBuf)) {
			_spawnable_process = false;
		} else if (_spawnable_process) {
			if (_term_handle_posix(cmdBuf) < 0)
				XEShellSpawn(cmdBuf);
		}

	cleanup:
		memset(cmdBuf, 0, 1024);
		index = 0;
		_process_needed = false;
		_draw_shell_curdir = true;
		_spawnable_process = true;
	}
}

/*
* main -- terminal emulator
*/
int main(int argc, char* arv[]) {
#ifdef ARCH_ARM64
	printf("Xeneva Shell v1.1 (arm64 build)\n");
#elif ARCH_X64
	printf("Xeneva Shell v1.1 (x86_64 build) \n");
#endif

	fflush(stdout);

	// Initialize command buffer
	cmdBuf = (char*)malloc(1024);
	if (!cmdBuf) {
		printf("[xeshell]: Failed to allocate command buffer\r\n");
		return 1;
	}
	memset(cmdBuf, 0, 1024);

	// Initialize current directory
	currentDirectory = (char*)malloc(2);
	if (!currentDirectory) {
		printf("[xeshell]: Failed to allocate current directory buffer\r\n");
		free(cmdBuf);
		return 1;
	}
	strcpy(currentDirectory, "/");

	// Initialize shell state
	_process_needed = false;
	_spawnable_process = true;
	_draw_shell_curdir = true;
	_sig_handled = false;
	job = 0;
	index = 0;
	timercount = 0;
	g_console_fd = XENEVA_STDIN;

	// Set environment variable
	_XESetEnvironmentVariable("PWD", currentDirectory, 0);
	printf("[xeshell]: PWD environment variable initialized to: %s\r\n", currentDirectory);

	// Set up signal handling
	if (_KeSetSignal(SIGINT, XEShellSignalTest) != 0) {
		printf("[xeshell]: Warning: Failed to set up signal handler\r\n");
	}
	// Timer functionality disabled - not available in XenevaOS

	printf("[xeshell]:XEShell initialized successfully. Use PGUP/PGDN to scroll\r\n");
	fflush(stdout);

	// Timer test code disabled - not available in XenevaOS
#if 0
	// Unix/Linux timer functions not available in XenevaOS
	// This code is commented out
#endif

	while (1) {
		XEShellWriteCurrentDir();
		XEShellReadLine();
		if (_process_needed) {
			XEShellProcessLine();
			_process_needed = false;
			index = 0; // Reset index for next command
		}
		_KeProcessSleep(10); // Increased sleep for better responsiveness
	}
}