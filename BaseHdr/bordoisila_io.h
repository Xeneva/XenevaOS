/**
* @file bordoisila_io.h
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

#ifndef __BORDOISILA_IO_H__
#define __BORDOISILA_IO_H__

#include <stdint.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <bordoisila_bits.h>
#include <Board/board.h>

#define _bordoisila_readb(addr)  (*(volatile uint8_t*)(addr))
#define _bordoisila_writeb(val, addr) \
 (*(volatile uint8_t*)(addr) = (val));\
  dsb_sy_barrier(); \
  dsb_ish();\
  isb_flush();

#define _bordoisila_readw(addr) (*(volatile uint16_t*)(addr))
#define _bordoisila_writew(addr) \
  (*(volatile uint16_t*)(addr) = (val));\
  dsb_sy_barrier(); \
  dsb_ish();\
  isb_flush();

#define _bordoisila_readl(addr) (*(volatile uint32_t*)(addr))
#define _bordoisila_writel(val,addr) (*(volatile uint32_t*)(addr) = (val));\
  dsb_sy_barrier(); \
  dsb_ish();\
  isb_flush();


static inline int _bordoisila_readl_poll_timeout(uintptr_t addr, uint32_t* val, 
	uint32_t sleep_us, uint32_t timeout_us) {
	uint32_t elapsed = 0;
	volatile uint32_t* reg = (volatile uint32_t*)addr;
	for (;;) {
		*val = *reg;
		if (timeout_us && elapsed >= timeout_us) {
			return -1;
		}

		if (sleep_us) {
			AuAA64BoardSleepUS(sleep_us);
			elapsed += sleep_us;
		}
		else {
			elapsed++;
		}	
	}
}

static inline void _bordoisila_update_bits(uintptr_t addr, uint32_t mask, uint32_t val) {
	uint32_t current_val = *(volatile uint32_t*)addr;
	current_val &= ~mask;
	current_val |= (val & mask);
	*(volatile uint32_t*)addr = current_val;
}


#define _bordoisila_read_poll_timeout(addr, val, cond, sleep_us, timeout_us) ({\
     uint32_t __timeout_us = (timeout_us);\
     uint32_t __elapsed_us = 0; \
     int __ret = 0; \
     while(1){ \
         (val) = *(volatile uint32_t*)(addr);\
         if (cond)\
            break;\
         if (__elapsed_us >= __timeout_us) {\
             __ret = -1; \
             break;\
         } \
         if (sleep_us) { \
             AuAA64BoardSleepUS(sleep_us); \
             __elapsed_us += (sleep_us); \
         }else {\
             __elapsed_us++; \
         }\
     }\
     __ret; \
})



#endif
