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

#ifndef __KE_SIGNAL_H__
#define __KE_SIGNAL_H__

#ifdef ARCH_X64
#define SIGHUP		1
#define SIGINT		2
#define SIGQUIT		3
#define SIGILL		4
#define SIGTRAP		5
#define SIGABRT		6
#define SIGEMT		7
#define SIGFPE		8
#define SIGKILL		9
#define SIGBUS		10
#define SIGSEGV		11
#define SIGSYS		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGUSR1		16
#define SIGUSR2		17
#define SIGCHLD		18
#define SIGPWR		19
#define SIGWINCH	20
#define SIGURG		21
#define SIGPOLL		22
#define SIGSTOP		23
#define SIGTSTP		24
#define SIGCONT		25
#define SIGTTIN		26
#define SIGTTOUT	27
#define SIGVTALRM	28
#define SIGPROF		29
#define SIGXCPU		30
#define SIGXFSZ		31
#define SIGWAITING	32
#define SIGDIAF		33
#define SIGHATE		34
#define SIGWINEVENT 35
#define SIGCAT		36
#define SIGTTOU		37
#elif ARCH_ARM64
/** in arch64 decided to go with minimal number of calls */
#define SIGHUP	1  /* Terminal closed or hangup*/
#define SIGINT	2  /* Interrupt (CTRL+C) */
#define SIGQUIT 3  /* Quit (CTRL+), generate core dump */
#define SIGILL	4  /* Illegal instruction*/
#define SIGTRAP 5  /* Breakpoint or trace trap */
#define SIGABRT 6  /* Abort a process */
#define SIGBUS	7  /* Bus error */
#define SIGFPE	8  /* Floating-point exception */
#define SIGKILL 9  /* Forcefully terminate */
#define SIGUSR1 10 /* User-defined signal 1*/
#define SIGSEGV 11 /* Invalid memory access (segmentation fault) */
#define SIGUSR2 12 /* user-defined signal 2*/
#define SIGPIPE 13 /* Write to a broken pipe */
#define SIGALRM 14 /* Alarm timer expired */
#define SIGTERM 15 /* Graceful termination request */
#define SIGCHLD 17 /*Child process stopped or exited */
#define SIGCONT 18 /* Continue a stopped process */
#define SIGSTOP 19 /* Stop process */
#define SIGTSTP 20 /* Terminal stop */
#define SIGTTIN 21 /* Background process attempted terminal input */
#define SIGTTOU 22 /* Background process attempted terminal output */

#endif

#ifdef ARCH_X64
#define NUMSIGNALS 38
#elif ARCH_ARM64
#define NUMSIGNALS 23
#endif

#define NSIG NUMSIGNALS

typedef void (*XESigHandler)(int signum);

#endif