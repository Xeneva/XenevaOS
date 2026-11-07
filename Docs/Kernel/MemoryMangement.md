# Memory Management in XenevaOS

Memory Management is the most important part of XenevaOS. It is the process of controlling and coordinating available memory, assigning portions called blocks to various running programs, and deallocating them when they are done. XenevaOS manages memory through two subsystems: _Physical Memory_ and _Virtual Memory_. Both are crucial parts of XenevaOS's memory management subsystem, and many other kernel layers depend heavily on the Physical Memory Manager and the Virtual Memory Manager. The three core components are: 
- Physical Memory Manager
- Virtual Memory Manager
- Shared Memory Manager

## Physical Memory Manager
XenevaOS uses a _'Paging'_ mechanism to manage installed memory. Paging is an efficient memory management scheme that eliminates the need for contiguous allocation of physical memory, thus solving the problem of fitting varying-sized memory chunks onto the backing store. By default, XenevaOS uses 4 KiB (_4096 bytes_) pages. The initialization process of the Physical Memory Manager involves two phases: _The XNLDR phase_ and _The Kernel phase_.<br><br>
- __XNLDR Phase__: XNLDR simply scans the `EFI_MEMORY_DESCRIPTOR` map to skip all non-usable memory areas and uses `EfiConventionalMemory` to manage usable memory using an _incremental dereferenced pointer_. Physical memory allocation in XNLDR involves decrementing the stack pointer used to store `EfiConventionalMemory` blocks and saving the allocated block number inside another pointer called `allocated_stack`. The XNLDR physical memory manager is used until the kernel and its runtime resources are fully loaded. When XNLDR passes control to the kernel, the `allocated_stack` pointer is passed along to ensure the kernel does not overwrite any blocks allocated by XNLDR.
- __Kernel Phase__: The kernel uses a bitmap allocation/deallocation scheme to manage physical memory. It initializes available and usable memory by scanning the memory map provided by XNLDR and locks all memory regions that are unusable. The kernel also locks all memory regions that were used by XNLDR, which are identified via the `allocated_stack` pointer.

## Virtual Memory Manager
The Virtual Memory Manager in XenevaOS involves two phases, similar to the Physical Memory Manager: _The XNLDR phase_ and _The Kernel phase_. XNLDR creates a temporary paging structure and loads the kernel into the higher-half address space. The kernel then creates a new paging structure with additional steps: __Linear Mapping the entire Physical Memory__ and __Clearing the Lower Half section__.<br><br>
__Linear Mapping the entire Physical Memory__ involves mapping physical addresses linearly starting from `0xFFFF800000000000`. For example, if a physical allocation is performed at address `0x11B5000`, its linear virtual address will be `0xFFFF8000011B5000`. This conversion is handled via the special functions `P2V` (Physical to Virtual) and `V2P` (Virtual to Physical). Linear mapping is critical for process management. The lower half of Virtual Memory is reserved for user-space processes, whereas the higher half is reserved for the kernel and its resources, and is shared among all processes.<br><br>
For each process, an entirely new virtual address space is created. The kernel code in the higher half is shared among all processes. The benefit of creating a unique address space for each process is that it prevents processes from accessing each other's memory, ensuring isolation.

__Clearing the Lower Half Section__: Clearing the lower half is only safe if the kernel is using linear addresses for allocating physical memory. For example, if a driver allocates physical memory through the `AuPmmngrAlloc` function, the function must use a linear address to allocate the physical block. When the Virtual Memory Manager initializes itself, it stores whether the memory manager has linearly mapped all physical memory. This helps the Physical Memory Manager decide whether to allocate a physical block from linear or non-linear space. If a physical block is allocated from non-linear space, clearing the lower half would cause an immediate crash.<br>
The `AuVmmngrBootFree` function clears out the lower half section and loads the new paging structure into the CR3 register.

__MMIO Memory Area__: The Virtual Address starting from _0xFFFFFF1000000000_. This region is reserved for Hardware to map its device registers into the virtual address space. This region is also marked as non-cacheable region. The __'AuMapMMIO'__ function is used to map device memories into _MMIO Region_. *Note: Redundant and unused MMIO mappings have been removed from the AArch64 kernel core to reduce translation table walk overhead and minimize boot latency.* 

__The External Functions of Virtual Memory Manager__: External functions in Virtual Memory Manager are marked with _'Ex'_ suffix with their function name. There are only two external function currently available, __AuMapPageEx__ and __AuGetPhysicalAddressEx__.
__External Functions of the Virtual Memory Manager__: External functions in the Virtual Memory Manager are suffixed with `Ex`. There are currently two external functions available: `AuMapPageEx` and `AuGetPhysicalAddressEx`.
- __AuMapPageEx__: In XenevaOS, mapping a physical address to a virtual address from one address space to another is achieved via this function. The destination address space does not need to be switched to. (Switching address spaces involves reloading the control register, i.e., CR3). XenevaOS does not follow the Unix fork-exec model; instead, when a new process is spawned, an entirely new process slot is created from scratch rather than copying the parent process. `AuMapPageEx` is used to map pages into the new process's address space without switching the CPU's active page tables.

- __AuGetPhysicalAddressEx__: Similar to `AuMapPageEx`, this function retrieves the physical address of a virtual address belonging to a different address space. It is used during `AuProcessClean` by the `init` process, which is responsible for cleaning up terminated processes.

## Shared Memory Manager
Between multiple processes, shared memory plays a critical role in low-latency communication. Process A may create a shared memory segment with a specific key and pass that key to Process B. Process B then accesses the segment using that key. The only common resource both processes share is the physical backing memory. Process A creates a fresh segment with a physical block backend, and Process B attaches to it. If the key is already present in the Shared Memory Manager, it simply maps the existing physical addresses to the new process's address space. This allows Process A and Process B to exchange information with minimal overhead.

Shared Memory is highly useful for low-latency IPC. The display server in XenevaOS and all graphical applications depend heavily on the Shared Memory Manager, as does the Audio Server for streaming client audio data.

Shared memory segments are created using `AuCreateSHM`, and virtual pointers to the segment are obtained via `AuSHMObtainMem`.

__AuCreateSHM__: Creates a new shared memory segment's parameters:
- __AuProcess* proc__ -- the current process that wants to create a new shared memory segment
- __uint16_t key__ -- the unique key that will be used by this process or another process to identify this memory segment
- __size_t sz__ -- size of the memory segment.
- __uint8_t flags__ -- any protection flags that will be considered by the shared memory manager while creating shared memory segment for another process with this key

__AuSHMObtainMem__: Obtains a memory from newly created shared memory segment. Its parameters are:
- __AuProcess* proc__ - the same process that created the shared memory segment and wants to obtain a memory from that segment.
- __uint16_t id__ -- Memory Segment id returned by __AuCreateSHM__ function. 
- __void* shmaddr__ -- This should be NULL if the shared memory segment should not be mapped to a fixed virtual address.
- __int shmflg__ -- any protection flags or controlling flags that should be considered by shared memory manager while obtaining memory from the provided shared memory segment. 
