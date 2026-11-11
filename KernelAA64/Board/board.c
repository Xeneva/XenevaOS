/**
* @file board.c
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
#include <Board/RPI3bp/rpi3bp.h>
#include <Board/imx8mp/imx8mp_clk.h>
#include <Board/imx8mp/imx8mp_pll.h>
#include <Board/imx8mp/imx8mp_clk_gate.h>
#include <Board/imx8mp/imx8mp_blkctrl.h>
#include <Board/imx8mp/imx8mp_gpc.h>
#include <Strings/export_imx8mp.h>
#include <Drivers/uart.h>
#include <Board/board.h>
#include <stdint.h>
#include <aucon.h>
#include <Drivers/virtio.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Log/klog.h>
#include <bordoisila_io.h>
#include <Drivers/res.h>

#ifdef __TARGET_BOARD_QEMU_VIRT__
extern void virt_power_down(uint64_t code);
extern void virt_power_reboot(uint64_t code);
extern uint64_t AuVirtGetBootEpoch();
#elif __TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__ || (__TARGET_BOARD_IMX8MP_SOC__)
/**
 * @brief imx8mp_board_initiailze -- initialize required
 * subsystems of board
 */
extern void imx8mp_board_initialize();
#endif

/**
 * @brief AuAA64BoardInitialize -- initialize board specific data
 */
void AuAA64BoardInitialize() {
	AuTextOut("[aurora]: initializing board data \r\n");
#ifdef __TARGET_BOARD_RPI3__
	AuRPI3Initialize();
#elif __TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__ || (__TARGET_BOARD_IMX8MP_SOC__)
	imx8mp_board_initialize();
#endif
}

/**
 * @brief AuAA64BoardPostSchedulerInitialize -- start board driver threads
 */
void AuAA64BoardPostSchedulerInitialize() {
#ifdef __TARGET_BOARD_RPI3__
	AuRPI3PostSchedulerInitialize();
#endif
}

/**
 * @brief AuAA64BoardSleepUS -- calls board specific sleep function
 */
void AuAA64BoardSleepUS(uint32_t us) {
#ifdef __TARGET_BOARD_RPI3__
	return AuRPI3DelayUS(us);
#endif
	uint64_t freq = get_cntfrq_el0();
	uint64_t ticks = (freq / 1000000ULL) * us;
	uint64_t start = get_cntpct_el0();
	while ((get_cntpct_el0() - start) < ticks) {
		_wfi();
	}
}

/**
 * @brief AuAA64BoardSleepMS -- calls board specific sleep function
 * in ms
 */
void AuAA64BoardSleepMS(uint32_t ms) {
#ifdef __TARGET_BOARD_RPI3__
	AuRPIDelayMS(ms);
	return;
#endif
	AuAA64BoardSleepUS(ms * 1000ULL);
}

/**
 * @brief AuAA64BoardPowerDown -- power down the board
 */
void AuAA64BoardPowerDown() {
#ifdef __TARGET_BOARD_QEMU_VIRT__
	UARTDebugOut("[aurora]: shutting down your system \r\n");
	virt_power_down(0x84000008);
#endif
}

/**
 * @brief AuAA64BoardReboot -- reboot the board
 */
void AuAA64BoardReboot() {
#ifdef __TARGET_BOARD_QEMU_VIRT__
	UARTDebugOut("[aurora]: restarting your system \r\n");
	virt_power_reboot(0x84000009);
#endif
}

/**
 * @brief AuAA64BoardGetBootEpoch -- returns the
 * boot epoch time from board
 */
uint64_t AuAA64BoardGetBootEpoch() {
#ifdef __TARGET_BOARD_QEMU_VIRT__
	return AuVirtGetBootEpoch();
#endif
}