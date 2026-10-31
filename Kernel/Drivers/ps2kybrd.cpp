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

#include <Hal/hal.h>
#include <Hal/x86_64_cpu.h>
#include <Hal/x86_64_hal.h>
#include <Hal/x86_64_signal.h>
#include <Fs/vfs.h>
#include <Mm/kmalloc.h>
#include <Fs/dev/devfs.h>
#include <string.h>
#include <Hal/serial.h>
#include <aucon.h>
#include <Fs/Dev/devinput.h>
#include <Drivers/ps2kybrd.h>


/*
 * AuPS2KybrdHandler -- ps2 keyboad handler
 */
void AuPS2KybrdHandler(size_t v, void* p) {
	if (x64_inportb(0x64) & 1) {
		int code = x64_inportb(0x60);

		/* for testing purpose lets send 
		 * a signal to deodhai thread which is
		 * in thread id 4, in a hacky way
		 */
		SeTextOut("Keyboard code : %x \r\n", code);
		AuInputMessage msg;
		memset(&msg, 0, sizeof(AuInputMessage));
		msg.type = AU_INPUT_KEYBOARD;
		msg.code = code;
		AuDevWriteKybrd(&msg);
		
	}
	AuInterruptEnd(1);
}

/*
 * AuPS2KybrdInitialize -- initialise the
 * ps2 kybrd 
 */
void AuPS2KybrdInitialize() {
	AuHalRegisterIRQ(1, AuPS2KybrdHandler, 1, false);

	x64_outportb(0xF0, 1);
	/* start the registration process */
	AuVFSNode* fs = AuVFSFind("/dev");
	AuDevFSCreateFile(fs, "/ps2kybrd", FS_FLAG_DEVICE);
	AuVFSNode* kybrd = AuDevFSOpen(fs, "/dev/ps2kybrd");
}
