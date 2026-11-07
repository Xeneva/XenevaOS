/**
* @file dtb.c
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

#include <dtb.h>
#include <aucon.h>
#include <_null.h>
#include <string.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>

#define FDT_MAGIC	   0xd00dfeed
#define FDT_BEGIN_NODE 0x1
#define FDT_END_NODE   0x2
#define FDT_PROP	   0x3
#define FDT_NOP		   0x4
#define FDT_END		   0x9

uint64_t* dtbAddress;

/**
 * @brief AuDTBSwap32 -- swaps 32 bit value
 * @param from -- value to swap
 */
uint32_t AuDTBSwap32(uint32_t from) {
	uint8_t a = from >> 24;
	uint8_t b = from >> 16;
	uint8_t c = from >> 8;
	uint8_t d = from;
	return (d << 24) | (c << 16) | (b << 8) | a;
}

/**
 * @brief AuDTBSwap64 -- swaps 64 bit value
 * @param from -- value to swap
 */
uint64_t AuDTBSwap64(uint64_t from) {
	uint8_t a = from >> 56;
	uint8_t b = from >> 48;
	uint8_t c = from >> 40;
	uint8_t d = from >> 32;
	uint8_t e = from >> 24;
	uint8_t f = from >> 16;
	uint8_t g = from >> 8;
	uint8_t h = from;
	return ((uint64_t)h << 56) | ((uint64_t)g << 48) | ((uint64_t)f << 40) | ((uint64_t)e << 32) |
		   ((uint64_t)d << 24) | ((uint64_t)c << 16) | ((uint64_t)b << 8) | a;
}

/**
 * @brief AuDTBSwap16 -- swaps 16 bit value
 * @param from -- value to swap
 */
uint16_t AuDTBSwap16(uint16_t from) {
	uint8_t a = from >> 8;
	uint8_t b = from;
	return (b << 8) | a;
}

uint32_t* AuDeviceTreeGetSubNode(uint32_t* node,
								 char* strings,
								 const char* name,
								 uint32_t** node_out,
								 int (*cmp)(const char* a, const char* b)) {
	while (AuDTBSwap32(*node) == 4)
		node++;
	if (AuDTBSwap32(*node) == 9)
		return NULL;
	if (AuDTBSwap32(*node) != 1)
		return NULL;
	node++;

	char* nodename = (char*)node;
	if (cmp((char*)node, name)) {
		*node_out = node;
		return NULL;
	}

	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF))
		node++;
	node++;
	while (1) {
		while (AuDTBSwap32(*node) == 4)
			node++;
		if (AuDTBSwap32(*node) == 2)
			return node + 1;
		if (AuDTBSwap32(*node) == 3) {
			uint32_t len = AuDTBSwap32(node[1]);
			node += 3;
			node += (len + 3) / 4;
		} else if (AuDTBSwap32(*node) == 1) {
			node = AuDeviceTreeGetSubNode(node, strings, name, node_out, cmp);
			if (!node)
				return NULL;
		}
	}
}

uint32_t* AuDeviceTreeFindPropertyInternal(uint32_t* node,
										   char* strings,
										   const char* property,
										   uint32_t** out) {
	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF))
		node++;
	node++;

	while (1) {
		while (AuDTBSwap32(*node) == 4)
			node++;
		if (AuDTBSwap32(*node) == 2)
			return node + 1;
		if (AuDTBSwap32(*node) == 3) {
			uint32_t len = AuDTBSwap32(node[1]);
			uint32_t nameoff = AuDTBSwap32(node[2]);
			if (!strcmp(strings + nameoff, property)) {
				*out = &node[1];
				return NULL;
			}
			node += 3;
			node += (len + 3) / 4;

		} else if (AuDTBSwap32(*node) == 1) {
			node = AuDeviceTreeFindPropertyInternal(node + 1, strings, property, out);
			if (!node)
				return NULL;
		}
	}
}

static int prefix_cmp(const char* a, const char* b) {
	return !memcmp(a, b, strlen(b));
}

/**
 * @brief AuDeviceTreeGetNode -- searches and get the offset of the node
 * @param name -- Node name
 */
uint32_t* AuDeviceTreeGetNode(const char* name) {
	if (!dtbAddress)
		return 0;
	fdt_header_t* fdt = (fdt_header_t*)dtbAddress;
	char* dtb_strings = (char*)((uint64_t)dtbAddress + AuDTBSwap32(fdt->off_dt_strings));
	uint32_t* dtb_struct = (uint32_t*)((uint64_t)dtbAddress + AuDTBSwap32(fdt->off_dt_struct));
	uint32_t* out = NULL;
	AuDeviceTreeGetSubNode(dtb_struct, dtb_strings, name, &out, prefix_cmp);
	return out;
}

/**
 * @brief AuDeviceTreeFindProperty -- searches for a property within a node
 * @param node -- Pointer to node offset
 * @param property -- Property name
 */
