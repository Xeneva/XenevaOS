/**
* @file circbuf.c
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

#include <circbuf.h>
#include <Mm/kmalloc.h>
#include <string.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif

/**
 * @brief AuAdvancePointer -- advances the pointer
 * of the buffer
 * @param cbuf -- Pointer to the circular buffer
 */
void AuAdvancePointer(CircBuffer* cbuf) {
	if (cbuf->full)
		cbuf->tail = (cbuf->tail + 1) % cbuf->max;

	cbuf->head = (cbuf->head + 1) % cbuf->max;
	cbuf->full = (cbuf->head == cbuf->tail);
}

/**
 * @brief AuRetreatPointer -- retreat the pointer of
 * the buffer
 * @param cbuf -- Pointer to the circular buffer
 */
void AuRetreatPointer(CircBuffer* cbuf) {
	cbuf->full = false;
	cbuf->tail = (cbuf->tail + 1) % cbuf->max;
}

/**
 * @brief AuCircBufReset -- reset the entire buffer
 * @param cbuf -- Pointer to the circular buffer
 */
void AuCircBufReset(CircBuffer* cbuf) {
	cbuf->head = 0;
	cbuf->tail = 0;
	cbuf->full = false;
}

/**
 * @brief AuCircBufInitialise -- initialise a new circular buffer
 * @param buffer -- Pointer to the actual buffer pointer
 * @param sz -- size of the buffer
 */
CircBuffer* AuCircBufInitialise(uint8_t* buffer, size_t sz) {
	CircBuffer* cbuf = (CircBuffer*)kmalloc(sizeof(CircBuffer));
	if (!cbuf)
		return NULL;
	cbuf->buffer = buffer;
	cbuf->max = sz;
	AuCircBufReset(cbuf);
	return cbuf;
}

/**
 * @brief AuCircBufFree -- free a circular buffer
 * @param cbuf -- Pointer to the circular buffer
 * to free
 */
void AuCircBufFree(CircBuffer* cbuf) {
	kfree(cbuf);
}

/**
 * @brief AuCircBufSize -- returns the circular
 * buffer size
 * @param cbuf -- Pointer to the circular
 * buffer size
 */
size_t AuCircBufSize(CircBuffer* cbuf) {
	size_t sz = cbuf->max;

	if (!cbuf->full) {
		if (cbuf->head >= cbuf->tail)
			sz = (cbuf->head - cbuf->tail);
		else
			sz = (cbuf->max + cbuf->head - cbuf->tail);
	}
	return sz;
}

/**
 * @brief AuCircBufCapacity -- returns the circular
 * buffer capacity
 */
size_t AuCircBufCapacity(CircBuffer* cbuf) {
	return cbuf->max;
}

/**
 * @brief AuCircBufPutData -- puts a data to circular buffer
 * @param cbuf-- Pointer to the circular buffer
 * @param data -- data to put
 */
void AuCircBufPutData(CircBuffer* cbuf, uint8_t data) {
	cbuf->buffer[cbuf->head] = data;
	AuAdvancePointer(cbuf);
}

/**
 * @brief AuCircBufPut -- puts data onto circular buffer
 * @param cbuf -- Pointer to the circular buffer
 * @param data -- data to put
 */
int AuCircBufPut(CircBuffer* cbuf, uint8_t data) {
	int r = -1;
	if (!CircBufFull(cbuf)) {
		cbuf->buffer[cbuf->head] = data;
		AuAdvancePointer(cbuf);
		r = 0;
	}

	return r;
}

size_t AuCircBufWrite(CircBuffer* cbuf, const uint8_t* src, size_t len) {
	size_t written = 0;
	while (written < len && !CircBufFull(cbuf)) {
		size_t avail = cbuf->max - AuCircBufSize(cbuf);
		size_t chunk = len - written;
		if (chunk > avail)
			chunk = avail;
		if (chunk == 0)
			break;
		size_t run = cbuf->max - cbuf->head;
		if (chunk > run) {
			memcpy(cbuf->buffer + cbuf->head, src + written, run);
			memcpy(cbuf->buffer, src + written + run, chunk - run);
		} else {
			memcpy(cbuf->buffer + cbuf->head, src + written, chunk);
		}
		cbuf->head = (cbuf->head + chunk) % cbuf->max;
		cbuf->full = (cbuf->head == cbuf->tail);
		written += chunk;
	}
	return written;
}

/**
 * @brief AuCircBufGet -- gets a data from circular
 * buffer
 * @param cbuf -- Pointer to the circular buffer
 * @param data -- Pointer to the buffer
 * where to put the data
 */
int AuCircBufGet(CircBuffer* cbuf, uint8_t* data) {
	int r = -1;

	if (!CircBufEmpty(cbuf)) {
		*data = cbuf->buffer[cbuf->tail];
		AuRetreatPointer(cbuf);
		r = 0;
	}
	return r;
}

size_t AuCircBufRead(CircBuffer* cbuf, uint8_t* dst, size_t len) {
	size_t got = 0;
	while (got < len && !CircBufEmpty(cbuf)) {
		size_t avail = AuCircBufSize(cbuf);
		size_t chunk = len - got;
		if (chunk > avail)
			chunk = avail;
		if (chunk == 0)
			break;
		size_t run = cbuf->max - cbuf->tail;
		if (chunk > run) {
			memcpy(dst + got, cbuf->buffer + cbuf->tail, run);
			memcpy(dst + got + run, cbuf->buffer, chunk - run);
		} else {
			memcpy(dst + got, cbuf->buffer + cbuf->tail, chunk);
		}
		cbuf->tail = (cbuf->tail + chunk) % cbuf->max;
		cbuf->full = false;
		got += chunk;
	}
	return got;
}

/**
 * @brief CircBufEmpty -- checks if the circular
 * buffer is empty
 * @param cbuf -- Pointer to the circular
 * buffer
 */
bool CircBufEmpty(CircBuffer* cbuf) {
	return (!cbuf->full && (cbuf->head == cbuf->tail));
}

/**
 * @brief CircBufFull -- checks if the circular
 * buffer is full
 * @param cbuf -- Pointer to the circular
 * buffer
 */
bool CircBufFull(CircBuffer* cbuf) {
	return cbuf->full;
}