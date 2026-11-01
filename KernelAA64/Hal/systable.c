/**
* @file systable.c
* 
* BSD 2-Clause License
*
* Copyright (c) 2023-2025, Manas Kamal Choudhury
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
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64cpu.h>
#include <Hal/AA64/sched.h>
#include <_null.h>
#include <Serv/sysserv.h>
#include <Serv/syscall.h>
#include <process.h>
#include <Fs/srvfs.h>
#include <Mm/shm.h>
#include <Mm/mmap.h>
#include <aucon.h>
#include <ftmngr.h>
#include <Fs/vfs.h>
#include <Fs/tty.h>
#include <Fs/vdisk.h>
#include <Net/socket.h>
#include <Hal/AA64/profile.h>
#include <Cred/group.h>
#include <Cred/cred.h>
#include <proctoken.h>
#include <Fs/pipe.h>
#include <power.h>
#include <Log/_print.h>
#include <Log/klog.h>
#include <timer.h>
#include <bordoisila_ptr.h>
#include <Sync/poll.h>

AA64Registers* svcCurrentRegs;

/* Syscall function format */
typedef int64_t (*syscall_func)(
	int64_t param1, int64_t param2, int64_t param3, int64_t param4, int64_t param5, int64_t param6);

uint64_t null_call(int64_t param1,
				   int64_t param2,
				   int64_t param3,
				   int64_t param4,
				   int64_t param5,
				   int64_t param6) {
	UARTDebugOut("Null call initiated \r\n");
	return 1;
}

extern uint64_t read_sp();

uint64_t test_call() {
	UARTDebugOut("Test call initiated \r\n");
	return 100;
}

#define _UTSNAME_LENGTH 256

struct utsname {
	char sysname[_UTSNAME_LENGTH];
	char nodename[_UTSNAME_LENGTH];
	char release[_UTSNAME_LENGTH];
	char version[_UTSNAME_LENGTH];
	char machine[_UTSNAME_LENGTH];
	char domainname[_UTSNAME_LENGTH];
};

/**
 * @brief ExecUname -- uname system call
 * @param name -- Pointer to user space utsname structure
 */
int ExecUname(struct utsname* name) {
	if (!name)
		return 1;
	if (!__BORDOISILA_UBUF_VALIDATE(name, sizeof(struct utsname))) {
		UARTDebugOut("ExecUname system call, invalid user buffer : %x \r\n", name);
		return 1;
	}
	//check for pointer validation of name
	strcpy(name->sysname, XENEVA_OS_NAME);
	strcpy(name->nodename, XENEVA_OS_NODENAME);
	strcpy(name->release, XENEVA_KERNEL_RELEASE);
	strcpy(name->version,
		   XENEVA_KERNEL_VERSION " #" XENEVA_KERNEL_BUILD " " XENEVA_KERNEL_BUILD_TAG);
	strcpy(name->machine, XENEVA_KERNEL_MACHINE);
	strcpy(name->domainname, " ");
	return 0;
}

AA64Registers* AA64GetCurrentRegCtx() {
	return svcCurrentRegs;
}

