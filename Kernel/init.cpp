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
#include <aurora.h>
#include <aucon.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Mm/shm.h>
#include <Hal/hal.h>
#include <Hal/x86_64_gdt.h>
#include <Hal/x86_64_lowlevel.h>
#include <Hal/x86_64_cpu.h>
#include <Hal/apic.h>
#include <Sync/spinlock.h>
#include <Hal/serial.h>
#include <Hal/pcpu.h>
#include <Mm/kmalloc.h>
#include <Mm/mmap.h>
#include <string.h>
#include <Mm/buddy.h>
#include <ahci.h>
#include <Fs/vfs.h>
#include <Fs/tty.h>
#include <Fs/pipe.h>
#include <Fs/initrd.h>
#include <Drivers/mouse.h>
#include <Drivers/ps2kybrd.h>
#include <Drivers/rtc.h>
#include <Hal/x86_64_sched.h>
#include <process.h>
#include <Fs/Dev/devfs.h>
#include <pe.h>
#include <audrv.h>
#include <loader.h>
#include <Fs/Fat/FatFile.h>
#include <Fs/Fat/FatDir.h>
#include <Sound/sound.h>
#include <Net/aunet.h>
#include <Net/arp.h>
#include <Ipc/postbox.h>
#include <autimer.h>
#include <ftmngr.h>
#include <hashmap.h>
#include <Drivers/usb.h>
#include <Hal/basicacpi.h>

/**========================================
** the main entry routine -- _AuMain
**/

void _AuMain(KERNEL_BOOT_INFO *info) {
	AuConsoleInitialize(info, true);	
	AuPmmngrInitialize(info);
	AuVmmngrInitialize();
	AuHeapInitialize();
	AuHalInitialise(info);
	AuInitialiseSerial();
	AuVFSInitialise();
	AuTextOut("BootDev HID -> %x, UID -> %x, CID -> %x \r\n", info->hid, info->uid, info->cid);
	/* TODO: AHCI, NVMe, USB Mass Storage, ..etc should
	 * be included in boot time driver	
	 */
	AuPS2KybrdInitialize();
	//AuBootDriverInitialise(info);
	AuConsolePostInitialise(info);
	AuInitrdInitialize(info);
	/* Here initialize all legacy bus system
	 * like ps2.... using AuLegacyBusInitialize() */
	AuPS2MouseInitialise();
	AuRTCInitialize();

	/*initialise kernel tty */
	AuTTYInitialise();

	/* initialise the shared mem man */
	AuInitialiseSHMMan();

	SharedMemMapListInitialise();

	/* initialise sound service */
	AuSoundInitialise();

	/* initialise PostBoxIPCManager */
	AuIPCPostBoxInitialise();

	/* initialise aurora timer manager*/
	AuTimerDataInitialise();

	/* initialise pre network service*/
	AuInitialiseNet();

	x64_cli();
	AuSchedulerInitialise();

	/* initialize the usb core subsystem */
	AuUSBSubsystemInit();
	
	/*initialise aurora driver manager*/
	AuDrvMngrInitialize(info);

	/* initialise all application processors*/
	AuHalPostInitialise();

	/* intiialise all system fonts */
	FontManagerInitialise();

	
	AuInitialiseLoader();

	/* make the kernel standalone*/
	AuVmmngrBootFree();

	/* Process initialisation begins here */
	AuStartRootProc();

	AuSchedulerStart();
	for (;;);
}