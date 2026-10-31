/**
* @file init.c
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

#include <aurora.h>
#include <stdint.h>
#include <aucon.h>
#include <Hal/AA64/aa64cpu.h>
#include <dtb.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Mm/kmalloc.h>
#include <Mm/shm.h>
#include <Mm/mmfile.h>
#include <Mm/dma.h>
#include <_null.h>
#include <Hal/AA64/gic.h>
#include <Hal/basicacpi.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <loader.h>
#include <Drivers/uart.h>
#include <Hal/AA64/qemu.h>
#include <list.h>
#include <Fs/vfs.h>
#include <Fs/Dev/devclip.h>
#include <Fs/initrd.h>
#include <Drivers/virtio.h>
#include <audrv.h>
#include <Hal/AA64/sched.h>
#include <Fs/tty.h>
#include <ftmngr.h>
#include <Ipc/postbox.h>
#include <Board/board.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <Net/aunet.h>
#include <Hal/AA64/profile.h>
#include <Cred/group.h>
#include <Cred/cred.h>
#include <Sound/sound.h>
#include <proctoken.h>
#include <timer.h>
#include <Fs/vdisk.h>
#include <Fs/Fat/Fat.h>
#include <Fs/Fat/FatFile.h>
#include <Fs/Fat/FatDir.h>
#include <Log/klog.h>
#include <linux/module_loader.h>
#include <fixup.h>

extern void AuBtInitialize(void);

extern int _fltused = 1;
static bool _littleboot_used;

/**
 * @brief LBUartPutc -- put a character to UART
 * @param c -- Character to put
 */
void AuUartPutc(char c) {
	char* uart0 = (char*)UART0;
	while ((*(uart0 + 0x18) & (1 << 5)))
		;
	*uart0 = c;
}

/**
 * @brief LBUartPutString -- print a string
 * to UART
 * @param s -- String to print
 */
void AuUartPutString(const char* s) {
	while (*s)
		AuUartPutc(*s++);
}

/**
 * @brief AuLittleBootUsed -- check if little boot protocol
 * is used
 */
bool AuLittleBootUsed() {
	return _littleboot_used;
}

int i_ = 1;

extern void sub_rsp();

void AuEntryTest(uint64_t test) {
	int c = 10;
	//enable_irqs();
	AuTextOut("[aurora]: test2 \r\n");
	while (1) {
		enable_irqs();
		if ((c % 2) == 0)
			AuTextOut("[aurora_t1]: 2 \r\n");
		c++;
	}
}

void AuEntryTest2(uint64_t test) {
	int d = 10;
	//enable_irqs();
	/*AA64Thread* thr = AuThreadFindByIDBlockList(1);
	if (thr) {
		UARTDebugOut("Unblocking thr : %s \n", thr->name);
		AuUnblockThread(thr);
	}*/
	AuTextOut("[aurora]: test3 \r\n");
	while (1) {
		enable_irqs();
		if ((d % 2) != 0)
			AuTextOut("[aurora_t2]: 3 \r\n");
		d++;
	}
}

void AuEntryTest4(uint64_t test) {
	int d = 10;
	//enable_irqs();
	/*AA64Thread* thr = AuThreadFindByIDBlockList(1);
	if (thr) {
		UARTDebugOut("Unblocking thr : %s \n", thr->nme);
		AuUnblockThread(thr);
	}*/
	AuTextOut("[aurora]: test4 \r\n");
	while (1) {
		enable_irqs();
		if ((d % 2) != 0)
			AuTextOut("[aurora_t4]: 4 \r\n");
		d++;
	}
}

KERNEL_BOOT_INFO* bootinfo;

/**
 * @brief AuGetBootInfoStruc -- return kernel boot information
 */
KERNEL_BOOT_INFO* AuGetBootInfoStruc() {
	return bootinfo;
}

extern void debugLIBOn();
extern void* dlmalloc(size_t sz);
extern void dlfree(void* p);
extern void XPT2046Initialise();

extern void AuPmmngrDebugInfo();
extern void AuConsoleBypassAuTextOut();

void _AuroraTimerCallback(void* p) {
	UARTDebugOut("from inside timer++ \r\n");
}

