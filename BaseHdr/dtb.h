/**
* @file dtb.h
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

#ifndef __AA64_DTB_H__
#define __AA64_DTB_H__

#include <stdint.h>
#include <aurora.h>

typedef struct {
    uint32_t magic;
    uint32_t totalSize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
}fdt_header_t;

typedef struct _fdt_prop_ {
    uint32_t tag;
    uint32_t len;
    uint32_t nameoff;
    uint8_t value[];
}fdt_property_t;

/**
 * @brief AuDTBSwap32 -- swaps 32 bit value
 * @param from -- value to swap
 */
AU_EXTERN AU_EXPORT uint32_t AuDTBSwap32(uint32_t from);


/**
 * @brief AuDTBSwap64 -- swaps 64 bit value
 * @param from -- value to swap
 */
AU_EXTERN AU_EXPORT uint64_t AuDTBSwap64(uint64_t from);

/**
 * @brief AuDTBSwap16 -- swaps 16 bit value
 * @param from -- value to swap
 */
AU_EXTERN AU_EXPORT uint16_t AuDTBSwap16(uint16_t from);
/**
 * @brief AuDeviceTreeFindProperty -- searches for a property within a node
 * @param node -- Pointer to node offset
 * @param property -- Property name
 */
AU_EXTERN AU_EXPORT uint32_t* AuDeviceTreeFindProperty(uint32_t* node, const char* property);

/**
 * @brief AuDeviceTreeGetNode -- searches and get the offset of the node
 * @param name -- Node name
 */
AU_EXTERN AU_EXPORT uint32_t* AuDeviceTreeGetNode(const char* name);

/**
 * @brief AuDeviceTreeGetAddressCells -- get the value from address cells
 * property
 * @param node -- Pointer to node
 */
AU_EXTERN AU_EXPORT uint32_t AuDeviceTreeGetAddressCells(uint32_t* node);

/**
 * @brief AuDeviceTreeGetSizeCells -- get the value from size cells
 * property
 * @param node -- Pointer to node
 */
AU_EXTERN AU_EXPORT uint32_t AuDeviceTreeGetSizeCells(uint32_t* node);

/**
 * @brief AuDeviceTreeGetRegAddress -- get the MMIO address from reg property
 * @param node -- Pointer to parent node
 * @param addressCell -- size of address cell
 */
AU_EXTERN AU_EXPORT uint64_t AuDeviceTreeGetRegAddress(uint32_t* node, uint32_t addressCell);

/**
 * @brief AuDeviceTreeGetRegSize -- get the size value from reg property
 * @param node -- Pointer to parent node
 * @param addressCell -- size of address cell
 * @param sizeCell -- value of size cell
 */
AU_EXTERN AU_EXPORT uint64_t AuDeviceTreeGetRegSize(uint32_t* node, uint32_t addressCell, uint32_t sizeCell);

AU_EXTERN AU_EXPORT uint32_t AuDeviceTreeGetU32Property(uint32_t* node, const char* property, uint32_t default_val);

AU_EXTERN AU_EXPORT uint32_t* AuDeviceTreeGetPropCells(uint32_t* node, const char* property, uint32_t* out_num_cells);

/**
 * @brief AuDeviceTreeGetStringProperty -- returns the string value of
 * a property, or NULL if the node doesnt have it
 * @param node -- Pointer to node
 * @param property -- Property name
 */
AU_EXTERN AU_EXPORT const char* AuDeviceTreeGetStringProperty(uint32_t* node, const char* property);

/**
 * @brief AuDeviceTreeGetChildNode -- returns the index-th direct child
 * of a node, grand children are skipped
 * @param node -- Pointer to parent node (as returned by AuDeviceTreeGetNode)
 * @param index -- zero based child index
 */
AU_EXTERN AU_EXPORT uint32_t* AuDeviceTreeGetChildNode(uint32_t* node, uint32_t index);

AU_EXTERN AU_EXPORT void AuDeviceTreeDumpAll();
AU_EXTERN AU_EXPORT void AuDeviceTreeDumpAllProps(const char* node_name);
/**
 * @brief AuDeviceTreeInitialize -- initialize the device tree
 * @param fdt_address -- device tree address passed by
 * bootloader
 */
extern void AuDeviceTreeInitialize(KERNEL_BOOT_INFO* info);


/**
 * @brief AuDeviceTreeMapMMIO -- Maps the physical device tree address
 * to MMIO address range
 */
extern void AuDeviceTreeMapMMIO();

#endif