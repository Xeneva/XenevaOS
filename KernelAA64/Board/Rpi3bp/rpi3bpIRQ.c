/**
* @file rpi3bpIRQ.c
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

#ifdef __TARGET_BOARD_RPI3__

#include <Board/RPI3bp/rpi3bp.h>
#include <stdint.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64cpu.h>
#include <aucon.h>
#include <Hal/AA64/gic.h>
#include <Drivers/uart.h>
#include <Drivers/rtcmmio.h>
#include <Hal/AA64/sched.h>
#include <process.h>
#include <_null.h>
#include <Mm/vmarea.h>
#include <Board/RPI3bp/rpi3bp_gpio.h>
#include <timer.h>

bool timer_debug = false;
void settimerdebug() {
	timer_debug = true;
}

extern void XPT2046ReadTouch();
/**
 * @brief RPI3_IRQ_handler -- handle pending IRQs of RPI board
 * @param regs -- Pointer to AA64 register struct
 */
void RPI3_IRQ_handler(AA64Registers* regs) {
	uint32_t pending = AuRPI3LocalIRQGetPending();
	// AuTextOut("IRQ Pending++ \r\n");
	if (pending & INT_SRC_CNTPNSIRQ) {
		uint64_t cnt = get_cntv_ctl_el0();
		uint8_t bit = (cnt >> 2) & 0x1;
		UARTDebugOut("[aurora]:******* rpi3 timer irq++ %d\r\n", bit);
		/* if (bit == 1)
            for (;;);*/
		// suspendTimer();
		// setupTimerIRQ();
		resetTimer();
		/** handle kernel expired kernel timers */
		AuroraTimerTick();
		AuScheduleThread(regs);
	}
	if (pending & INT_SRC_CNTPSIRQ) {
		UARTDebugOut("[aurora]: rpi timer++ \r\n");
	}
	if (pending & INT_SRC_CNTHPIRQ) {
		UARTDebugOut("[aurora]: rpi timer HPIRQ++ \r\n");
	}
	if (pending & (1ULL << 3)) {
		//suspendTimer();
		//setupTimerIRQ();
		resetTimer();
		/** handle kernel's expired timers */
		AuroraTimerTick();
		AuScheduleThread(regs);
	}

	if (pending & (1ULL << 8)) {
		uint32_t penP = AuRPI3PeripheralIRQGetPending2();
		uint32_t penP1 = AuRPI3PeripheralIRQGetPending1();

		if (penP1 & (1ULL << 9)) {
			GICCallSPIHandler(9);
		}
		/* Handle GPIO irq*/
		if (pending & (1ULL << 9)) {
			UARTDebugOut("USB interrupt under GPU IRQ??? \r\n");
		}
		if (penP & (1ULL << 17)) {
			uint32_t gpioEvent = AuRPIGPIOGetEvents();
			/* PI HDMI Display's XPT2046 Penirq*/
			if (gpioEvent & (1ULL << 25)) {
				/* Because matured OS do that, disable GPIO irq then clear
                 * the event bit
                 */
				AuRPIGPIODisableInterrupt(25);
				XPT2046ReadTouch();
				while (AuRPIGPIOPinLevelLow(25))
					AuRPIDelayMS(20);

				AuRPIDelayMS(5);
				AuRPIGPIOClearEvent(25);
				/* Re enable it boss*/
				AuRPIGPIOEnableInterrupt(25);
			}
		}
	}

	if (pending & (1ULL << 9)) {
		UARTDebugOut("[dwc2_otg]: interrupt occured \r\n");
	}
}

#endif