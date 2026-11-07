/**
* @file proctoken.h
*
* BSD 2-Clause License
*
* Copyright (c) 2026, Manas Kamal Choudhury
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

#include <proctoken.h>
#include <_null.h>
#include <aucon.h>
#include <process.h>
#include <Hal/AA64/sched.h>
#include <Drivers/uart.h>

AuProcessToken _tokens[AURORA_MAX_PROC_TOKEN];

/**
 * @brief AuProcessTokenInitialize -- initialize token manager
 */
void AuProcessTokenInitialize() {
	for (int i = 0; i < AURORA_MAX_PROC_TOKEN; i++) {
		_tokens[i].proc = NULL;
		_tokens[i].thread_id = -1;
	}
	AuTextOut("[aurora]: process tokens initialized \r\n");
}

/**
 * @brief _token_id_to_strong --return the string value
 */
char* _token_id_to_string(uint8_t category) {
	switch (category) {
	case PROCESS_TOKEN_NETWORK:
		return "PROCESS_TOKEN_NETWORK";
	case PROCESS_TOKEN_DISPLAY:
		return "PROCESS_TOKEN_DISPLAY";
	case PROCESS_TOKEN_AUDIO:
		return "PROCESS_TOKEN_AUDIO";
	case PROCESS_TOKEN_DEV:
		return "PROCESS_TOKEN_DEV";
	case PROCESS_TOKEN_NETSERVER:
		return "PROCESS_TOKEN_NETSERVER";
	case PROCESS_TOKEN_LAUNCHER:
		return "PROCESS_TOKEN_LAUNCHER";
	default:
		return "unknown";
	}
}

/**
 * AuProcessTokenAddSelf -- add current process
 * to its belonging category
 * @param category -- category to look
 */
int AuProcessTokenAddSelf(uint8_t category) {
	if (_tokens[category].proc) {
		UARTDebugOut("[aurora]: token manager already has registered token to id : %s \r\n",
					 _token_id_to_string(category));
		return 1;
	}
	AA64Thread* current_thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(current_thr);
	if (!proc) {
		proc = AuProcessFindSubThread(current_thr);
		if (!proc)
			return 1;
	}
	_tokens[category].proc = proc;
	_tokens[category].thread_id = current_thr->thread_id;
	UARTDebugOut("[aurora]: process registered to token : %s \r\n", _token_id_to_string(category));
	if (category == PROCESS_TOKEN_DISPLAY) {
		/* The compositor draws into the same physical framebuffer the
		 * kernel console writes to. Mute further framebuffer text so boot
		 * and driver messages stop scribbling over the live desktop. UART
		 * logging continues uninterrupted. --axiss */
		AuConsoleSetDisplayOwned();
	}
	return 0;
}

/**
 * @brief AuProcessTokenGetProc -- get a process from
 * token entry
 * @param category -- category to look
 */
AuProcess* AuProcessTokenGetProc(uint8_t category) {
	return _tokens[category].proc;
}

/**
 * @brief AuProcessTokenGetThreadID -- get the thread id
 * of a slot
 */
int AuProcessTokenGetThreadID(uint8_t category) {
	return _tokens[category].thread_id;
}

/**
 * @brief AuProcessTokenRemoveSelf -- remove self from a token
 * @param category -- category to free
 */
int AuProcessTokenRemoveSelf(uint8_t category) {
	AA64Thread* current_thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(current_thr);
	if (!proc) {
		proc = AuProcessFindSubThread(current_thr);
		if (!proc)
			return 1;
	}
	_tokens[category].proc = NULL;
	_tokens[category].thread_id = -1;
	UARTDebugOut("[aurora]: process removed from token : %s \r\n", _token_id_to_string(category));
	return 0;
}
