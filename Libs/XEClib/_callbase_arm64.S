/**
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

#include <Serv/syscall.h>

.global __chkstk 
__chkstk:
    ret

.global _KePrint
_KePrint:
    str x8, [sp, #-16]!
    mov x16, SYS_TEXTOUT
    svc #0
	mov x0, x6
	ldr x8, [sp], #16
    ret

.global _KePauseThread
_KePauseThread:
    mov x16, SYS_PAUSE_THREAD
    svc #0
	mov x0, x6
    ret

.global _KeGetThreadID
_KeGetThreadID:
    mov x16, SYS_GET_THREAD_ID
    svc #0
	mov x0, x6
    ret


.global _KeGetProcessID
_KeGetProcessID:
    mov x16, SYS_GET_PROCESS_ID
    svc #0
	mov x0, x6
    ret

.global _KeProcessExit
_KeProcessExit:
    mov x16, SYS_PROCESS_EXIT
    svc #0
	mov x0, x6
    ret


//===================================
// _KeProcessWaitForTermination: suspends
// currently running process until another
// process state changes
// @param pid -- process id in rcx
//=========================================

.global _KeProcessWaitForTermination
_KeProcessWaitForTermination:
      mov x16, SYS_PROCESS_WAIT
	  svc #0
	  mov x0, x6
	  ret

//=======================================
// _KeCreateProcess -- create a new process
// slot by taking
// @param parent_id -- process parent id
// @param name -- process name
// @ret -- process id of newly create process
// slot
//========================================
.global _KeCreateProcess
_KeCreateProcess:
      mov x16, SYS_CREATE_PROCESS
      svc #0
	  mov x0, x6
	  ret

.global _KeProcessLoadExec
_KeProcessLoadExec:
      mov x16, SYS_PROCESS_LOAD_EXEC
	  svc #0
	  mov x0, x6
	  ret

.global _KeProcessSleep
_KeProcessSleep:
      mov x16, SYS_PROCESS_SLEEP
	  svc #0
	  mov x0, x6
	  ret


//===============================
// _KeSetSignal -- Register a signal
// handler
//===============================
.global _KeSetSignal
_KeSetSignal:
	  mov x16, SYS_SET_SIGNAL
	  svc #0
	  mov x0, x6
	  ret

.global _KeCreateSharedMem
_KeCreateSharedMem:
      mov x16, SYS_CREATE_SHARED_MEM
	  svc #0
	  mov x0, x6
	  ret

.global _KeObtainSharedMem
_KeObtainSharedMem:
      mov x16, SYS_OBTAIN_SHARED_MEM
	  svc #0
	  mov x0, x6
	  ret

.global _KeUnmapSharedMem
_KeUnmapSharedMem:
      mov x16, SYS_UNMAP_SHARED_MEM
	  svc #0
	  mov x0, x6
	  ret

.global _KeMemMap
_KeMemMap:
	  mov x16, SYS_CREATE_MEM_MAPPING
	  svc #0
	  mov x0, x6
	  ret

.global _KeMemUnmap
_KeMemUnmap:
	  mov x16, SYS_UNMAP_MEM_MAPPING
	  svc #0
	  mov x0, x6
	  ret

//====================================
//_KeGetProcessHeapMem -- Grabs some
// memory from heap area of process
// slot
// @param x0 -- size, number of pages
// multiplied by PAGE_SIZE
//====================================
.global _KeGetProcessHeapMem
_KeGetProcessHeapMem:
	  mov x16, SYS_GET_PROCESS_HEAP_MEM
	  svc #0
	  mov x0, x6
	  ret

//=====================================
// _KeOpenFile -- opens a file and
// return a file descriptor
// @param x0 -- file name
// @param x1 -- open mode
//=====================================
.global _KeOpenFile
_KeOpenFile:
	  mov x16, SYS_OPEN_FILE
	  svc #0
	  mov x0, x6
	  ret

//====================================
// _KeReadFile -- reads a file into
// buffer
// @param x0 -- file descriptor
// @param x1 -- buffer address
// @param x2 -- length to read in
//====================================
.global _KeReadFile
_KeReadFile:
	  mov x16, SYS_READ_FILE
	  svc #0
	  mov x0, x6
	  ret

//====================================
// _KeWriteFile -- write a file onto
// buffer
// @param x0 -- file descriptor
// @param x1 -- buffer address
// @param x2 -- length to write out
//====================================
.global _KeWriteFile
_KeWriteFile:
	  mov x16, SYS_WRITE_FILE
	  svc #0
	  mov x0, x6
	  ret

//==================================
// _KeCreateDir -- create a new
// directory
// @param x0 -- pathname
//==================================
.global _KeCreateDir
_KeCreateDir:
	  mov x16, SYS_CREATE_DIR
	  svc #0
	  mov x0, x6
	  ret


//====================================
// _KeRemoveFile -- remove a file
// from file system
// @param x0 -- filename
//====================================
.global _KeRemoveFile
_KeRemoveFile:
	  mov x16, SYS_REMOVE_FILE
	  svc #0
	  mov x0, x6
	  ret

//====================================
// _KeCloseFile -- closes a file
// @param x0 -- file descriptor
//====================================
.global _KeCloseFile
_KeCloseFile:
	  mov x16, SYS_CLOSE_FILE
	  svc #0
	  mov x0, x6
	  ret

.global _KeFileIoControl
_KeFileIoControl:
	  mov x16, SYS_FILE_IO_CONTROL
	  svc #0
	  mov x0, x6
	  ret

//===============================
// _KeFileStat -- writes the status
// of a file to a buffer
//===============================
.global _KeFileStat
_KeFileStat:
	  mov x16, SYS_FILE_STAT
	  svc #0
	  mov x0, x6
	  ret

//===============================
// _KeGetSystemTimerTick -- get the
// current timer tick value from
// kernel
//===============================
.global _KeGetSystemTimerTick
_KeGetSystemTimerTick:
	  mov x16, SYS_GET_SYSTEM_TIMER_TICK
	  svc #0
	  mov x0, x6
	  ret

//===============================
// _KeGetFontID -- get a system 
// font id
//===============================
.global _KeGetFontID
_KeGetFontID:
	  mov x16, SYS_GET_FONT_ID
	  svc #0
	  mov x0, x6
	  ret

//===============================
// _KeGetNumFonts -- get total 
// number of fonts installed
//===============================
.global _KeGetNumFonts
_KeGetNumFonts:
	  mov x16, SYS_GET_NUM_FONTS
	  svc #0
	  mov x0, x6
	  ret
      
//===============================
// _KeGetFontSize -- get font size
// by its name
//===============================
.global _KeGetFontSize
_KeGetFontSize:
	  mov x16, SYS_GET_FONT_SIZE
	  svc #0
	  mov x0,x6
	  ret


.global _KeMemMapDirty
_KeMemMapDirty:
	  mov x16, SYS_MEM_MAP_DIRTY
	  svc #0
	  mov x0, x6
	  ret

.global _KeCreateTTY
_KeCreateTTY:
	  mov x16, SYS_CREATE_TTY
	  svc #0
	  mov x0, x6
	  ret

.global _KeCreateThread
_KeCreateThread:
	  mov x16, SYS_CREATE_USER_THREAD
	  svc #0
	  mov x0, x6
	  ret

.global _KeSetFileToProcess
_KeSetFileToProcess:
	  mov x16, SYS_SET_FILE_TO_PROCESS
	  svc #0
	  mov x0, x6
	  ret

.global _KeProcessHeapUnmap
_KeProcessHeapUnmap:
	  mov x16, SYS_PROCESS_HEAP_UNMAP
	  svc #0
	  mov x0, x6
	  ret

.global _KeSendSignal
_KeSendSignal:
	  mov x16, SYS_SEND_SIGNAL
	  svc #0
	  mov x0,x6
	  ret

.global _KeGetCurrentTime
_KeGetCurrentTime:
	 mov x16, SYS_GET_CURRENT_TIME
	 svc #0
	 mov x0, x6
	 ret

.global _KeOpenDir
_KeOpenDir:
	 mov x16, SYS_OPEN_DIR
	 svc #0
	 mov x0, x6
	 ret

.global _KeReadDir
_KeReadDir:
	 mov x16, SYS_READ_DIR
	 svc #0
	 mov x0,x6
	 ret

.global _KeCreateTimer
_KeCreateTimer:
	 mov x16, SYS_CREATE_TIMER
	 svc #0
	 mov x0, x6
	 ret

.global _KeStartTimer
_KeStartTimer:
	 mov x16, SYS_START_TIMER
	 svc #0
	 mov x0,x6
	 ret

.global _KeStopTimer
_KeStopTimer:
	 mov x16, SYS_STOP_TIMER
	 svc #0
	 mov x0, x6
	 ret

.global _KeDestroyTimer
_KeDestroyTimer:
	 mov x16, SYS_DESTROY_TIMER
	 svc #0
	 mov x0, x6
	 ret

.global _KeProcessGetFileDesc
_KeProcessGetFileDesc:
	 mov x16, SYS_GET_FILE_DESC
	 svc #0
	 mov x0, x6
	 ret

.global _KeFileSetOffset
_KeFileSetOffset:
	mov x16, SYS_FILE_SET_OFFSET
	svc #0
	mov x0, x6
	ret


.global gettimeofday
gettimeofday:
	mov x16, SYS_GET_TIME_OF_DAY
	svc #0
	mov x0, x6
	ret

.global socket
socket:
   mov x16, SYS_CREATE_SOCKET
   svc #0
   mov x0, x6
   ret

.global connect
connect:
   mov x16, SYS_NET_CONNECT
   svc #0
   mov x0, x6
   ret

.global send
send:
   mov x16, SYS_NET_SEND
   svc #0
   mov x0, x6
   ret

.global receive
receive:
   mov x16, SYS_NET_RECEIVE
   svc #0
   mov x0, x6
   ret

.global socket_setopt
socket_setopt:
   mov x16, SYS_SOCKET_SET_OPT
   // rest will be passed through stack
   svc #0
   mov x0, x6
   ret

.global bind
bind:
   mov x16, SYS_NET_BIND
   svc #0
   mov x0, x6
   ret

.global accept
accept:
   mov x16, SYS_NET_ACCEPT
   svc #0
   mov x0, x6
   ret

.global listen
listen:
	mov x16, SYS_NET_LISTEN
	svc #0
	mov x0, x6
	ret

.global getsockopt
getsockopt:
	mov x16, SYS_GETSOCKOPT
	svc #0
	mov x0, x6
	ret

.global accept4
accept4:
	mov x16, SYS_ACCEPT4
	svc #0
	mov x0, x6
	ret

.global socketpair
socketpair:
	mov x16, SYS_SOCKETPAIR
	svc #0
	mov x0, x6
	ret

.global shutdown
shutdown:
	mov x16, SYS_SHUTDOWN
	svc #0
	mov x0, x6
	ret

.global getsockname
getsockname:
	mov x16, SYS_GETSOCKNAME
	svc #0
	mov x0, x6
	ret

.global getpeername
getpeername:
	mov x16, SYS_GETPEERNAME
	svc #0
	mov x0, x6
	ret

.global sendto
sendto:
	mov x16, SYS_SENDTO
	svc #0
	mov x0, x6
	ret

.global recvfrom
recvfrom:
	mov x16, SYS_RECVFROM
	svc #0
	mov x0, x6
	ret

.global sendmmsg
sendmmsg:
	mov x16, SYS_SENDMMSG
	svc #0
	mov x0, x6
	ret

.global recvmmsg
recvmmsg:
	mov x16, SYS_RECVMMSG
	svc #0
	mov x0, x6
	ret

.global _KeIfNameToIndex
_KeIfNameToIndex:
	mov x16, SYS_IF_NAMETOINDEX
	svc #0
	mov x0, x6
	ret

.global _KeGetIfAddrs
_KeGetIfAddrs:
	mov x16, SYS_GETIFADDRS
	svc #0
	mov x0, x6
	ret

.global _KeCreatePipe
_KeCreatePipe:
	mov x16, SYS_CREATE_PIPE
	svc #0
	mov x0, x6
	ret

.global _KeGetStorageDiskInfo
_KeGetStorageDiskInfo:
	mov x16, SYS_GET_VDISK_INFO
	svc #0
	mov x0, x6
	ret

.global _KeGetStoragePartitionInfo
_KeGetStoragePartitionInfo:
	mov x16, SYS_GET_VDISK_PARTITION_INFO
	svc #0
	mov x0, x6
	ret

.global _KeGetEnvironmentBlock
_KeGetEnvironmentBlock:
	mov x16, SYS_GET_ENVIRONMENT_BLOCK
	svc #0
	mov x0, x6
	ret

.global _KeCredChangeID
_KeCredChangeID:
    mov x16, SYS_CRED_CHANGE_ID
	svc #0
	mov x0, x6
	ret

.global _KeCredAddSGroup
_KeCredAddSGroup:
    mov x16, SYS_CRED_ADD_SGROUP
	svc #0
	mov x0, x6
	ret

.global _KeCredSetCap
_KeCredSetCap:
    mov x16, SYS_CRED_SET_CAP
	svc #0
	mov x0, x6
	ret

.global _KeCredGetCap
_KeCredGetCap:
    mov x16, SYS_CRED_GET_CAP
	svc #0
	mov x0, x6
	ret

.global _KeSetUID
_KeSetUID:
    mov x16, SYS_SET_UID
	svc #0
	mov x0, x6
	ret

.global _KeSetGID
_KeSetGID:
    mov x16, SYS_SET_GID
	svc #0
	mov x0, x6
	ret

.global _KeGetGlobalGroupID
_KeGetGlobalGroupID:
    mov x16, SYS_CRED_GET_GROUP_ID
	svc #0
	mov x0, x6
	ret

.global _KeProcessTokenAddSelf
 _KeProcessTokenAddSelf:
    mov x16, SYS_PROCESS_TOKEN_ADD_SELF
	svc #0
	mov x0, x6
	ret

.global _KeProcessTokenGetThreadID
_KeProcessTokenGetThreadID:
    mov x16, SYS_PROCESS_TOKEN_GET_THREAD_ID
	svc #0
	mov x0, x6
	ret

.global _KeProcessTokenRemoveSelf
_KeProcessTokenRemoveSelf:
    mov x16, SYS_PROCESS_TOKEN_REMOVE_SELF
	svc #0
	mov x0, x6
	ret

.global _KePowerDown
_KePowerDown:
    mov x16, SYS_POWER_DOWN
	svc #0
	/*haha, power down success hole eneiu return nahe*/
	mov x0, x6
	ret