static void* syscalls[AURORA_MAX_SYSCALL] = {
	[SYS_NULL] = null_call,
	[SYS_TEXTOUT] = UARTDebugOut,
	[SYS_PAUSE_THREAD] = PauseThread,
	[SYS_GET_THREAD_ID] = GetThreadID,
	[SYS_GET_PROCESS_ID] = GetProcessID,
	[SYS_PROCESS_EXIT] = ProcessExit,
	[SYS_PROCESS_WAIT] = ProcessWaitForTermination,
	[SYS_CREATE_PROCESS] = CreateProcess,
	[SYS_PROCESS_LOAD_EXEC] = ProcessLoadExec,
	[SYS_CREATE_SHARED_MEM] = CreateSharedMem,
	[SYS_OBTAIN_SHARED_MEM] = ObtainSharedMem,
	[SYS_UNMAP_SHARED_MEM] = UnmapSharedMem,
	[SYS_OPEN_FILE] = OpenFile,
	[SYS_CREATE_MEM_MAPPING] = CreateMemMapping,
	[SYS_UNMAP_MEM_MAPPING] = UnmapMemMapping,
	[SYS_GET_PROCESS_HEAP_MEM] = GetProcessHeapMem,
	[SYS_READ_FILE] = ReadFile,
	[SYS_WRITE_FILE] = WriteFile,
	[SYS_CREATE_DIR] = CreateDir,
	[SYS_REMOVE_FILE] = RemoveFile,
	[SYS_CLOSE_FILE] = CloseFile,
	[SYS_FILE_IO_CONTROL] = FileIoControl,
	[SYS_FILE_STAT] = FileStat,
	[SYS_PROCESS_SLEEP] = ProcessSleep,
	[SYS_SIGNAL_RETURN] = SignalReturn,
	[SYS_SET_SIGNAL] = SetSignal,
	[SYS_GET_SYSTEM_TIMER_TICK] = AuGetSystemTimerTick,
	[SYS_GET_FONT_ID] = AuFTMngrGetFontID,
	[SYS_GET_NUM_FONTS] = AuFTMngrGetNumFonts,
	[SYS_GET_FONT_SIZE] = AuFTMngrGetFontSize,
	[SYS_MEM_MAP_DIRTY] = MemMapDirty,
	[SYS_CREATE_TTY] = AuTTYCreate,
	[SYS_CREATE_USER_THREAD] = CreateUserThread,
	[SYS_SET_FILE_TO_PROCESS] = SetFileToProcess,
	[SYS_PROCESS_HEAP_UNMAP] = ProcessHeapUnmap,
	[SYS_SEND_SIGNAL] = SendSignal,
	[SYS_GET_CURRENT_TIME] = 0,
	[SYS_OPEN_DIR] = OpenDir,
	[SYS_READ_DIR] = ReadDir,
	[SYS_CREATE_TIMER] = 0,
	[SYS_START_TIMER] = 0,
	[SYS_STOP_TIMER] = 0,
	[SYS_DESTROY_TIMER] = 0,
	[SYS_GET_FILE_DESC] = ProcessGetFileDesc,
	[SYS_FILE_SET_OFFSET] = FileSetOffset,
	[SYS_GET_TIME_OF_DAY] = 0,
	[SYS_CREATE_SOCKET] = AuCreateSocket,
	[SYS_NET_CONNECT] = NetConnect,
	[SYS_NET_SEND] = NetSend,
	[SYS_NET_RECEIVE] = NetReceive,
	[SYS_SOCKET_SET_OPT] = NetSocketSetOpt,
	[SYS_NET_BIND] = NetBind,
	[SYS_NET_ACCEPT] = NetAccept,
	[SYS_NET_LISTEN] = NetListen,
	[SYS_SOCKETPAIR] = NetSocketPair,
	[SYS_GETSOCKOPT] = NetGetSockOpt,
	[SYS_GETSOCKNAME] = NetGetSockName,
	[SYS_GETPEERNAME] = NetGetPeerName,
	[SYS_SHUTDOWN] = NetShutdown,
	[SYS_SENDMMSG] = NetSendMmsg,
	[SYS_RECVMMSG] = NetRecvMmsg,
	[SYS_ACCEPT4] = NetAccept4,
	[SYS_SENDTO] = NetSendTo,
	[SYS_RECVFROM] = NetRecvFrom,
	[SYS_IF_NAMETOINDEX] = NetIfNameToIndex,
	[SYS_GETIFADDRS] = NetGetIfAddrs,
	[SYS_CREATE_PIPE] = AuCreatePipe,
	[SYS_GET_VDISK_INFO] = AuGetVDiskInfo,
	[SYS_GET_VDISK_PARTITION_INFO] = AuGetVDiskPartitionInfo,
	[SYS_GET_ENVIRONMENT_BLOCK] = GetEnvironmenBlock,
	[SYS_CRED_CHANGE_ID] = AuCredChangeID,
	[SYS_CRED_ADD_SGROUP] = AuCredAddSGroup,
	[SYS_CRED_SET_CAP] = AuCredSetCap,
	[SYS_CRED_GET_CAP] = AuCredGetCap,
	[SYS_SET_UID] = AuSetUID,
	[SYS_SET_GID] = AuSetGID,
	[SYS_CRED_GET_GROUP_ID] = AuCredGetGroupID,
	[SYS_PROCESS_TOKEN_ADD_SELF] = AuProcessTokenAddSelf,
	[SYS_PROCESS_TOKEN_GET_THREAD_ID] = AuProcessTokenGetThreadID,
	[SYS_PROCESS_TOKEN_REMOVE_SELF] = AuProcessTokenRemoveSelf,
	[SYS_POWER_DOWN] = AuPowerDown,
	[SYS_POWER_RESET] = AuPowerReset,
	[SYS_GET_CURRENT_US] = AuGetCurrentUS,
	[SYS_GET_CURRENT_MS] = AuGetCurrentMS,
	[SYS_ALARM] = Alarm,
	[SYS_SET_ITIMER] = SetITimer,
	[SYS_GET_ITIMER] = GetITimer,
	[SYS_GET_NUM_PROCESS_COUNT] = AuProcGetNumProcessCount,
	[SYS_PROCESS_FETCH] = AuProcessFetch,
	[SYS_SET_WALLTIME] = AuSetWalltime,
	[SYS_GET_WALLTIME] = AuGetWalltime,
	[SYS_EXIT_SUB_THREAD] = ExitSubThread,
	[SYS_SEND_SIGNAL_TO_THREAD] = SendSignalToThread,
	[SYS_THREAD_WAIT_FOR_TERMINATION] = AuThreadWaitForTermination,
	[SYS_KERNEL_UNAME] = ExecUname,
	[SYS_FILE_POLL] = AuPoll,
};

