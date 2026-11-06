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

/**
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */

#ifndef XE_SCRIPT_H
#define XE_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Run a script file. argv[0] is the script name, argv[1..] are $1.. / %1..
 * Returns the script's exit status (127 if the file can't be opened). */
int XsRunFile(const char* path, int argc, char** argv);

/* Run script text that is already in memory. */
int XsRunText(const char* text, int len, bool bat, int argc, char** argv);

/* true if the name ends in .sh (*isBat=false) or .bat/.cmd (*isBat=true) */
bool XsScriptKind(const char* name, bool* isBat);

/* Bumped by xesh's SIGINT handler, polled by every loop. */
extern volatile int g_interrupted;

/* ------------------------------------------------------------------ */
/* Host hooks, implemented by xesh                                     */
/* ------------------------------------------------------------------ */

/* Run a shell builtin or external program. fd < 0 means "inherit the
 * shell's own stdin/stdout/stderr". Returns the exit status (127 = not found). */
int XsHostRun(int argc, char** argv, int fdIn, int fdOut, int fdErr);

const char* XsHostCwd(void);

/* mode: 0 = read, 1 = write (create + TRUNCATE), 2 = append. Returns fd or -1. */
int XsHostOpen(const char* path, int mode);
void XsHostClose(int fd);
int XsHostRead(int fd, void* buf, int n);
int XsHostWrite(int fd, const void* buf, int n);

/* 1 if the path exists; *isDir set when non-NULL */
int XsHostExists(const char* path, int* isDir);
void XsHostRemove(const char* path); /* may be a no-op */

/* Console input. ReadLine echoes and returns the length (-1 on EOF).
 * ReadKey returns ONE raw byte with no echo and no line buffering (-1 on EOF). */
int XsHostReadLine(char* buf, int max);
int XsHostReadKey(void);

void XsHostSetEnv(const char* name, const char* value);
void XsHostSleepMs(int ms);
void XsHostWinSize(int* cols, int* rows); /* leave untouched when unknown */

#ifdef __cplusplus
}
#endif
#endif
