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

#ifndef __SIGNAL_H__
#define __SIGNAL_H__

#include <stdint.h>
#include <Hal/AA64/sched.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif

typedef enum _signal_num_ {
	SIGHUP = 1,  /* Terminal closed or hangup*/
	SIGINT,      /* Interrupt (CTRL+C) */
	SIGQUIT,     /* Quit (CTRL+), generate core dump */
	SIGILL,      /* Illegal instruction*/
	SIGTRAP,     /* Breakpoint or trace trap */
	SIGABRT,     /* Abort a process */
	SIGBUS,      /* Bus error */
	SIGFPE,      /* Floating-point exception */
	SIGKILL,     /* Forcefully terminate */
	SIGUSR1,     /* User-defined signal 1*/
	SIGSEGV,     /* Invalid memory access (segmentation fault) */
	SIGUSR2,     /* user-defined signal 2*/
	SIGPIPE,     /* Write to a broken pipe */
	SIGALRM,     /* Alarm timer expired */
	SIGTERM,     /* Graceful termination request */
	SIGCHLD = 17,  /*Child process stopped or exited */
	SIGCONT,     /* Continue a stopped process */
	SIGSTOP,     /* Stop process */
	SIGTSTP,     /* Terminal stop */
	SIGTTIN,     /* Background process attempted terminal input */
	SIGTTOU,     /* Background process attempted terminal output */
}AuSignalNumber;

typedef void(*AuSignalHandler)(int signum);
/**
 * @brief AuAllocSignal -- allocate a signal onto the
 * signal queue of thread
 * @param thread -- Pointer to destination thread
 * @param sigNum -- signal number
 */
extern int AuAllocSignal(AA64Thread* thread, int sigNum);


/**
 * @brief AuSignalDeliver -- deliver the current thread's
 * signal
 * @param current_thread -- pointer to thread
 */
extern bool AuSignalDeliver(AA64Thread* current_thread);

/**
 * @brief AuSignalDispatch -- dispatch a signal
 * @param thr -- pointer to thread
 */
extern int AuSignalDispatch(AA64Thread* thr);

/**
 * @brief AuSignalInitializeTrampoline -- installs
 * return trampoline code to threads sig return address
 * @param t -- pointer to thread struct
 */
extern void AuSignalInitializeTrampoline(AA64Thread* t);

#endif
