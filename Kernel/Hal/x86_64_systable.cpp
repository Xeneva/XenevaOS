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

#include <stdint.h>
#include <_null.h>
#include <aucon.h>
#include <Hal/x86_64_sched.h>
#include <Hal/x86_64_hal.h>
#include <loader.h>
#include <Mm/vmmngr.h>
#include <process.h>
#include <Hal/serial.h>
#include <Sync/mutex.h>
#include <Hal/x86_64_signal.h>
#include <Serv/sysserv.h>
#include <Serv/syscall.h>
#include <ftmngr.h>
#include <Fs/tty.h>
#include <Fs/pipe.h>
#include <Mm/mmap.h>
#include <Net/socket.h>
#include <Fs/vdisk.h>

/* Syscall function format */
typedef int64_t(*syscall_func) (int64_t param1, int64_t param2, int64_t param3, int64_t
	param4, int64_t param5, int64_t param6);


/*
 * KePringMsg -- this system call uses kernel console output
 * to pring msg directly from process
 */
uint64_t KePrintMsg(int64_t param1, int64_t param2, int64_t param3, int64_t
	param4, int64_t param5, int64_t param6) {
	char* text = (char*)param1;
	AuTextOut("%s\n",text);
	return 0;
}

/*
 * null_call -- 0th call is null call
 */
uint64_t null_call(int64_t param1, int64_t param2, int64_t param3, int64_t
	param4, int64_t param5, int64_t param6) {
	return 0;
}

/* syscall entries */
static void* syscalls[AURORA_MAX_SYSCALL] = {
	[SYS_NULL]                 = null_call,
	[SYS_TEXTOUT]              = SeTextOut,
	[SYS_PAUSE_THREAD]         = PauseThread,
	[SYS_GET_THREAD_ID]        = GetThreadID,
	[SYS_GET_PROCESS_ID]       = GetProcessID,
	[SYS_PROCESS_EXIT]         = ProcessExit,
	[SYS_PROCESS_WAIT]         = ProcessWaitForTermination,
	[SYS_CREATE_PROCESS]       = CreateProcess,
	[SYS_PROCESS_LOAD_EXEC]    = ProcessLoadExec,
	[SYS_CREATE_SHARED_MEM]    = CreateSharedMem,
	[SYS_OBTAIN_SHARED_MEM]    = ObtainSharedMem,
	[SYS_UNMAP_SHARED_MEM]     = UnmapSharedMem,
	[SYS_OPEN_FILE]            = OpenFile,
	[SYS_CREATE_MEM_MAPPING]   = CreateMemMapping,
	[SYS_UNMAP_MEM_MAPPING]    = UnmapMemMapping,
	[SYS_GET_PROCESS_HEAP_MEM] = GetProcessHeapMem,
	[SYS_READ_FILE]            = ReadFile,
	[SYS_WRITE_FILE]           = WriteFile,
	[SYS_CREATE_DIR]           = CreateDir,
	[SYS_REMOVE_FILE]          = RemoveFile,
	[SYS_CLOSE_FILE]           = CloseFile,
	[SYS_FILE_IO_CONTROL]      = FileIoControl,
	[SYS_FILE_STAT]            = FileStat,
	[SYS_PROCESS_SLEEP]        = ProcessSleep,
	[SYS_SIGNAL_RETURN]        = SignalReturn,
	[SYS_SET_SIGNAL]           = SetSignal,
	[SYS_GET_SYSTEM_TIMER_TICK] = GetSystemTimerTick,
	[SYS_GET_FONT_ID]          = AuFTMngrGetFontID,
	[SYS_GET_NUM_FONTS]        = AuFTMngrGetNumFonts,
	[SYS_GET_FONT_SIZE]        = AuFTMngrGetFontSize,
	[SYS_MEM_MAP_DIRTY]        = MemMapDirty,
	[SYS_CREATE_TTY]           = AuTTYCreate,
	[SYS_CREATE_USER_THREAD]   = CreateUserThread,
	[SYS_SET_FILE_TO_PROCESS]  = SetFileToProcess,
	[SYS_PROCESS_HEAP_UNMAP]   = ProcessHeapUnmap,
	[SYS_SEND_SIGNAL]          = SendSignal,
	[SYS_GET_CURRENT_TIME]     = GetCurrentTime,
	[SYS_OPEN_DIR]             = OpenDir,
	[SYS_READ_DIR]             = ReadDir,
	[SYS_CREATE_TIMER]         = CreateTimer,
	[SYS_START_TIMER]          = StartTimer,
	[SYS_STOP_TIMER]           = StopTimer,
	[SYS_DESTROY_TIMER]        = DestroyTimer,
	[SYS_GET_FILE_DESC]        = ProcessGetFileDesc,
	[SYS_FILE_SET_OFFSET]      = FileSetOffset,
	[SYS_GET_TIME_OF_DAY]      = GetTimeOfDay,
	[SYS_CREATE_SOCKET]        = AuCreateSocket,
	[SYS_NET_CONNECT]          = NetConnect,
	[SYS_NET_SEND]             = NetSend,
	[SYS_NET_RECEIVE]          = NetReceive,
	[SYS_SOCKET_SET_OPT]       = AuSocketSetOpt,
	[SYS_NET_BIND]             = NetBind,
	[SYS_NET_ACCEPT]           = NetAccept,
	[SYS_NET_LISTEN]           = NetListen,
	[SYS_CREATE_PIPE]          = AuCreatePipe,
	[SYS_GET_VDISK_INFO]       = AuGetVDiskInfo,
	[SYS_GET_VDISK_PARTITION_INFO] = AuGetVDiskPartitionInfo,
	[SYS_GET_ENVIRONMENT_BLOCK] = GetEnvironmenBlock,
};

//! System Call Handler Functions
//! @param a -- arg1 passed in r12 register
//! @param b -- arg2 passed in r13 register
//! @param c -- arg3 passed in r14 register
//! @param d -- arg4 passed in r15 register
extern "C" int64_t x64_syscall_handler(int a) {
	x64_cli();
	AuThread* current_thr = AuGetCurrentThread();
	uint64_t ret_code = 0;

	if (a < 0 || a >= AURORA_MAX_SYSCALL)
		return -1;
	
	syscall_func func = (syscall_func)syscalls[a];
	if (!func)
		return 0;

	ret_code = func(current_thr->syscall_param.param1, current_thr->syscall_param.param2, current_thr->syscall_param.param3,
			current_thr->syscall_param.param4, current_thr->syscall_param.param5, current_thr->syscall_param.param6);

	return ret_code;
}
