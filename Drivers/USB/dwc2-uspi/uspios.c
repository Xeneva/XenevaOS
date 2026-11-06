//
// uspios.h
//
//
// USPi - An USB driver for Raspberry Pi written in C
// Copyright (C) 2014-2018  R. Stange <rsta2@o2online.de>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "uspios.h"
#include <Mm/kmalloc.h>
#include <Hal/AA64/aa64cpu.h>
#include <Drivers/uart.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <Hal/AA64/gic.h>
#include <Hal/AA64/aa64cpu.h>
#include <Hal/AA64/aa64lowlevel.h>

void* malloc(unsigned nSize) {
	return kmalloc(nSize);
}

void free(void* pBlock) {
	return kfree(pBlock);
}

void MsDelay(unsigned nMilliSeconds) {
	AA64SleepMS(nMilliSeconds);
}
void usDelay(unsigned nMicroSeconds) {
	AA64SleepUS(nMicroSeconds);
}

void LogWrite(const char* pSource,		// short name of module
	unsigned	   Severity,		// see above
	const char* pMessage, ...) {	// uses printf format options
	UARTDebugOut(pMessage);
}

int SetPowerStateOn(unsigned nDeviceId) {
	AuRPISetPowerState(nDeviceId);
}


static void* inthandler;
static void* pParam_;

void dwc2_interrupt_handler(void* fptr, int spi) {
	mask_irqs();
	TInterruptHandler* pHandler = (TInterruptHandler*)inthandler;
	pHandler(pParam_);
	enable_irqs();
}

// USPi uses USB IRQ 9
void ConnectInterrupt(unsigned nIRQ, TInterruptHandler* pHandler, void* pParam) {
	/** connect irq handler **/
	UARTDebugOut("[dwc2-uspi]: Connecting interrupt : %x ,nIRQ: %d \r\n", pHandler, nIRQ);
	UARTDebugOut("[dwc2-uspi]: pParam : %x \r\n", pParam);
	inthandler = (void*)pHandler;
	pParam_ = pParam;
	GICRegisterSPIHandler(dwc2_interrupt_handler, 9);
	AuRPI3PeripheralIRQEnable(9);
}

// returns the timer handle (hTimer)
unsigned StartKernelTimer(unsigned	        nHzDelay,	// in HZ units (see "system configuration" above)
	TKernelTimerHandler* pHandler,
	void* pParam, void* pContext) {	// handed over to the timer handler
	return 0;
}

void CancelKernelTimer(unsigned hTimer) {

}

// display "assertion failed" message and halt
void uspi_assertion_failed(const char* pExpr, const char* pFile, unsigned nLine) {
	UARTDebugOut("[dwc2-uspi]: assertion failed : %s ,FILE: %s, LINE: %d \r\n", pExpr, pFile, nLine);
}

int GetMACAddress(unsigned char Buffer[6]) {
	return 0;
}

void DebugHexdump(const void* pBuffer, unsigned nBufLen, const char* pSource /* = 0 */) {

}