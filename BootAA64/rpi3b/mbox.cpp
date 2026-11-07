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

#ifdef __TARGET_BOARD_RPI3__

#include "mbox.h"
#include "../uart0.h"
#include "../gpio.h"

volatile unsigned int __declspec(align(16)) mbox[36];

/**
 * Make a mailbox call. Returns 0 on failure, non-zero on success
 */
int mbox_call(unsigned char ch) {
	unsigned int r = (((unsigned int)((unsigned long)&mbox) & ~0xF) | (ch & 0xF));
	/* wait until we can write to the mailbox */
	do {
	} while (*MBOX_STATUS & MBOX_FULL);
	/* write the address of our message to the mailbox with channel identifier */
	*MBOX_WRITE = r;
	/* now wait for the response */
	while (1) {
		/* is there a response? */
		do {
		} while (*MBOX_STATUS & MBOX_EMPTY);
		/* is it a response to our message? */
		if (r == *MBOX_READ)
			/* is it a valid successful response? */
			return mbox[1] == MBOX_RESPONSE;
	}
	return 0;
}

#ifdef __TARGET_BOARD_RPI3__

void RPI3BUartInit() {
	register uint32_t r;

	/* initialize UART */
	*UART0_CR = 0; // turn off UART0

	/* set up clock for consistent divisor values */
	mbox[0] = 9 * 4;
	mbox[1] = MBOX_REQUEST;
	mbox[2] = MBOX_TAG_SETCLKRATE; // set clock rate
	mbox[3] = 12;
	mbox[4] = 8;
	mbox[5] = 2;	   // UART clock
	mbox[6] = 4000000; // 4Mhz
	mbox[7] = 0;	   // clear turbo
	mbox[8] = MBOX_TAG_LAST;
	mbox_call(MBOX_CH_PROP);

	/* map UART0 to GPIO pins */
	r = *GPFSEL1;
	r &= ~((7 << 12) | (7 << 15)); // gpio14, gpio15
	r |= (4 << 12) | (4 << 15);	   // alt0
	*GPFSEL1 = r;
	*GPPUD = 0; // enable pins 14 and 15
	r = 150;
	while (r--) {}
	*GPPUDCLK0 = (1 << 14) | (1 << 15);
	r = 150;
	while (r--) {}
	*GPPUDCLK0 = 0; // flush GPIO setup

	*UART0_ICR = 0x7FF; // clear interrupts

	*UART0_IBRD = 2; // 115200 baud
	*UART0_FBRD = 0xB;
	*UART0_LCRH = (3 << 5) | (1 << 4);	 // 8n1, enable FIFOs
	*UART0_CR = (1 << 9) | (1 << 8) | 1; // enable Tx, Rx, UART
}

#endif

#endif