.global _KePowerReset
_KePowerReset:
    mov x16, SYS_POWER_RESET
	svc #0
	/* ki dhemali behh, return nahe buli janiu, return mari asu 
	 * haha, bhakha ulai jai mukhere 
	 */
	 mov x0, x6
	 ret

.global _KeGetCurrentUS
_KeGetCurrentUS:
    mov x16, SYS_GET_CURRENT_US
	svc #0
	mov x0, x6
	ret

.global _KeGetCurrentMS
_KeGetCurrentMS:
    mov x16, SYS_GET_CURRENT_MS
	svc #0
	mov x0, x6
	ret

.global _KeSetAlarm
_KeSetAlarm:
    mov x16, SYS_ALARM
	svc #0
	mov x0, x6
	ret

.global _KeSetITimer
_KeSetITimer:
    mov x16, SYS_SET_ITIMER
	svc #0
	mov x0, x6
	ret

.global _KeGetITimer
_KeGetITimer:
    mov x16, SYS_GET_ITIMER
	svc #0
	mov x0, x6
	ret

.global _KeGetNumProcessCount
_KeGetNumProcessCount:
    mov x16, SYS_GET_NUM_PROCESS_COUNT
	svc #0
	mov x0, x6
	ret

.global _KeProcessFetch
_KeProcessFetch:
    mov x16, SYS_PROCESS_FETCH
	svc #0
	mov x0, x6
	ret

.global _KeSetWalltime
_KeSetWalltime:
    mov x16, SYS_SET_WALLTIME
	svc #0
	mov x0, x6
	ret

.global _KeGetWalltime
_KeGetWalltime:
    mov x16, SYS_GET_WALLTIME
	svc #0
	mov x0, x6
	ret

.global _PthreadSetParam
_PthreadSetParam:
    mov x2, x0
	ret

.global _KeExitSubThread
_KeExitSubThread:
    mov x16, 79
	svc #0
	mov x0, x6
	ret

.global _KeSendSignalToThread
_KeSendSignalToThread:
    mov x16, 80
	svc #0
	mov x0, x6
	ret

.global _KeThreadWaitForTermination
_KeThreadWaitForTermination:
    mov x16, 81
	svc #0
	mov x0, x6
	ret

.global uname
uname:
    mov x16, 82
	svc #0
	mov x0, x6
	ret

.global poll
poll:
    mov x16, 83
	svc #0
	mov x0, x6
	ret