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

#include "littleboot.h"
#include "string.h"
#include "dtb.h"


uint64_t *dtb_;

#define FDT_MAGIC  0xd00dfeed
#define FDT_BEGIN_NODE 0x1
#define FDT_END_NODE   0x2
#define FDT_PROP       0x3
#define FDT_NOP        0x4
#define FDT_END        0x9

/*
 * fdt32_to_cpu -- swaps fdt values
 * @param x -- value to swap
 */
uint32_t fdt32_to_cpu(uint32_t x){
    return __builtin_bswap32(x);
}

uint32_t swizzle(uint32_t from){
    uint8_t a = from >> 24;
    uint8_t b = from >> 16;
    uint8_t c = from >> 8;
    uint8_t d = from;
    return (d << 24) | (c << 16) | (b << 8) | (a);
}

uint64_t swizzle64(uint64_t from){
    uint8_t a = from >> 56;
    uint8_t b = from >> 48;
    uint8_t c = from >> 40;
    uint8_t d = from >> 32;
    uint8_t e = from >> 24;
    uint8_t f = from >> 16;
    uint8_t g = from >> 8;
    uint8_t h = from;
    return ((uint64_t)h << 56) | ((uint64_t)g << 48) |
    ((uint64_t)f << 40) | ((uint64_t)e << 32) | 
    (d << 24) | (c << 16) | (b << 8) | (a);
}

uint16_t swizzle16(uint16_t from){
    uint8_t a = from >> 8;
    uint8_t b = from;
    return (b << 8) | (a);
}

/*
 * CODE copied from Toaru OS aarch64 dtb.c 
 */

uint32_t* find_subnode(uint32_t* node, char* strings, const char* name, uint32_t** node_out, 
int (*cmp)(const char* a, const char* b)){
    while(swizzle(*node) == 4)node++;
    if (swizzle(*node) == 9) return NULL;
    if (swizzle(*node) != 1) return NULL;
    node++; //used uint32_t because as per open firmware spec it should be 4 byte aligned
    
    if (cmp((char*)node, name)){
        *node_out = node;
        return NULL;
    }

    while((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF)) node++;
    node++;

    while(1){
        while(swizzle(*node) == 4) node++;
        if (swizzle(*node) == 2) return node+1;
        if (swizzle(*node) == 3){
            uint32_t len = swizzle(node[1]);
            node += 3;
            node += (len + 3) / 4;
        }else if (swizzle(*node) == 1){
            node = find_subnode(node, strings, name, node_out, cmp);
            if (!node) return NULL;
        }
    }
}

uint32_t * skip_node(uint32_t * node, void (*callback)(uint32_t * child)) {
	while (swizzle(*node) == 4) node++;
	if (swizzle(*node) == 9) return NULL;
	if (swizzle(*node) != 1) return NULL;
	node++;
	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF)) node++;
	node++;
	while (1) {
		while (swizzle(*node) == 4) node++;
		if (swizzle(*node) == 2) return node+1;
		if (swizzle(*node) == 3) {
			uint32_t len = swizzle(node[1]);
			node += 3;
			node += (len + 3) / 4;
		} else if (swizzle(*node) == 1) {
			if (callback) callback(node+1);
			node = skip_node(node, NULL);
			if (!node) return NULL;
		}
	}
}

static int base_cmp(const char *a, const char *b) {
	return !strcmp(a,b);
}

uint32_t * dtb_find_node(const char * name) {
	return find_node_int(name,base_cmp);
}

static int prefix_cmp(const char *a, const char *b) {
	return !memcmp(a,b,strlen(b));
}

uint32_t * dtb_find_node_prefix(const char * name) {
	return find_node_int(name,prefix_cmp);
}

uint32_t* find_node_int(const char* name, int (*comp)(const char*, const char*)){
    if (dtb_ == NULL)
        return 0;
    fdt_header_t* fdt = (fdt_header_t*)dtb_;
    char* dtb_strings = (char*)((uint64_t)dtb_ + swizzle(fdt->off_dt_strings));
    uint32_t* dtb_struct = (uint32_t*)((uint64_t)dtb_ + swizzle(fdt->off_dt_struct));
    uint32_t* out = NULL;
    find_subnode(dtb_struct, dtb_strings, name, &out, comp);
    return out;
}

