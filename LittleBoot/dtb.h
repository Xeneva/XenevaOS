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

#ifndef __DTB_H__
#define __DTB_H__

#include "littleboot.h"

uint16_t swizzle16(uint16_t from);
uint64_t swizzle64(uint64_t from);
uint32_t swizzle(uint32_t from);

typedef struct _fdt_prop_{
    uint32_t tag;
    uint32_t len;
    uint32_t nameoff;
    uint8_t value[];
}fdt_property_t;

uint32_t * dtb_find_node_prefix(const char * name);
uint32_t* find_node_int(const char* name, int (*comp)(const char*, const char*));
uint32_t * node_find_property_int(uint32_t * node, char * strings, const char * property, uint32_t ** out);
uint32_t * dtb_node_find_property(uint32_t * node, const char * property);
uint32_t* find_subnode(uint32_t* node, char* strings, const char* name, uint32_t** node_out, 
int (*cmp)(const char* a, const char* b));

/*
 * LBDTBGetMemorySize -- gets memory size from
 * device tree blob
 * @param memaddr -- address where to put the physical
 * start address
 * @param physSize -- total size of the physical memory
 */
void LBDTBGetMemorySize(uint64_t *memaddr, size_t *physSize);

/*
 * LBDeviceTreeInitialize -- initialize device tree
 * parser
 * @param dtb -- Pointer to Device tree blob
 */
extern void LBDeviceTreeInitialize(uint64_t dtb);

/*
 * LBDTBGetTotalSize -- returns the
 * total size of FDT
 */
extern uint32_t LBDTBGetTotalSize();

#endif