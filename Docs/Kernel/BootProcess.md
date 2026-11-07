## The Boot Process 
Current versions of XenevaOS follow simple boot steps to load the __'Kernel'__. From the beginning, XenevaOS has booted under a UEFI environment, utilizing its own bootloader to prepare all the required steps for the kernel. These steps include setting up the _Memory Map_, _The Allocated Physical Memory Stack_, and _Boot Information Struct_.

## The Memory Map
The Memory Map is a pointer to a buffer allocated by __XNLDR__ which includes all information related to `EFI_MEMORY_DESCRIPTOR`. XenevaOS depends on the EFI-provided Memory Descriptor to obtain all usable _'Physical Memory Blocks'_ while excluding _Non-Usable Memory_ blocks.<br><br>
__Non-Usable Memory Blocks__: Non-usable memory blocks are regions of memory that the operating system does not use for its regular operations. These regions are typically reserved for firmware, hardware, or other special purposes. Key types of non-usable memory blocks in EFI memory descriptors include:
- __EfiReservedMemoryTypes__: This memory is reserved by the firmware.
- __EfiLoaderCode and EfiLoaderData__: These regions are used by the firmware for storing its code and data. These regions are used during the boot process and should not be used by the operating system.
- __EfiBootServicesCode and EfiBootServicesData__: These memory areas are used by UEFI boot services, which are no longer available once the OS has taken control.
- __EfiRuntimeServicesCode and EfiRuntimeServicesData__: These regions are used by UEFI runtime services. Although they remain available after the OS has taken control, they are generally not usable for other purposes.
- __EfiUnusableMemory__: This memory has been detected by the firmware as faulty and should not be used by the OS.
- __EfiACPIReclaimMemory__: Memory that holds ACPI tables and can be reclaimed after the tables have been parsed by the OS.
- __EfiACPIMemoryNVS__: Non-volatile memory used by the firmware for storing ACPI information that should not be used by the OS.
- __EfiMemoryMappedIO and EfiMemoryMappedIOPortSpace__: Memory-mapped I/O regions, including those used for memory mapped I/O devices and ports.
- __Allocated Physical Memory for Kernel Code and Data__: This includes all the memory regions being used by _XNLDR_ to load the kernel and runtime drivers. This region also includes the _'Kernel Stack'_.

The _'Allocated Physical Memory for Kernel Code and Data'_ is passed to the kernel through a dereferenced pointer operation, along with the number of allocated blocks.<br>
```
uint64_t allocated_blk_count = bootinfo->reserved_mem_count; 
uint64_t* allocated_blk_stack = (uint64_t*)bootinfo->allocated_stack;
while(allocated_blk_count){
    uint64_t address = *allocated_blk_stack--;
	AuPmmngrLockPage(address);
	allocated_blk_count--;
}
```
The _'AuPmmngrLockPage'_ function is a kernel Physical Memory Manager function that marks a memory block as unusable.

## Boot Information Struct
XenevaOS has its own format of Boot Information that it expects to be passed as an argument to the main entry of the kernel. The _'Boot Information Struct'_ contains all the information for _Memory Management, UEFI GOP's framebuffer, pointers to ACPI tables, the formatted print function implemented by XNLDR, and pointers to loaded runtime drivers memory areas_. 

```
/**
* Kernel Boot information structure passed by XNLDR
*/
#pragma pack (push,1)
typedef struct _KERNEL_BOOT_INFO_ {
	/* Boot type either UEFI_BOOT or BIOS_BOOT */
	int boot_type;

	void* allocated_stack;

	uint64_t reserved_mem_count;

	/* map -- UEFI memory map */
	void *map;

	/* descriptor_size -- UEFI memory map descriptor size */
	uint64_t descriptor_size;

	/* mem_map_size -- UEFI memory map size */
	uint64_t mem_map_size;

	/* graphics_framebuffer -- framebuffer address passed by XNLDR */
	uint32_t* graphics_framebuffer;

	/* fb_size -- framebuffer total size */
	size_t   fb_size;

	/* X_Resolution -- Total width of the entire display */
	uint16_t  X_Resolution;

	/* Y_Resolution -- Total height of the entire display */
	uint16_t   Y_Resolution;

	/* pixels_per_line -- scanline of the current display */
	uint16_t   pixels_per_line;

	/* redmask, greenmask, bluemask, resvmask -- color mask */
	uint32_t redmask;
	uint32_t greenmask;
	uint32_t bluemask;
	uint32_t resvmask;

	/* acpi_table_pointer -- acpi base memory map */
	void*    acpi_table_pointer;

	/* kernel_size -- total kernel image size */
	size_t   kernel_size;

	/* psf_font_data -- screen font address loaded
	by XNLDR to use for debugging purpose */
	uint8_t* psf_font_data;

	/* printf_gui -- formatted printing function pointer to use
	for debugging purpose provided by XNLDR */
	void(*printf_gui) (const char* text, ...);

	/* unused pointer entries, reserved for runtime drivers */
	uint8_t* driver_entry1;   
	uint8_t *driver_entry2;   
	uint8_t *driver_entry3;   
	uint8_t* driver_entry4;   
	uint8_t* driver_entry5;   
	uint8_t* driver_entry6;  

    /* multiprocessor initialisation code */
	void*    apcode;

    /* boot storage related information */
	uint32_t hid;
	uint32_t uid;
	uint32_t cid;
}KERNEL_BOOT_INFO, *PKERNEL_BOOT_INFO;
#pragma pack(pop)
```
File from _'BaseHdr/aurora.h'_ <br>
Overall, the boot information passed by the bootloader to the kernel is crucial for the successful initialization and operation of the operating system. This information includes various parameters and settings that help the kernel understand the system's hardware and environment. The boot information ensures a smooth transition from the firmware/bootloader to a fully operational operating system. [More information can be found here](AboutKernel.md#technical-details-of-xeneva-kernel-boot-protocol).

