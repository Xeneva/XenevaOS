# XenevaOS Driver - Development

Driver software is an important part of the operating system, allowing the OS to communicate with a hardware device or software component. Drivers act as translators between the OS and the device or component, enabling them to exchange data and instructions. Drivers run at a high privilege level (the Kernel Land), which grants them direct control over the hardware. In XenevaOS, driver software uses kernel-provided functions to communicate with hardware and prepares the environment for the kernel to control that hardware; both are interdependent.

## Project Configuration
Development of XenevaOS is done purely under a Windows environment. The XenevaOS project uses Visual Studio and its compiler toolchain. ***Visual Studio 2013, 2019, or later*** versions are recommended. **[NOTE]: Before beginning driver development, please make sure to set up the XenevaOS development environment; visit [Build Instructions](../BuildInstructions.md).**<br>
- Get the XenevaOS repository
- From the repository, open the Templates folder and copy ***"MyXenevaDriver.zip*** to ***C:/Users/YourUsername/Documents/Visual Studio YourVersion/Templates/ProjectTemplates***, Replace _YourUsername_ with actual user name of your PC and _YourVersion_ with your Visual Studio version (e.g. 2013, 2019, 2022 ...etc). For example, my Visual Studio version is 2019 and my user name is 'XenevaOS' so the path would look like - ***C:/Users/XenevaOS/Documents/Visual Studio 2019/Templates/ProjectTemplates***.
- Start Visual Studio, and open ***Aurora.sln*** from the repository, or alternatively you can directly open ***Aurora.sln*** file from repository.
- In the solution explorer (_right side of visual studio window_), right click on _"Solution Aurora" -> Add -> New Project_.
- In the New Project Window, _"MyXenevaDriver"_ should appear with Xeneva logo, click on _"MyXenevaDriver"_ and click Next
- Now give a suitable name to your driver project and change the location to _"Drivers"_ folder inside _"Aurora"_ folder from XenevaOS repository, for e.g. _"../Aurora/Drivers"_ then click Create.
- Now a new project should appear in the solution explorer within the _"Solution 'Aurora' "_.
- Now right click on the newly created driver project, from the solution explorer and open properties, ***NewlyCreatedProject -> Properties***
- Now inside Properties window, Goto ***Configuration Properties -> General*** and change the ***Target Name*** to your ***desired name*** _(see [Driver Naming Convention](#driver-naming-convention) for more information)_, click Apply and Ok.
- Finally you can start coding your Xeneva driver.

## Driver Naming Convention
The XenevaOS project includes certain naming conventions. In XenevaOS driver development, naming conventions play a crucial role in supporting backward compatibility. Early versions of XenevaOS supported the FAT32 filesystem with no long file name (LFN) support. XenevaOS driver names are limited to a maximum of 8 characters (with an extra 3 characters reserved for the extension), because the FAT filesystem without LFN support allows a maximum of 11 characters in a filename. _We recommend limiting the driver name to up to 8 characters._

## Dependencies
- XenevaOS base headers 
- Kernel.lib (which is automatically created after building the Kernel 'Aurora')

## Xeneva Driver Types
The current version of XenevaOS supports only kernel-mode drivers. Kernel-mode drivers are divided into two types:
- Boot Time Drivers 
- Runtime Drivers
- USB Class Drivers

You can read detailed information about each driver in [About XenevaOS Driver](../Kernel/Drivers.md)

## Your first Xeneva Driver
Information on Initialization and Unloading of XenevaOS drivers can be found [here](../Kernel/Drivers.md#initialization). Xeneva Driver must implement two basic driver functions:
- ***AuDriverMain*** - The Main entrypoint for the driver
- ***AuDriverUnload*** - Clears all the resources allocated by the driver and unload it from the Kernel

See here for a [simple driver example.](../Kernel/Drivers.md#initialization)

## Memory Management
Memory management is a very important part of XenevaOS driver development. It is the most critical aspect of system stability, as improper handling of memory can lead to severe crashes or unpredictable behavior. Since drivers operate at a low level with direct access to system resources, any mistake in ___Allocation, Deallocation, or Access___ can corrupt kernel memory, causing system instability or even a complete crash. The proper use of physical memory allocation/deallocation and kernel heap allocation/deallocation is recommended. Memory management is divided into three parts:
- _MMIO memory mapping_ that maps hardware's physical address to Kernel virtual memory
- _Kernel Heap memory management_ helps allocate and deallocate small objects.
- _Physical Memory management_ handles direct physical memory allocation, as some hardware does not understand virtual memory. Here, `P2V` and `V2P` functions play an important role, as the kernel is mapped to higher-half memory, and lower memory mappings are cleared before entering user space. The entire physical memory is linearly mapped from `0xFFFF800000000000`.

### MMIO Mappings
Once the system enters user space, physical addresses are not directly accessible. Attempting to access a physical address will cause the system to enter a _Page Fault Exception_. For example, consider `0xFE000000` to be a physical address representing some hardware registers. Writing to this address sends commands to the hardware. Say we write the value `1` to offset `0x04` of physical address `0xFE000000` via `*(0xFE000000 + 0x04) = 1` to reset the hardware. If this write is attempted after XenevaOS has entered user space and user-space services have started, it will cause a _Page Fault Exception_. This happens because before transitioning to the user world, all physical memory mappings are cleared from the lower half of the kernel's address space to make room for user-space mappings. To access this hardware address after the transition, we must use XenevaOS MMIO Mappings.

### _Why do we clear the Lower Half?_
There are two reasons:
- The kernel code and its data are always present in the higher half of the kernel's address space, and the lower half is filled with physical memory mappings during boot. Whenever a new process is created in XenevaOS, it gets a completely new address space. When the process occupies the CPU (i.e., gets executed), the page directory is switched to the process's address space. If the higher half of the process's address space is not populated with kernel code and data, an immediate system crash will occur. To prevent this, whenever a new address space is created, the kernel's higher-half mappings are copied directly to it.
- For security purposes, user-space programs use memory from the lower half. In doing so, if they attempt to write to any hardware area, it will cause the system to enter a _Protection Fault_ or _Abort Exception_.

MMIO regions are mapped using the `AuMapMMIO` function call: `AuMapMMIO(uint64_t physicalAddr, size_t sizeOfTheAddress)`. Once mapped successfully, this function returns the virtual address through which you can read/write to control the hardware.

## Memory Flags Descriptions related to Driver development
| _Flag_ | _Description_ |
|--------|-------------|
| X86_64_PAGING_NO_EXECUTE | Marks the virtual page as non executable | 
| X86_64_PAGING_NO_CACHING | Marks the page as non cacheable [ ***Important for mmio memory mappings*** ]
|X86_64_PAGING_WRITE_THROUGH | Marks the page as write through |
| PTE_ATTR_IDX_0 (__ARM64__)| Marks the page as device memory |
|PTE_ATTR_IDX_1 (__ARM64__) | Marks the page as Normal memory |
| PTE_NORMAL_MEM (__ARM64__) | Same as PTE_ATTR_IDX_1 | 
|PTE_DEVICE_MEM (__ARM64__) | Same as PTE_ATTR_IDX_0 | 
| PTE_KERNEL_NOT_EXECUTABLE (__ARM64__) | Marks the page as non executable |


### X86_64_PAGING_WRITE_THROUGH :
This flag typically refers to the use of write-through caching policy. In a write-through enabled page, data is simultaneously updated in both the cache and the main memory, ensuring data consistency.

## Interrupt Handling
Interrupt handling is the mechanism for responding to external events that require immediate attention from the CPU, such as a USB interrupt transfer or I2C slave interrupt. In XenevaOS, interrupts are handled through MSI/MSI-X signals to the processor on both the x86_64 and ARM64 architectures. Generic interrupt ID allocation through the Device Tree is supported on the ARM64 architecture. Legacy PCI interrupt routing is not supported.

### SPI ID and Interrupt Vector Allocation
In XenevaOS, both terms are similar; the only difference is the architecture. The term SPI (Shared Peripheral Interrupt) is used in the ARM64 architecture, while the term Vector is used in the x86_64 architecture.

| ***Function*** | ***Description*** |
|----------|-------------|
| ``int AuGICAllocateSPI`` | Allocates an SPI ID from the kernel and returns it to the driver. |
| ``void AuGICDeallocateSPI`` | Deallocates a used SPI ID from the kernel and marks it as usable. |
| ``void setvect`` | Used in x86_64 to allocate an interrupt number within the IDT table. |
| ``bool AuPCIEAllocMSI`` | Allocates MSI/MSI-X with the given SPI/Vector number. *NOTE: This function automatically allocates MSI or MSI-X for a particular device through its PCI configuration.* |

### Interrupt Service Routine (ISR)
In general definition, Interrupt Service Routine (ISR) is a piece of software that runs when an interrupt signal is received, causing the processor to pause its current task to handle an urgent event. 



## ***TODO***
_Section describing PCIe management, interrupt handling and synchronization, Memory Management, and Kernel resource allocation will be available soon_