extern void modload_test_run(void);

/* DCL initcalls (linux/init.h: fs_initcall()) + the drivers/char proof */
extern void DclRunInitcalls(void);
extern void DclMemTestRun(void);

/* Stage 1 of the 8250/serial milestone: interrupt registration, port I/O,
 * windowing, jiffies, wait queues. Runs after DclMemTestRun so the log keeps
 * the same order as the milestones. */
extern void DclPrimTestRun(void);

/* Stage 2: the flip buffer -- tty_buffer.c's allocation, the used/commit/read
 * protocol, the flag byte stride, and delivery through queue_work(). */
extern void DclTtyBufferTestRun(void);

/* Stage 3: the serial core -- uart_get_divisor(), uart_get_baud_rate(),
 * uart_update_timeout(), and 8250's set_termios()/tx_chars() driven against
 * a register file that lives in ordinary RAM. */
extern void DclSerialTestRun(void);

/**
 * @brief _AuMain -- the main entry point for kernel
 * @param info -- Kernel Boot information passed
 * by bootloader
 **/
void _AuMain(KERNEL_BOOT_INFO* info) {
	_littleboot_used = false;
	AuUartPutString("[aurora]: kernel entry reached \r\n");
	if (info->boot_type == BOOT_LITTLEBOOT_ARM64) {
		AuUartPutString("[aurora]:Kernel is booted using LittleBoot ARM64 \r\n");
		_littleboot_used = true;
	}

	bootinfo = info;

#ifdef __KERNEL_PROFILER_ON__
	PROFILE_START("_AuMain");
#endif

	AuConsoleInitialize(info, true);
	B_KLogInit();
	AuDeviceTreeInitialize(info);
	AA64CpuInitialize();
	mask_irqs();
	AuTextOut("[aurora]: CPU initialized \r\n");
	AuPmmngrInitialize(info);
	AuVmmngrInitialize();
	AuTextOut("[aurora]: virtual memory manager initialized \r\n");
	AuHeapInitialize();
	AuDeviceTreeMapMMIO();
	AA64CPUPostInitialize(info);
	AuAA64BoardInitialize();
	AuVFSInitialise();
	AuInitrdInitialize(info);
	/* Device nodes published by AuConsolePostInitialise (notably /dev/graph)
	 * use the global group table.  Initialize it before those nodes are
	 * created so user-space compositor and GPU services receive the same
	 * world-group ID as their credentials. */
	AuCredGroupInitialize();
	AuConsolePostInitialise(info);
	//AuConsoleBypassAuTextOut();

	AuroraTimerInitialize();

	/* initialize the tty service */
	AuTTYInitialise();

	/* initialize the shared memory manager*/
	AuInitialiseSHMMan();
	AuMmngrFileCacheInit();

	/* /dev/clipboard registers here rather than from devfs: its group comes
	 * from the initialized table. Registering with devfs before the group
	 * table was ready gave the node group 0, and AuCredCheckPermissions admits
	 * only root or a matching group, so no ordinary process could open it. */
	AuDevClipInitialise();

	/* initialize the network layer */
	AuInitialiseNet();

	/* initialize aurora sound layer */
	AuSoundInitialise();

	/* initialize process token layer */
	AuProcessTokenInitialize();

#ifdef __TARGET_BOARD_RPI3__
	XPT2046Initialise();
#endif

	/* initialize our basic requirement */
#if defined(__TARGET_BOARD_QEMU_VIRT__) && !defined(__XENEVA_BYPASS_VIRTIO_INPUT__)
	/* for qemu virt board, virtIO inputs are used.  On the vendored-input
	 * build these PCI functions belong to DCL's virtio scan instead. */
//	AuVirtIOInputInitialize();
#endif
	/* required virtio-mouse and keyboard */
	//Here goes board pre driver initialize
	AuDrvMngrInitialize(info);
	UARTDebugOut("[aurora]: driver initialized \r\n");

	FontManagerInitialise();

	/* DCL initcalls first: chr_dev_init() publishes the mainline memory
	 * devices (/dev/null, /dev/zero, /dev/full, /dev/random, /dev/urandom,
	 * /dev/kmsg, /dev/mem) through the cdev bridge, so the .ko test below
	 * and every later user see them. */
	DclRunInitcalls();

	/* Test DCL Layer: load an embedded ELF object through
	 * DCL/module_loader.c, then bind the virtio-rng device. The driver is
	 * compiled from source into the image now (Vendored/, registered by
	 * DclRunInitcalls() above); what runs here is the scan for the device
	 * and the hwrng self-test (see modload_test.c). */
	modload_test_run();

	/* /dev/urandom reads the hardware RNG, so this runs after
	 * modload_test_run() has bound virtio_rng. */
	DclMemTestRun();

	/* Stage 1 (primitives) for tty/serial: proves jiffies advances, an SPI
	 * irq registers and an SGI is refused, ioremap is the linear map, a
	 * window claim is refused on overlap, and a wake reaches a waiter. */
	DclPrimTestRun();

	/* Stage 2 (flip buffer) for tty/serial: proves tty_buffer.c allocates
	 * and accounts correctly, that bytes stay invisible until
	 * tty_flip_buffer_push(), that the flag byte survives the round trip
	 * through data + size, and that queue_work() delivers inline. */
	DclTtyBufferTestRun();

	/* Stage 3 (serial core) for tty/serial: proves the baud rate helpers
	 * decode and round the way mainline says they do, that a full
	 * serial8250_do_set_termios() lands the divisor in the latch and the
	 * frame time in the port, and that serial8250_tx_chars() drains the
	 * tty's transmit ring byte by byte. */
	DclSerialTestRun();

	/* from here, be carefull with AuPmmngrAllocBlocks,
	 * sometime it doesn't allocate blocks contiguously,
	 * it allocate one previously freed block and other
	 * contiguously from some unallocated portion of
	 * physical memory, which make it problamatic for kheap
	 * or other data structures that get overwritten.
	 * We need to be carefull on low level side for this 
	 */
	AuInitialiseLoader();

	/* initialize the deodhai's communication protocol */
	AuIPCPostBoxInitialise();

	/* check the fixup table */
	__bordoisila_fixup_table_check();

#ifdef __TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__
	AuTextOut("[aurora]: Board model : Toradex imx8M Plus Verdin Dahlia (Xeneva Build %s - %s)\r\n",
			  __DATE__,
			  __TIME__);
#elif __TARGET_BOARD_RPI3__
	AuTextOut(
		"[aurora]: Board Model : Raspberry Pi 3B+ (Xeneva Build %s - %s)\r\n", __DATE__, __TIME__);
#elif __TARGET_BOARD_QEMU_VIRT__
	UARTDebugOut(
		"[aurora]: Board Model : QEMU Virt Machine (Xeneva Build %s - %s)\r\n", __DATE__, __TIME__);
#endif
	UARTDebugOut("[aurora]: starting xeneva (ARM64) please wait...\r\n");

	/* clear out the lower half memory */
	AuVmmngrBootFree();
	AuMmngrFileCacheEnable();
	_BlogBypassAuConsole();

	UARTDebugOut("[aurora]: boot freed up \r\n");

	AuSchedulerInitialize();
	//AuBtInitialize();
	//AuEDFSelfTestStart();
#ifdef __XENEVA_SOAK__
	AuSoakStart();
#endif

	AuProcess* proc = AuCreateProcessSlot(0, "exec");
	int num_args = 1;
	char* about = (char*)kmalloc(strlen("-about") + 1);
	strcpy(about, "-about");
	char** argvs = (char**)kmalloc(num_args * sizeof(char*));
	memset(argvs, 0, num_args * sizeof(char*));
	argvs[0] = about;

	/** make init process, as root of all */
	CRED_SET_CAP_ROOT(proc);
	CRED_MARK_ROOT(proc);
	AuLoadExecToProcess(proc, "/init.exe", num_args, argvs);

#ifdef __KERNEL_PROFILER_ON__
	PROFILE_END("_AuMain");
#endif
	AuSchedulerStart();
	while (1) {
		//UARTDebugOut("Printing \n");
	}
}
