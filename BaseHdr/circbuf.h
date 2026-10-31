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

#ifndef __CIRC_BUF_H__
#define __CIRC_BUF_H__

#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#include <stddef.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif


typedef struct _circ_buf_ {
	uint8_t* buffer;
	size_t head;
	size_t tail;
	size_t max;
	bool full;
}CircBuffer;


/*
* AuAdvancePointer -- advances the pointer
* of the buffer
* @param cbuf -- Pointer to the circular buffer
*/
extern void AuAdvancePointer(CircBuffer *cbuf);

/*
* AuRetreatPointer -- retreat the pointer of
* the buffer
* @param cbuf -- Pointer to the circular buffer
*/
extern void AuRetreatPointer(CircBuffer *cbuf);

/*
* AuCircBufReset -- reset the entire buffer
* @param cbuf -- Pointer to the circular buffer
*/
extern void AuCircBufReset(CircBuffer *cbuf);

/*
* AuCircBufInitialise -- initialise a new circular buffer
* @param buffer -- Pointer to the actual buffer pointer
* @param sz -- size of the buffer
*/
extern CircBuffer* AuCircBufInitialise(uint8_t* buffer, size_t sz);

/*
* AuCircBufFree -- free a circular buffer
* @param cbuf -- Pointer to the circular buffer
* to free
*/
extern void AuCircBufFree(CircBuffer *cbuf);

/*
* AuCircBufSize -- returns the circular
* buffer size
* @param cbuf -- Pointer to the circular
* buffer size
*/
extern size_t AuCircBufSize(CircBuffer *cbuf);

/*
* AuCircBufCapacity -- returns the circular
* buffer capacity
*/
extern size_t AuCircBufCapacity(CircBuffer *cbuf);

/*
* AuCircBufPutData -- puts a data to circular buffer
* @param cbuf-- Pointer to the circular buffer
* @param data -- data to put
*/
extern void AuCircBufPutData(CircBuffer* cbuf, uint8_t data);

/*
* AuCircBufPut -- puts data onto circular buffer
* @param cbuf -- Pointer to the circular buffer
* @param data -- data to put
*/
extern int AuCircBufPut(CircBuffer* cbuf, uint8_t data);

/*
 * AuCircBufWrite -- bulk write up to 'len' bytes, returns bytes written
 */
extern size_t AuCircBufWrite(CircBuffer* cbuf, const uint8_t* src, size_t len);

/*
 * AuCircBufGet -- gets a data from circular
 * buffer
* @param cbuf -- Pointer to the circular buffer
* @param data -- Pointer to the buffer
* where to put the data
*/
extern int AuCircBufGet(CircBuffer *cbuf, uint8_t *data);

/*
 * AuCircBufRead -- bulk read up to 'len' bytes, returns bytes read
 */
extern size_t AuCircBufRead(CircBuffer* cbuf, uint8_t* dst, size_t len);

/*
 * CircBufEmpty -- checks if the circular
* buffer is empty
* @param cbuf -- Pointer to the circular
* buffer
*/
extern bool CircBufEmpty(CircBuffer *cbuf);

/*
* CircBufFull -- checks if the circular
* buffer is full
* @param cbuf -- Pointer to the circular
* buffer
*/
extern bool CircBufFull(CircBuffer *cbuf);


#endif