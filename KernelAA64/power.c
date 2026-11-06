/**
* @file power.c
*
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

#include <Board/board.h>
#include <Hal/AA64/aa64cpu.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Cred/cred.h>
#include <process.h>
#include <Hal/AA64/sched.h>
#include <audrv.h>
#include <Hal/AA64/gic.h>
#include <Drivers/uart.h>
#include <Drivers/virtio.h>
#include <Fs/vdisk.h>

/**
 * @brief AuPowerDown -- power down system call
 */
int AuPowerDown() {
	AA64Thread* thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc) {
		proc = AuProcessFindSubThread(thr);
		if (!proc)
			return 1;
	}

	if (proc->creds.uid != 0) {
		UARTDebugOut("[pwrdbg]: denied uid=%d\r\n", (int)proc->creds.uid);
		return 1;
	}

	if (proc->creds.gid != 0) {
		UARTDebugOut("[pwrdbg]: denied gid=%d\r\n", (int)proc->creds.gid);
		return 1;
	}

	UARTDebugOut("[pwrdbg]: flushing disks\r\n");

	/* flush every registered disk's write cache before tearing anything
	 * down. still a no-op per disk til some storage driver actually
	 * populates AuVDisk::Flush --axiss */
	AuVDiskFlushAll();

#ifdef __TARGET_BOARD_QEMU_VIRT__
	AuVirtioKbdDown();
	AuVirtioTabletDown();
#endif

	// call system_down for xeneva for freeing up resources
	// call all drivers to turn off itself before powering off
	AuDrvUnloadAll();
	UARTDebugOut("[pwrdbg]: drivers unloaded\r\n");

	/** suspend timers */
	suspendTimer();
	UARTDebugOut("[pwrdbg]: timers suspended\r\n");

	/* de-initialize the interrupt controller*/
	GICDisable();
	UARTDebugOut("[pwrdbg]: GIC off, cleaning caches\r\n");

	/* dirty cache lines were getting lost across power-off/reset
	 * with this commented out --axiss */
	aa64_clean_invalidate_dcache();
	tlb_flush_vmalle1is(); //need better naming schems boss --axiss

	UARTDebugOut("[pwrdbg]: calling PSCI off\r\n");
	AuAA64BoardPowerDown();

	//false here
	return 1;
}

/**
 * @brief AuPowerReset -- reset system call
 */
void AuPowerReset() {
	AA64Thread* thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc) {
		proc = AuProcessFindSubThread(thr);
		if (!proc)
			return;
	}

	if (proc->creds.uid != 0)
		return;

	if (proc->creds.gid != 0)
		return;

	/* flush every registered disk's write cache before tearing anything
	 * down, still a no-op per disk since no storage driver actually
	 * populates AuVDisk::Flush yet --axiss */
	AuVDiskFlushAll();

#ifdef __TARGET_BOARD_QEMU_VIRT__
	AuVirtioKbdDown();
	AuVirtioTabletDown();
#endif
	// call system_down for xeneva for freeing up resources
	// call all driver to power off itself before restart
	AuDrvUnloadAll();

	/** suspend timers */
	suspendTimer();
	UARTDebugOut("[aurora]: timer suspended \r\n");

	/* de-initialize the interrupt controller */
	GICDisable();

	/* dirty cache lines, disk I/O buffers included, were getting lost
	 * across power-off/reset with this commented out --axiss */
	aa64_clean_invalidate_dcache();
	tlb_flush_vmalle1is();

	AuAA64BoardReboot();
}