#ifdef __KERNEL_PROFILER_ON__
static char* syscall_name[AURORA_MAX_SYSCALL] = {
	[SYS_NULL] = "null_call",
	[SYS_TEXTOUT] = "UARTDebugOut",
	[SYS_PAUSE_THREAD] = "PauseThread",
	[SYS_GET_THREAD_ID] = "GetThreadID",
	[SYS_GET_PROCESS_ID] = "GetProcessID",
	[SYS_PROCESS_EXIT] = "ProcessExit",
	[SYS_PROCESS_WAIT] = "ProcessWaitForTermination",
	[SYS_CREATE_PROCESS] = "CreateProcess",
	[SYS_PROCESS_LOAD_EXEC] = "ProcessLoadExec",
	[SYS_CREATE_SHARED_MEM] = "CreateSharedMem",
	[SYS_OBTAIN_SHARED_MEM] = "ObtainSharedMem",
	[SYS_UNMAP_SHARED_MEM] = "UnmapSharedMem",
	[SYS_OPEN_FILE] = "OpenFile",
	[SYS_CREATE_MEM_MAPPING] = "CreateMemMapping",
	[SYS_UNMAP_MEM_MAPPING] = "UnmapMemMapping",
	[SYS_GET_PROCESS_HEAP_MEM] = "GetProcessHeapMem",
	[SYS_READ_FILE] = "ReadFile",
	[SYS_WRITE_FILE] = "WriteFile",
	[SYS_CREATE_DIR] = "CreateDir",
	[SYS_REMOVE_FILE] = "RemoveFile",
	[SYS_CLOSE_FILE] = "CloseFile",
	[SYS_FILE_IO_CONTROL] = "FileIoControl",
	[SYS_FILE_STAT] = "FileStat",
	[SYS_PROCESS_SLEEP] = "ProcessSleep",
	[SYS_SIGNAL_RETURN] = "SignalReturn",
	[SYS_SET_SIGNAL] = "SetSignal",
	[SYS_GET_SYSTEM_TIMER_TICK] = "AuGetSystemTimerTick",
	[SYS_GET_FONT_ID] = "AuFTMngrGetFontID",
	[SYS_GET_NUM_FONTS] = "AuFTMngrGetNumFonts",
	[SYS_GET_FONT_SIZE] = "AuFTMngrGetFontSize",
	[SYS_MEM_MAP_DIRTY] = "MemMapDirty",
	[SYS_CREATE_TTY] = "AuTTYCreate",
	[SYS_CREATE_USER_THREAD] = "CreateUserThread",
	[SYS_SET_FILE_TO_PROCESS] = "SetFileToProcess",
	[SYS_PROCESS_HEAP_UNMAP] = "ProcessHeapUnmap",
	[SYS_SEND_SIGNAL] = "SendSignal",
	[SYS_GET_CURRENT_TIME] = "GetCurrentTime",
	[SYS_OPEN_DIR] = "OpenDir",
	[SYS_READ_DIR] = "ReadDir",
	[SYS_CREATE_TIMER] = "CreateTimer",
	[SYS_START_TIMER] = "StartTimer",
	[SYS_STOP_TIMER] = "StopTimer",
	[SYS_DESTROY_TIMER] = "DestroyTimer",
	[SYS_GET_FILE_DESC] = "ProcessGetFileDesc",
	[SYS_FILE_SET_OFFSET] = "FileSetOffset",
	[SYS_GET_TIME_OF_DAY] = "GetTimeOfDay",
	[SYS_CREATE_SOCKET] = "AuCreateSocket",
	[SYS_NET_CONNECT] = "NetConnect",
	[SYS_NET_SEND] = "NetSend",
	[SYS_NET_RECEIVE] = "NetReceive",
	[SYS_SOCKET_SET_OPT] = "NetSocketSetOpt",
	[SYS_NET_BIND] = "NetBind",
	[SYS_NET_ACCEPT] = "NetAccept",
	[SYS_NET_LISTEN] = "NetListen",
	[SYS_SOCKETPAIR] = "NetSocketPair",
	[SYS_GETSOCKOPT] = "NetGetSockOpt",
	[SYS_GETSOCKNAME] = "NetGetSockName",
	[SYS_GETPEERNAME] = "NetGetPeerName",
	[SYS_SHUTDOWN] = "NetShutdown",
	[SYS_SENDMMSG] = "NetSendMmsg",
	[SYS_RECVMMSG] = "NetRecvMmsg",
	[SYS_ACCEPT4] = "NetAccept4",
	[SYS_SENDTO] = "NetSendTo",
	[SYS_RECVFROM] = "NetRecvFrom",
	[SYS_IF_NAMETOINDEX] = "NetIfNameToIndex",
	[SYS_GETIFADDRS] = "NetGetIfAddrs",
	[SYS_CREATE_PIPE] = "AuCreatePipe",
	[SYS_GET_VDISK_INFO] = "AuGetVDiskInfo",
	[SYS_GET_VDISK_PARTITION_INFO] = "AuGetVDiskPartitionInfo",
	[SYS_GET_ENVIRONMENT_BLOCK] = "GetEnvironmenBlock",
	[SYS_CRED_CHANGE_ID] = "AuCredChangeID",
	[SYS_CRED_ADD_SGROUP] = "AuCredAddSGroup",
	[SYS_CRED_SET_CAP] = "AuCredSetCap",
	[SYS_CRED_GET_CAP] = "AuCredGetCap",
	[SYS_SET_UID] = "AuSetUID",
	[SYS_SET_GID] = "AuSetGID",
	[SYS_CRED_GET_GROUP_ID] = "AuCredGetGroupID",
	[SYS_PROCESS_TOKEN_ADD_SELF] = "AuProcessTokenAddSelf",
	[SYS_PROCESS_TOKEN_GET_THREAD_ID] = "AuProcessTokenGetThreadID",
	[SYS_PROCESS_TOKEN_REMOVE_SELF] = "AuProcessTokenRemoveSelf",
	[SYS_POWER_DOWN] = "AuPowerDown",
	[SYS_POWER_RESET] = "AuPowerReset",
	[SYS_GET_CURRENT_US] = "AuGetCurrentUS",
	[SYS_GET_CURRENT_MS] = "AuGetCurrentMS",
	[SYS_ALARM] = "Alarm",
	[SYS_SET_ITIMER] = "SetITimer",
	[SYS_GET_ITIMER] = "GetITimer",
	[SYS_GET_NUM_PROCESS_COUNT] = "AuProcGetNumProcessCount",
	[SYS_PROCESS_FETCH] = "AuProcessFetch",
	[SYS_SET_WALLTIME] = "AuSetWalltime",
	[SYS_GET_WALLTIME] = "AuGetWalltime",
};
#endif