uint32_t* AuDeviceTreeFindProperty(uint32_t* node, const char* property) {
	if (!dtbAddress)
		return NULL;
	fdt_header_t* fdt = (fdt_header_t*)dtbAddress;
	char* dtb_strings = (char*)((uint64_t)dtbAddress + AuDTBSwap32(fdt->off_dt_strings));
	uint32_t* out = NULL;
	AuDeviceTreeFindPropertyInternal(node, dtb_strings, property, &out);
	return out;
}

/**
 * @brief AuDeviceTreeGetAddressCells -- get the value from address cells
 * property
 * @param node -- Pointer to node
 */
uint32_t AuDeviceTreeGetAddressCells(uint32_t* node) {
	if (!dtbAddress)
		return 0;
	uint32_t* addressCell = AuDeviceTreeFindProperty(node, "#address-cells");
	if (addressCell) {
		uint32_t addressSize = AuDTBSwap32(addressCell[2]);
		return addressSize;
	}
	return 0;
}

/**
 * @brief AuDeviceTreeGetSizeCells -- get the value from size cells
 * property
 * @param node -- Pointer to node
 */
uint32_t AuDeviceTreeGetSizeCells(uint32_t* node) {
	if (!dtbAddress)
		return 0;
	uint32_t* sizeCell = AuDeviceTreeFindProperty(node, "#size-cells");
	if (sizeCell) {
		uint32_t sizeSize = AuDTBSwap32(sizeCell[2]);
		return sizeSize;
	}
	return 0;
}

static inline uint32_t _swap32(uint32_t x) {
	return ((x & 0x000000FFU) << 24) | ((x & 0x0000FF00U) << 8) | ((x & 0x00FF0000U) >> 8) |
		   ((x & 0xFF000000U) >> 24);
}
/**
 * @brief AuDeviceTreeGetRegAddress -- get the MMIO address from reg property
 * @param node -- Pointer to parent node
 * @param addressCell -- size of address cell
 */
uint64_t AuDeviceTreeGetRegAddress(uint32_t* node, uint32_t addressCell) {
	uint32_t* reg = AuDeviceTreeFindProperty(node, "reg");
	if (reg) {
		uint64_t addr = 0;
		for (int i = 0; i < addressCell; i++) {
			addr <<= 32;
			addr |= AuDTBSwap32(reg[i + 2]);
		}
		return addr;
	}
	return 0;
}

/**
 * @brief AuDeviceTreeGetRegSize -- get the size value from reg property
 * @param node -- Pointer to parent node
 * @param addressCell -- size of address cell
 * @param sizeCell -- value of size cell
 */
uint64_t AuDeviceTreeGetRegSize(uint32_t* node, uint32_t addressCell, uint32_t sizeCell) {
	uint32_t* reg = AuDeviceTreeFindProperty(node, "reg");
	if (reg) {
		uint64_t size = 0;
		for (int i = 0; i < sizeCell; i++) {
			size <<= 32;
			size |= AuDTBSwap32(reg[2 + addressCell + i]);
		}
		return size;
	}
	return 0;
}

uint32_t* AuDeviceTreeGetPropCells(uint32_t* node, const char* property, uint32_t* out_num_cells) {
	uint32_t* prop = AuDeviceTreeFindProperty(node, property);
	if (!prop) {
		*out_num_cells = 0;
		return NULL;
	}
	uint32_t len_bytes = AuDTBSwap32(prop[0]);
	*out_num_cells = len_bytes / 4;
	return &prop[2];
}

uint32_t AuDeviceTreeGetU32Property(uint32_t* node, const char* property, uint32_t default_val) {
	if (!node)
		return default_val;
	uint32_t* prop = AuDeviceTreeFindProperty(node, property);
	if (!prop)
		return default_val;
	return AuDTBSwap32(prop[2]);
}

/**
 * @brief AuDeviceTreeGetStringProperty -- returns the string value of
 * a property, or NULL if the node doesnt have it
 * @param node -- Pointer to node
 * @param property -- Property name
 */
const char* AuDeviceTreeGetStringProperty(uint32_t* node, const char* property) {
	if (!node)
		return NULL;
	uint32_t* prop = AuDeviceTreeFindProperty(node, property);
	if (!prop)
		return NULL;
	if (AuDTBSwap32(prop[0]) == 0)
		return NULL;
	return (const char*)&prop[2];
}

/**
 * @brief AuDeviceTreeGetChildNode -- returns the index-th direct child
 * of a node, grand children are skipped
 * @param node -- Pointer to parent node (as returned by AuDeviceTreeGetNode)
 * @param index -- zero based child index
 */
