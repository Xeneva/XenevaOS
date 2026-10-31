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

#ifndef __RPI3BP_SPI_H__
#define __RPI3BP_SPI_H__

#include <stdint.h>

/*
 * AuRPI3SPI0Map -- map the spi0 to kernel higher half address
 */
extern void AuRPI3SPI0Map();

/*
 *AuRPI3SPI0Init -- initialize SPI0 of RPI3bp
 */
extern void AuRPI3SPI0Init();

/*
 * AuRPISPITransfer -- transfer a single
 * 1 byte data
 * @param data -- data to transfer
 */
extern void AuRPISPITransfer(uint8_t data);

/*
 * AuRPISPITransferBuffer -- transfer multiple 1 byte
 * aligned buffer data
 * @param data -- pointer to buffer
 * @param len -- Length of total datas
 */
extern void AuRPISPITransferBuffer(const uint8_t* data, uint32_t len);

extern void AuRPISPITransferStart();

extern void AuRPISPITrasnferWrite(uint32_t data);

extern void AuRPISPITransferStop();

extern void AuRPISPIFifoWrite(uint32_t data);
#endif
