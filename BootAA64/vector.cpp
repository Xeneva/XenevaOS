/**
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

#include "uart0.h"
#include "lowlevel.h"

extern "C" void sync_el1_handler() {
	XEUARTPrint("Sync Exception \r\n");
	uint64_t esr = read_esr_el1();
	uint64_t faultAddr = read_far_el1();
	uint64_t elr = read_elr_el1();

	XEUARTPrint("ESR: %x \r\n", esr);
	XEUARTPrint("Fault address: %x \r\n", faultAddr);
	XEUARTPrint("Instruction: %x \r\n", elr);

	uint32_t dfsc = esr & 0x3F;

	switch (dfsc) {
	case 0b000000:
		XEUARTPrint("address size fault level 0\n");
		break;
	case 0b000001:
		XEUARTPrint("Address size fault level1\n");
		break;
	case 0b000010:
		XEUARTPrint("Address size fault level2\n");
		break;
	case 0b000011:
		XEUARTPrint("Address size fault level 3 \n");
		break;
	case 0b000100:
		XEUARTPrint("translation fault level 0\n");
		break;
	case 0b000101:
		XEUARTPrint("translation fault level 1\n");
		break;
	case 0b000110:
		XEUARTPrint("translation fault level 2\n");
		break;
	case 0b000111:
		XEUARTPrint("translation fault level 3\n");
		break;
	case 0b001001:
		XEUARTPrint("access falg fault 1\n");
		break;
	case 0b001010:
		XEUARTPrint("access falg fault 2\n");
		break;
	case 0b001011:
		XEUARTPrint("access falg fault 3\n");
		break;
	case 0b001101:
		XEUARTPrint("permission fault level 1\n");
		break;
	case 0b001110:
		XEUARTPrint("permission fault level 2\n");
		break;
	case 0b001111:
		XEUARTPrint("permission fault level 3\n");
		break;
	default:
		XEUARTPrint("Unknown fault code \n");
		break;
	}
	while (1)
		;
}

extern "C" void irq_el1_handler() {
	XEUARTPrint("IRQ Exception occured \n");
	while (1)
		;
}

extern "C" char vectors[];

/*
 * XEVectorInstall -- install exception handlers
 */
void XEVectorInstall() {
	write_vbar_el1((uint64_t)&vectors);
	XEUARTPrint("XNLDR exception handlers installed for EL1 \r\n");
}