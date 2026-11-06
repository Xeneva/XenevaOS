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

#ifndef __AURORA_H__
#define __AURORA_H__

#include <stdint.h>

#ifdef __GNUC__
#define AU_EXPORT __attribute__((visibility("default")))
#define AU_IMPORT
#define AU_ALIGN(x) __attribute__((aligned(x)))
#else
#define AU_EXPORT	__declspec(dllexport) __attribute__((__noinline__))
#define AU_IMPORT	__declspec(dllimport)
#define AU_ALIGN(x) __declspec(align(x))
#endif

#ifdef __AU_KERNEL__
#define AU_FUNC AU_EXPORT
#else
#define AU_FUNC AU_IMPORT
#endif

#ifdef __cplusplus
#define AU_EXTERN extern "C"
#else
#define AU_EXTERN extern
#endif

typedef uint32_t CapRights;

#ifdef ARCH_X64
#define KERNEL_STACK_LOCATION 0xFFFFFB0000000000
#elif ARCH_ARM64
#define KERNEL_STACK_LOCATION 0xFFFFB00000000000 //0x0000000B00000000
#endif

#define KERNEL_STACK_SIZE 40960 //16384  //16KiB

// macro for llvm compatibility
#ifndef ALIGNED
#if defined(_MSC_VER)
#define ALIGNED(x) __declspec(align(x))
#elif defined(__GNUC__) || defined(__clang__)
#define ALIGNED(x) __attribute__((aligned(x)))
#else
#define ALIGNED(x)
#endif
#endif

/**
 * Kernel specific information
 */

/** operating system identity */
#define XENEVA_OS_NAME "XenevaOS"

//using hardcoded value
#define XENEVA_OS_NODENAME "jajabor-xeneva"

#define XENEVA_KERNEL_NAME "Aurora_Bordoisila"

/* each architectural release may 
 * be in different version/release
 */
#ifdef ARCH_X64
#define XENEVA_KERNEL_VERSION	"1.0.0"
#define XENEVA_KERNEL_RELEASE	"1.0.0"
#define XENEVA_KERNEL_BUILD		"x86_64"
#define XENEVA_KERNEL_BUILD_TAG "jajabor"
/** Target architecture */
#define XENEVA_KERNEL_ARCH "x86_64"
#define XENEVA_KERNEL_MACHINE

#elif ARCH_ARM64
#define XENEVA_KERNEL_VERSION	"1.0.0"
#define XENEVA_KERNEL_RELEASE	"1.0.0"
/** Target architecture */
#define XENEVA_KERNEL_ARCH		"arm64"
#define XENEVA_KERNEL_BUILD		"arm64"
#define XENEVA_KERNEL_BUILD_TAG "jajabor"

/** define more quirks here */
#ifdef __TARGET_BOARD_QEMU_VIRT__
#define XENEVA_KERNEL_MACHINE "aarch64-qemu-virt"
#elif __TARGET_BOARD_RPI3__
#define XENEVA_KERNEL_MACHINE "aarch64_rpi3"
#endif

#elif ARCH_RISCV64
#define XENEVA_KERNEL_VERSION	"1.0.0"
#define XENEVA_KERNEL_RELEASE	"1.0.0"
/** Target architecture */
#define XENEVA_KERNEL_ARCH		"riscv64"
#define XENEVA_KERNEL_BUILD		"riscv64"
#define XENEVA_KERNEL_BUILD_TAG "jajabor"
#define XENEVA_KERNEL_MACHINE
#endif

/** kernel configuration  */
#define XENEVA_KERNEL_SMP	  0
#define XENEVA_KERNEL_PREEMPT 1

typedef struct _lbprotocol_ {
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
	uint64_t physicalStart;
	uint64_t physicalEnd;
	uint64_t numberOfPages;
} AuLittleBootProtocol;

#define BOOT_UEFI_x64		  1
#define BOOT_UEFI_ARM64		  2
#define BOOT_LITTLEBOOT_ARM64 3
/**
* Kernel Boot information structure passed by XNLDR
*/

typedef struct _KERNEL_BOOT_INFO_ {
	/* Boot type either UEFI_BOOT or BIOS_BOOT */
	int boot_type;

	void* allocated_stack;

	uint64_t reserved_mem_count;

	/* map -- UEFI memory map */
	void* map;

	/* descriptor_size -- UEFI memory map descriptor size */
	uint64_t descriptor_size;

	/* mem_map_size -- UEFI memory map size */
	uint64_t mem_map_size;

	/* Install the physical direct map here from the loader itself --axiss */
	uint64_t physical_direct_map_base;
	uint64_t physical_direct_map_size;

	/* graphics_framebuffer -- framebuffer address passed by XNLDR */
	uint32_t* graphics_framebuffer;

	/* fb_size -- framebuffer total size */
	size_t fb_size;

	/* X_Resolution -- Total width of the entire display */
	uint16_t X_Resolution;

	/* Y_Resolution -- Total height of the entire display */
	uint16_t Y_Resolution;

	/* pixels_per_line -- scanline of the current display */
	uint16_t pixels_per_line;

	/* redmask, greenmask, bluemask, resvmask -- color mask */
	uint32_t redmask;
	uint32_t greenmask;
	uint32_t bluemask;
	uint32_t resvmask;

	/* acpi_table_pointer -- acpi base memory map */
	void* acpi_table_pointer;

	/* kernel_size -- total kernel image size */
	size_t kernel_size;

	/* psf_font_data -- screen font address loaded
	by XNLDR to use for debugging purpose */
	uint8_t* psf_font_data;

	/* printf_gui -- character printing function pointer to use
	for debugging purpose provided by XNLDR */
	void (*printf_gui)(const char* text, ...);

	/* unused pointer entries */
	uint8_t* driver_entry1; //OTHER
	uint8_t* driver_entry2; //!NVME
	uint8_t* driver_entry3; //!AHCI
	uint8_t* driver_entry4; //!FLOPPY
	uint8_t* driver_entry5; //!ATA
	uint8_t* driver_entry6; //!USB
	void* apcode;
	uint32_t hid;
	uint32_t uid;
	uint32_t cid;

	/* DesktopOverrideWidth/Height -- requested desktop (virtio-gpu scanout)
	 * size chosen in the loader menu, independent of the firmware GOP mode.
	 * 0 means none: drivers fall back to X_Resolution/Y_Resolution. The
	 * firmware framebuffer geometry above always describes the real GOP
	 * surface, so the early console never runs off it. --axiss */
	uint16_t DesktopOverrideWidth;
	uint16_t DesktopOverrideHeight;
} KERNEL_BOOT_INFO, *PKERNEL_BOOT_INFO;

/*
 * AuGetBootInfoStruc -- return kernel boot information
 */
AU_EXTERN AU_EXPORT KERNEL_BOOT_INFO* AuGetBootInfoStruc();

#endif
