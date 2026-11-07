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

#ifndef __AURORA_H__
#define __AURORA_H__

#include "littleboot.h"

/*
 * The kernel boot info, that will be passed
 * to the main kernel file
 * NOTE: the structure is similar to x86_64 build and
 * arm64 build. This structure was designed only for
 * UEFI based boot protocol. But LittleBoot doesn't
 * use UEFI, so it has to somehow prepare the same structure
 * and passed to the kernel, because kernel was already
 * build from ground up with UEFI in mind. Most of the
 * fields are not used though, but little boot uses
 * its own structure for the kernel and pass it through
 * the main structure, kernel can recognise little boot
 * structure
 */

 #define BOOT_UEFI_X64  1
 #define BOOT_UEFI_ARM64 2
 #define BOOT_LITTLEBOOT_ARM64 3  //we'll use this

 typedef struct _lbprotocol_{
    uint64_t initrd_start;
    uint64_t initrd_end;
    uint64_t device_tree_base;
    uint64_t device_tree_end;
    uint64_t device_tree_sz;
    /* we should take care of
     * little boot binary occupied
     * memory area also, becasuse
     * upto the main kernel becomes
     * standalone, we can't neglect
     * little boot code. because memory
     * map data structure, and paging
     * data structures are statically
     * allocated within this region
     */
    uint64_t littleBootStart;
    uint64_t littleBootEnd;

    /* usable memory map describes
     * usable memory areas*/
    void* usable_memory_map;
    int usable_region_count;
    uint64_t physical_start;
    uint64_t physical_end;
    uint64_t num_page_count;
 }LittleBootProtocol;

typedef struct _kebootinfo_{
    int boot_type;
    void* allocatedStack;
    uint64_t reserved_mem_count;
    void* map;
    uint64_t descriptor_size;
    uint64_t mem_map_size;
    uint32_t* graphics_framebuffer;
    size_t fb_size;
    uint16_t XResolution;
    uint16_t YResolution;
    uint16_t pixels_per_line;
    uint32_t redmask;
    uint32_t greenmask;
    uint32_t bluemask;
    uint32_t resvmask;
    void* acpi_table_ptr;
    size_t kernel_size;
    uint8_t* psf_font_data;
    void(*printf_gui)(const char*text, ...);
    uint8_t* driver_entry1;
    uint8_t* driver_entry2;
    uint8_t* driver_entry3;
    uint8_t* driver_entry4;
    uint8_t* driver_entry5;
    uint8_t* driver_entry6;
    void* apcode;
    uint32_t hid;
    uint32_t uid;
    uint32_t cid;
} KERNEL_BOOT_INFO, *PKERNEL_BOOT_INFO;

#endif