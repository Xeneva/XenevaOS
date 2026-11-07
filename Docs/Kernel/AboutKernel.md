## The XEKernel (_Xeneva Kernel_)

The _XenevaOS Kernel_ is the main component of the entire operating system. It follows a __Hybrid Kernel Design__ (a mixture of monolithic and microkernel architectures). The core functionalities (such as _device drivers, filesystem management, IPC (Inter-Process Communication), scheduling, and low-level networking_) run in kernel space, while some services (such as _Display Management, Network Manager, and Audio Services_) run in user space.

## Core Functionalities
The core functionalities of the kernel are :
- Memory Management
- Device Driver Management
- File System Management
- IPCs
- Scheduling
- Network Management
- Process Management
- Kernel Audio Module
- System Call (_Service Calls_, in Xeneva's language)

## Xeneva Kernel Boot Protocol
The XenevaOS boot protocol defines a simple and structured interface between the bootloader and the kernel, where the kernel expects a pointer to a `KernelBootInfo` structure to be passed as a parameter during boot. From the very beginning, the kernel has been designed to boot from a UEFI environment on both x86_64 and ARM64 architectures (unless a traditional non-UEFI boot method is used on ARM64). The structure contains essential system information such as memory layout, framebuffer configuration, hardware-specific data (_like Device Tree Blobs and pointers to ACPI tables_), and pointers to system files required for successful kernel initialization. By using a well-defined structure, the protocol ensures that the kernel receives all critical information in a consistent format, allowing it to initialize subsystems across various platforms.

### Technical Details of Xeneva Kernel Boot Protocol
Before the bootloader calls the Kernel Entry Point, it must ensure that the kernel is properly loaded into memory and mapped to the ***Kernel Virtual Base Address: `0xFFFFC00000000000`***. A proper stack memory of size 1 MiB (`0x100000`) is also required. The kernel expects the `KernelBootInfo` structure as its first parameter at the Kernel Entry Point. For the x86_64 MSVC calling convention, the first parameter is passed in register ***rcx***, and for the System V AMD64 ABI, it is passed in ***rdi***. For ARM64 (_AAPCS64_), the first parameter is passed in register ***x0***. This register holds the address of the __KernelBootInfo__ structure. A corrupted __KernelBootInfo__ structure or an invalid address will cause the kernel to crash or behave unpredictably. 

### The KernelBootInfo structure:
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

    /* boot storage related informations */  
	uint32_t hid;
	uint32_t uid;
	uint32_t cid;
}KERNEL_BOOT_INFO, *PKERNEL_BOOT_INFO;
#pragma pack(pop)
```

- Here are the descriptions of each field in use

| Field Name | Type | Description |
|------------|------|-------------|
|```boot_type```   | ```int```  | Tells the Kernel from where it's booted. Available values are ```BOOT_UEFI_x64 (1)```, ```BOOT_UEFI_ARM64 (2)``` and ```BOOT_LITTLEBOOT_ARM64 (3)```
| ```allocated_stack``` | ```void*``` | The Bootloader allocates Physical Memory to load the kernel and its required runtime data. This field is a stack pointer holding all allocated physical memory addresses, so that Kernel, while using the _Kernel Physical Memory Manager_, reserves these addresses (i.e., stores them in a safe zone).
| ```reserve_mem_count``` | ```uint64_t``` | It holds the number of pages being allocated by Bootloader for Kernel code and runtime data, i.e., the number of physical addresses stored in _allocated_stack_.
| ```map``` | ```void*``` | Pointer to UEFI memory map. _NOTE: Unused in boot_type ```BOOT_LITTLEBOOT_ARM64```_
| ```descriptor_size``` | ```uint64_t``` | UEFI memory map descriptor size. _NOTE: Unused in boot_type ```BOOT_LITTLEBOOT_ARM64```_
| ```mem_map_size``` | ```uint64_t``` | UEFI memory map size. _NOTE: Unused in boot_type ```BOOT_LITTLEBOOT_ARM64```_
| ```graphics_framebuffer``` | ```uint32_t*```| Pointer to Framebuffer address. If boot_type is ```BOOT_UEFI_x64``` or ```BOOT_UEFI_ARM64``` this field holds the address of GOP's (_Graphics Output Protocol_) framebuffer address. If boot_type is ```BOOT_LITTLEBOOT_ARM64``` this field might be unused; in this case, the Kernel utilizes its Graphics Driver for the framebuffer address, or if LittleBoot implements the Graphics Driver internally, this field is used.
| ```fb_size``` | ```size_t``` | The total size of framebuffer address, i.e., total pages occupied. _NOTE: In `BOOT_LITTLEBOOT_ARM64`, this might be used if LittleBoot implements the Graphics Driver, otherwise it is unused._
| ```X_Resolution``` | ```uint16_t``` | Size of screen in width.
| ```Y_Resolution``` | ```uint16_t``` | Size of screen in height.
| ```pixels_per_line```| ```uint16_t```| The number of pixels displayed horizontally in a single row of the screen.
| ```redmask``` | ```uint32_t``` | Tells which bits in 32-bit pixels correspond to red.
| ```greenmask``` | ```uint32_t```| Tells which bits in 32-bit pixels correspond to green.
| ```bluemask``` | ```uint32_t``` | Tells which bits in 32-bit pixels correspond to blue.
| ```resvmask``` | ```uint32_t```| Tells which bits in 32-bit pixels are reserved.
|```acpi_table_pointer```| ```void*```| Pointer to ACPI tables _xsdp_. _NOTE: Unused in boot_type ```BOOT_LITTLEBOOT_ARM64```_
|```kernel_size``` | `size_t` | Total size of Kernel in bytes
| ```psf_font_data``` | `uint8_t*` | Pointer to PSF font data start. Used only on boot_type _```BOOT_UEFI_x64```_
| ```printf_gui``` | _function pointer_ | Formatted text printing function implemented in Bootloader, used by kernel during early initialization phase. _NOTE: Unused in boot_type ```BOOT_LITTLEBOOT_ARM64```_
|```driver_entry1 - driver_entry6``` | ```uint8_t*``` | Slots available for using as pointer to Kernel Boot Time drivers. During Kernel early initialization phase, it cannot load the boot time drivers from boot storage because boot storage driver itself is a boot time driver. The bootloader is responsible for loading the boot time drivers and mapping them to Kernel virtual address space. ***Boot time drivers are mapped from base address: 0xFFFFC00000400000***. _NOTE: This field can also be used for an `initrd`-like image. If `boot_type` is `BOOT_LITTLEBOOT_ARM64`, `driver_entry1` is used as a pointer to the `LittleBootInfo` structure._
| ```ap_code``` | ```void*``` | Pointer to AP (_Application Processors_) initialization code.
| ```hid``` | ```uint32_t``` | _Unused_
| ```uid``` | ```uint32_t``` | _Unused_
| ```cid``` | ```uint32_t``` | _Unused_