uint32_t* AuDeviceTreeGetChildNode(uint32_t* node, uint32_t index) {
	if (!dtbAddress || !node)
		return NULL;

	uint32_t depth = 0;
	uint32_t count = 0;

	/* skip the parent's own name */
	node += (strlen((char*)node) + 4) / 4;

	while (1) {
		uint32_t tag = AuDTBSwap32(*node);
		if (tag == FDT_NOP) {
			node++;
		} else if (tag == FDT_PROP) {
			uint32_t len = AuDTBSwap32(node[1]);
			node += 3;
			node += (len + 3) / 4;
		} else if (tag == FDT_BEGIN_NODE) {
			node++;
			if (depth == 0) {
				if (count == index)
					return node;
				count++;
			}
			depth++;
			node += (strlen((char*)node) + 4) / 4;
		} else if (tag == FDT_END_NODE) {
			/* end of the parent itself */
			if (depth == 0)
				return NULL;
			depth--;
			node++;
		} else {
			return NULL;
		}
	}
}

/**
 * @brief AuDeviceTreeInitialize -- initialize the device tree
 * @param fdt_address -- device tree address passed by
 * bootloader
 */
void AuDeviceTreeInitialize(KERNEL_BOOT_INFO* info) {
	void* fdt_address = NULL;
	if (info->boot_type == BOOT_LITTLEBOOT_ARM64) {
		AuLittleBootProtocol* lb = (AuLittleBootProtocol*)info->driver_entry1;
		if (!lb)
			return;
		fdt_address = (void*)lb->device_tree_base;
	} else if (info->boot_type == BOOT_UEFI_ARM64) {
		fdt_address = info->apcode;
	}

	if (!fdt_address) {
		AuTextOut("Device Tree Blob not found \r\n");
		return;
	}
	fdt_header_t* dtb = (fdt_header_t*)fdt_address;
	if (AuDTBSwap32(dtb->magic) != FDT_MAGIC) {
		AuTextOut("[Aurora]:Device Tree invalid magic \r\n");
		return;
	}
	AuTextOut("DTB Magic : %x \r\n", AuDTBSwap32(dtb->magic));
	dtbAddress = fdt_address;
}

/**
 * @brief AuDeviceTreeMapMMIO -- Maps the physical device tree address
 * to MMIO address range
 */
void AuDeviceTreeMapMMIO() {
	if (!dtbAddress)
		return;
	fdt_header_t* fdt = (fdt_header_t*)dtbAddress;
	int pageCount = AuDTBSwap32(fdt->totalSize) / 0x1000;
	void* mmio = (void*)P2V((uint64_t)dtbAddress); //AuMapMMIO((uint64_t)dtbAddress, pageCount);
	dtbAddress = mmio;
	AuTextOut("[aurora]: Device tree mmio %x mapped with page count : %d \r\n", mmio, pageCount);
}

uint32_t* dump_recursive_(uint32_t* node, int depth);

void AuDeviceTreeDumpAll() {
	fdt_header_t* fdt = (fdt_header_t*)dtbAddress;
	uint32_t* node = (uint32_t*)((uint64_t)dtbAddress + AuDTBSwap32(fdt->off_dt_struct));
	dump_recursive_(node, 0);
}

uint32_t* dump_recursive_(uint32_t* node, int depth) {
	while (AuDTBSwap32(*node) == 4)
		node++;
	if (AuDTBSwap32(*node) != 1)
		return node;
	node++;

	char* name = (char*)node;
	for (int i = 0; i < depth; i++)
		AuTextOut(" ");
	AuTextOut("[aurora]: node: \%s \r\n", name);

	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF))
		node++;
	node++;

	while (1) {
		while (AuDTBSwap32(*node) == 4)
			node++;
		uint32_t tag = AuDTBSwap32(*node);
		if (tag == 2)
			return node + 1;
		if (tag == 9)
			return node;
		if (tag == 3) {
			uint32_t len = AuDTBSwap32(node[1]);
			node += 3;
			node += (len + 3) / 4;
		} else if (tag == 1) {
			node = dump_recursive_(node, depth + 1);
		} else {
			AuTextOut("[aurora]: UNKNOWN TAG %x at node %x - DESYNC \r\n", tag, (void*)node);
			return node;
		}
	}
}

void AuDeviceTreeDumpAllProps(const char* node_name) {
	uint32_t* node = AuDeviceTreeGetNode(node_name);
	if (!node) {
		AuTextOut("[aurora]: AuDeviceTreeDumpAllProps: no node found : %s \r\n", node_name);
		return;
	}

	fdt_header_t* fdt = (fdt_header_t*)dtbAddress;
	char* strings = (char*)((uint64_t)dtbAddress + AuDTBSwap32(fdt->off_dt_strings));

	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF))
		node++;
	node++;

	AuTextOut("[aurora]: properties of \%s: \r\n", node_name);
	while (1) {
		while (AuDTBSwap32(*node) == 4)
			node++;
		uint32_t tag = AuDTBSwap32(*node);
		if (tag == 2 || tag == 9)
			break;
		if (tag == 3) {
			uint32_t len = AuDTBSwap32(node[1]);
			uint32_t nameoff = AuDTBSwap32(node[2]);
			AuTextOut("[aurora]:  \%s\" (len=%d)\r\n", strings + nameoff, len);
			node += 3;
			node += (len + 3) / 4;
		} else {
			break;
		}
	}
}