uint32_t * node_find_property_int(uint32_t * node, char * strings, const char * property, uint32_t ** out) {
	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF)) node++;
	node++;

	while (1) {
		while (swizzle(*node) == 4) node++;
		if (swizzle(*node) == 2) return node+1;
		if (swizzle(*node) == 3) {
			uint32_t len = swizzle(node[1]);
			uint32_t nameoff = swizzle(node[2]);
			if (!strcmp(strings + nameoff, property)) {
				*out = &node[1];
				return NULL;
			}
			node += 3;
			node += (len + 3) / 4;
		} else if (swizzle(*node) == 1) {
			node = node_find_property_int(node+1, strings, property, out);
			if (!node) return NULL;
		}
	}
}


uint32_t * dtb_node_find_property(uint32_t * node, const char * property) {
	uintptr_t addr = (uintptr_t)dtb_;
	fdt_header_t * fdt = (fdt_header_t*)addr;
	char * dtb_strings = (char *)(addr + swizzle(fdt->off_dt_strings));
	uint32_t * out = NULL;
	node_find_property_int(node, dtb_strings, property, &out);
	return out;
}

uint32_t LBDTBPropLength(uint32_t* prop){
    return swizzle(prop[0]);
}

uint64_t LBDTBPropReadCells(uint32_t* prop, int index, int ncells){
    uint64_t value = 0;
    for (int i = 0; i < ncells; i++)
        value = (value << 32) | swizzle(prop[2 + index + i]);
    return value;
}

void LBDTBGetRootCells(int* address_cells, int* size_cells){
    /* defaults from the devicetree spec */
    *address_cells = 2;
    *size_cells = 1;
    uint32_t* root = dtb_find_node_prefix("");
    if (!root)
        return;
    uint32_t* ac = dtb_node_find_property(root, "#address-cells");
    uint32_t* sc = dtb_node_find_property(root, "#size-cells");
    if (ac)
        *address_cells = (int)LBDTBPropReadCells(ac, 0, 1);
    if (sc)
        *size_cells = (int)LBDTBPropReadCells(sc, 0, 1);
}

/*
 * LBDTBGetMemorySize -- gets memory size from
 * device tree blob
 * @param memaddr -- address where to put the physical
 * start address
 * @param physSize -- total size of the physical memory
 */
void LBDTBGetMemorySize(uint64_t *memaddr, size_t *physSize) {
    LBUartPutString("Searching memory node \n");
    uint32_t * memory = dtb_find_node_prefix("memory");
    if (!memory){
        LBUartPutString("DTB no memory node \n");
        return;
    }

    uint32_t *regs = dtb_node_find_property(memory, "reg");
    if (!regs){
        LBUartPutString("DTB: Memory node has no regs \n");
        return;
    }
    /* regs[0] is the property length, the <address size> cells follow
     * (BCM2837: #address-cells = <1>, #size-cells = <1>) */
    int address_cells, size_cells;
    LBDTBGetRootCells(&address_cells, &size_cells);
    uint64_t memaddr_ = LBDTBPropReadCells(regs, 0, address_cells);
    uint64_t memsize_ = LBDTBPropReadCells(regs, address_cells, size_cells);
    LBUartPutString("DTB: Memory node found \n");
    LBUartPrintHex(memaddr_);
    LBUartPrintHex(memsize_);
    *memaddr = memaddr_;
    *physSize = memsize_;
}


/*
 * LBDeviceTreeInitialize -- initialize device tree
 * parser
 * @param dtb -- Pointer to Device tree blob
 */
void LBDeviceTreeInitialize(uint64_t dtb){
    if (dtb == NULL){
        LBUartPutString("No Device Tree found :");
        LBUartPrintHex(dtb);
        return;
    }
    fdt_header_t* fdt = (fdt_header_t*)dtb;
    uint32_t magic = fdt32_to_cpu(fdt->magic);
    if (magic != FDT_MAGIC){
        LBUartPutString("Device Tree invalid magic number \n");
        return;
    }
    LBUartPutString("Device tree magic : %x \n");
    LBUartPrintHex(magic);
    dtb_ = (uint64_t*)dtb;
}

uint32_t LBDTBGetTotalSize(){
    if (!dtb_)
       return 0;
    fdt_header_t* fdt = (fdt_header_t*)dtb_;
    /* FDT header fields are big endian */
    return swizzle(fdt->totalSize);
}