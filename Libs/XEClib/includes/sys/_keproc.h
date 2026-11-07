/**
* @file _keproc.h
* 
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

#ifndef __KE_PROC_H__
#define __KE_PROC_H__

#include <stdint.h>
#include <_xeneva.h>
#include <sys/_kesignal.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	/**
	 * process token definitions
	 */
	enum _proc_tokens_ {
		PROCESS_TOKEN_NETWORK,
		PROCESS_TOKEN_DISPLAY,
		PROCESS_TOKEN_AUDIO,
		PROCESS_TOKEN_DEV,
		PROCESS_TOKEN_NETSERVER,
		PROCESS_TOKEN_LAUNCHER,
	};

	/**
 * _sys_proc_list -- a way to report current status
 * of information about all processes
 */
	typedef struct _sys_proc_list_ {
		int proc_id;
		char name[16];
		uint64_t total_runtime_us;
		uint64_t window_runtime_us;
		uint32_t num_threads;
		uint32_t num_file_opened;
		uint32_t cpu_usage;
	} XEProcessList;

	typedef void (*_XEThreadEntry)();

	/**
	 * @brief _KePauseThread -- pause currently running
	 * thread
	 */
	XE_LIB int _KePauseThread();

	/**
	 * @brief _KeGetThreadID -- get currently running
	 * thread id
	 */
	XE_LIB uint64_t _KeGetThreadID();

	/**
	 * @brief _KeGetProcessID -- get currently running
	 *  process id
	 */
	XE_LIB int _KeGetProcessID();

	/**
	 * @brief _KeProcessExit -- exits current process
	 */
	XE_LIB int _KeProcessExit();

	/**
	 * @brief _KeProcessWaitForTermination -- suspends
	 * currently running process until another
	 * process state changes
	 * @param pid -- process id,-1 for all
	 */
	XE_LIB int _KeProcessWaitForTermination(int pid);

	/**
	 * @brief _KeCreateProcess -- create a new process
	 * slot by taking
	 * @param parent_id -- process parent id
	 * @param name -- process name
	 * @return -- process id of newly create process
	 * slot
	 */
	XE_LIB int _KeCreateProcess(int parent_id, char* name);

	/**
	 * @brief _KeProcessLoadExec -- loads an executable to a
	 * process slot
	 * @param proc_id -- process slot id
	 * @param filename -- filename of the process
	 * @param argc -- number of arguments
	 * @param argv -- argument array to pass
	 *
	 */
	XE_LIB int _KeProcessLoadExec(int proc_id, char* filename, int argc, char** argv);

	/**
	* @brief _KeGetProcessHeapMem -- Grabs some memory from
	* heap memory of current process slot
	* @param sz -- size that is needed, should be page-aligned
	*/
	XE_LIB uint64_t _KeGetProcessHeapMem(size_t sz);

	/**
	 * @brief _KeProcessSleep -- put the current process main thread
	 *  to sleep mode
	 *  1s = 10000 ms
	 * @param ms -- millisecond to sleep
	 */
	XE_LIB int _KeProcessSleep(uint64_t ms);

	/**
	 * @brief _KeSetSignal -- register a new signal handler for this
	 * process
	 * @param signo -- signal number
	 * @param handler -- handler function
	 */
	XE_LIB int _KeSetSignal(int signo, XESigHandler handler);

	/**
	 * @brief _KeGetSystemTimerTick -- returns the current system
	 * timer tick
	 */
	XE_LIB size_t _KeGetSystemTimerTick();

	/**
	 * @brief _KeCreateThread -- creates a new thread
	 * inside current process slot
	 */
	XE_LIB int _KeCreateThread(void (*entry)(), char* name);

	/**
	 * @brief _KeCreateThreadArg -- creates a new thread with one
	 * argument delivered in x0 of the entry point
	 */
	XE_LIB int _KeCreateThreadArg(void (*entry)(), char* name, void* arg);

	/**
	 * @brief _KeFutex -- native futex wait/wake/requeue
	 * @param uaddr -- user address of the 32-bit futex word
	 * @param op -- futex command, optionally ORed with PRIVATE_FLAG
	 * @param val -- expected value for wait, count for wake
	 * @param timeout -- relative timespec for wait operations, or NULL
	 * @param uaddr2 -- destination address for requeue
	 * @param val3 -- bitset or compare value
	 */
	XE_LIB int _KeFutex(uint32_t* uaddr, int op, uint32_t val, const void* timeout,
						uint32_t* uaddr2, uint32_t val3);

	/**
	* @brief _KeSetFileToProcess -- copies a file from one process
	* to other
	* @param fileno -- file number of the current process
	* @param dest_fdidx -- destination process file index
	* @param proc_id -- destination process id
	*/
	XE_LIB int _KeSetFileToProcess(int fileno, int dest_fidx, int proc_id);

	/**
	 * @brief _KeSendSignal -- send a signal to desired process
	 * @param pid -- thread id
	 * @param signum -- signal number
	 */
	XE_LIB int _KeSendSignal(int pid, int signum);

	/**
	 * @brief _KeSendSignalToThread -- send a signal to desired
	 * thread
	 * @param tid -- thread id
	 * @param signum -- signal number
	 */
	XE_LIB int _KeSendSignalToThread(int tid, int signum);

	/**
	 * 2brief _KeProcessGetFileDesc -- returns file descriptor for
	 * given filename
	 * @param filename -- name of the file if it's already
	 * opened
	 */
	XE_LIB int _KeProcessGetFileDesc(const char* filename);

	/**
	 * @brief _KeGetEnvironmentBlock -- returns the current environment
	 * block of the process
	 */
	XE_LIB uint64_t _KeGetEnvironmentBlock();

	/**
     * _KeProcessTokenAddSelf -- add current process
     * to its belonging category
     * @param category -- category to look
     */
	XE_LIB int _KeProcessTokenAddSelf(uint8_t category);

	/**
     * @brief _KeProcessTokenGetThreadID -- get the thread id
     * of a slot
	 * @param category -- token category
     */
	XE_LIB int _KeProcessTokenGetThreadID(uint8_t category);

	/**
     * @brief _KeProcessTokenRemoveSelf -- remove self from a token
     * @param category -- category to free
     */
	XE_LIB int _KeProcessTokenRemoveSelf(uint8_t category);

	XE_LIB int _KeGetNumProcessCount();

	XE_LIB int _KeProcessFetch(XEProcessList * list, int num_proc_count);

	XE_LIB int _KeExitSubThread(int thread_id);

	XE_LIB int _KeThreadWaitForTermination(int thread_id);

#ifdef __cplusplus
}
#endif

#endif