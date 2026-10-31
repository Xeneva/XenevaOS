/**
* @file gic.h
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

#ifndef __GIC_H__
#define __GIC_H__

#include <aa64hcode.h>
#include <stdint.h>
#include <aurora.h>

typedef struct _gic_ {
	uint8_t version;
	uint64_t gicDPhys;
	uint64_t gicCPhys;
	uint64_t gicRPhys;
	uint64_t gicMSIPhys;
	uint64_t gicDMMIO;
	uint64_t gicCMMIO;
	uint64_t gicRMMIO;
	uint64_t gicMSIMMIO;
	uint16_t spiBase;
	uint16_t spiCount;
}GIC;


GIC* AuGetSystemGIC();

/**
 * @brief GICInitialize -- initialize the gic
 * controller, it can be parsed through ACPI MADT
 * or hard-coding
 */
extern void GICInitialize();

/** @briefGICEnableIRQ -- enable an IRQ
 * @param irq -- IRQ number
 */
AU_EXTERN AU_EXPORT void GICEnableIRQ(uint32_t irq);


/** @brief GICEnableIRQ -- enable an SPI IRQ
 * @param irq -- IRQ number
 */
AU_EXTERN AU_EXPORT void GICEnableSPIIRQ(uint32_t irq);


AU_EXTERN AU_EXPORT void GICClearPendingIRQ(uint32_t irq);

/**
 * @brief GICReadIAR -- read interrupt acknowledge
 * register
 */
extern uint32_t GICReadIAR();

extern void GICCheckPending(uint32_t irq);

/* one-line distributor+CPU view for an SPI, see gic.c */
extern void GICDumpSPI(uint32_t irq);

/**
 * @brief AuGICGetMSIAddress -- calculate and return MSI address
 * for given spi offset
 * @param interruptID -- spi offset
 * @return composited MSI address
 */
AU_EXTERN AU_EXPORT uint64_t AuGICGetMSIAddress(int interruptID);

/**
 * @brief AuGICGetMSIData -- obtains composited MSI data address
 * of respected interruptID, useful in PCIe
 * @param interruptID -- interrupt ID of device
 */
AU_EXTERN AU_EXPORT uint32_t AuGICGetMSIData(int interruptID);

/**
 * @brief AuGICAllocateSPI -- allocates Shared Peripheral
 * interrupt ID
 */
AU_EXTERN AU_EXPORT int AuGICAllocateSPI();

/**
 *@brief AuGICDeallocateSPI -- free up an used SPI id
 *@param spiID -- target spi id
 */
AU_EXTERN AU_EXPORT void AuGICDeallocateSPI(int spiID);

/**
 * @brief GICSendEOI --sends end of interrupt to
 * GIC cpu interface
 * @param irqnum -- interrupt ID of the device
 */
AU_EXTERN AU_EXPORT void GICSendEOI(uint32_t irqnum);

extern void GICSetupTimer();

AU_EXTERN AU_EXPORT void GICSetTargetCPU(int spi);

/**
 * @brief GICRegisterSPIHandler -- register a spi handler
 * to callback list
 * @param fptr -- SPI Callback address
 * @param spi -- SPI number
 */
AU_EXTERN AU_EXPORT void GICRegisterSPIHandler(void* fptr, int spi);

/**
 * GICGetSPIHandler -- read back the callback registered for an spi
 * (0 when free). Paired with GICRegisterSPIHandler()'s first-writer-wins
 * behaviour so callers can detect an occupied slot; see gic.c.
 */
AU_EXTERN AU_EXPORT void* GICGetSPIHandler(int spi);

/**
 * GICClearSPIHandler -- release an spi slot (bounds checked).
 */
AU_EXTERN AU_EXPORT void GICClearSPIHandler(int spi);

/**
 * @brief GICCallSPIHandler -- jump to a callback handler
 * associated with given spi number
 * @param spi -- SPI number
 */
extern void GICCallSPIHandler(int spi);

/**
 * @brief GICDisable -- disable the GIC
 */
extern void GICDisable();
#endif