extern void set_syscall_retval(uint64_t val);
extern bool isSyscall();
extern void modifyx17();
/**
 * @brief AuAA64SyscalHandler -- common system call handler for aarch64
 * @param regs -- Register information passed by sync_exception
 */
int64_t AuLinuxSyscall(
	uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

void AuAA64SyscallHandler(AA64Registers* regs) {
	mask_irqs();
	uint64_t vector = regs->x16;
	uint64_t retcode = 0;
	uint64_t syscall_elr;
	uint64_t syscall_spsr = read_spsr_el1();
	__asm__ volatile("mrs %0, elr_el1" : "=r"(syscall_elr));

	/* Musl puts the Linux number in x8. PE programs still use x16. */
	{
		AA64Thread* linux_thr = AuGetCurrentThread();
		AuProcess* linux_proc = NULL;
		if (linux_thr) {
			linux_proc = (AuProcess*)linux_thr->procSlot;
			if (!linux_proc)
				linux_proc = AuProcessFindThread(linux_thr);
			if (!linux_proc)
				linux_proc = AuProcessFindSubThread(linux_thr);
		}
		if (linux_proc && (linux_proc->type_flags & PROCESS_TYPE_LINUX)) {
			linux_thr->returnFromSyscall = 1;
			linux_thr->syscallNum = (uint32_t)regs->x8;
			svcCurrentRegs = regs;
			retcode = (uint64_t)AuLinuxSyscall((uint64_t)regs->x8,
											   (uint64_t)regs->x0,
											   (uint64_t)regs->x1,
											   (uint64_t)regs->x2,
											   (uint64_t)regs->x3,
											   (uint64_t)regs->x4,
											   (uint64_t)regs->x5);
			regs->x0 = retcode;
			regs->x6 = retcode;
			linux_thr->returnFromSyscall = 0;
			return;
		}
	}

#ifdef __KERNEL_PROFILER_ON__
	if (syscall_name[vector] == 0)
		goto skip_1;
	PROFILE_START(syscall_name[vector]);
skip_1:
#endif
	if ((vector >= AURORA_MAX_SYSCALL) || (vector < 0)) {
		regs->x0 = retcode;
		return;
	}

	AA64Thread* currThr = AuGetCurrentThread();
	currThr->returnFromSyscall = 1;
	currThr->syscallNum = vector;
	svcCurrentRegs = regs;

	syscall_func func = (syscall_func)syscalls[vector];
	if (!func) {
		currThr->returnFromSyscall = 0;
		regs->x0 = (uint64_t)-AU_ERR_ENOSYS;

#ifdef __KERNEL_PROFILER_ON__
		if (syscall_name[vector] == 0)
			goto skip_2;
		PROFILE_END(syscall_name[vector]);
	skip_2:
#endif
		return;
	}

	if (vector == 31) {
		UARTDebugOut("Enterring function x0: %x, x1 : %x \r\n", regs->x0, regs->x1);
		UARTDebugOut("Current sp : %x \r\n", read_sp());
	}
	AuSrvfsSyscallEnter(currThr);
	retcode = func(regs->x0, regs->x1, regs->x2, regs->x3, regs->x4, regs->x5);
	AuSrvfsSyscallExit(currThr);
	/* A syscall may synchronously invoke a component that enables interrupts.
	 * If an IRQ schedules the current thread while that component is running,
	 * ELR_EL1 can retain the component's kernel PC instead of the user SVC
	 * return address. Restore the entry PC before the exception return path. */
	__asm__ volatile("msr elr_el1, %0" : : "r"(syscall_elr));
	__asm__ volatile("msr spsr_el1, %0" : : "r"(syscall_spsr));
	regs->x0 = retcode;
	regs->x6 = retcode;
	currThr->returnFromSyscall = 0;

#ifdef __KERNEL_PROFILER_ON__
	if (syscall_name[vector] == 0)
		goto skip_3;
	PROFILE_END(syscall_name[vector]);
skip_3:
#endif
	return;
}
