# XELoader ( _Xeneva dynamic linker and loader_ )

XELoader is a crucial component in XenevaOS, playing a vital role in the process of running programs. XELoader is responsible for loading an executable file into the process's memory, preparing it for execution by loading all the necessary resources for it. XELoader supports dynamic linking by loading only the required libraries and linking them to the executable during runtime. This involves finding the required libraries, mapping them into memory, and resolving the addresses of the executable and the libraries. Some commonly used libraries include _XEClib_ ('The Xeneva C Library') and _Chitralekha_ ('The Xeneva Graphics, Widget and Audio Library').

## What is _Dynamic Linker and Loader_?
The dynamic linker and loader are components of modern operating systems that handle the process of preparing and executing dynamically linked executables (executables that depend on shared libraries). It performs the following tasks:
- Resolves external symbols used by the program at runtime.
- Links the required shared libraries with the executable after it's loaded into memory.
- Loads the program and shared libraries into memory before linking.
- Maps each segment (code, data) of the binary into the process's address space.


## Initialization
Initialization of a dynamically linked program consists of two steps. Whenever a new process slot is created and an executable is loaded into the process slot, the kernel verifies whether the executable is statically or dynamically linked. Statically linked programs are directly executed by the kernel, bypassing XELoader, while dynamically linked programs are passed to XELoader. XELoader itself is a statically linked program. If the kernel finds a dynamically linked program during the loading phase, it immediately dismisses the loading process and starts loading XELoader. The kernel passes the name of the dynamically linked program as an argument, followed by other arguments for the program, to XELoader. Once XELoader gains control, it starts loading the program in user mode during runtime.

## Loading and Linking

Everything inside XELoader is treated as an object. Objects are of two types: _Main Executable_ and _Libraries_. Objects hold information about the binary and its dependencies. XELoader first loads the main executable and extracts the names of the required libraries. It loads all the required libraries as objects and stores them in the object list. During the library loading stage, two steps are performed:
- __Symbol resolution__ is performed by binding symbol references from one library to another. A practical example involves the _Chitralekha_ library, which has many symbol references to _XEClib_ that need to be resolved efficiently. 
- __Relocation__: After loading, XELoader updates the addresses of each library to match the loaded memory address, so that symbol references point to correct memory locations. By default, every executable has a base address of `0x600000` for x86_64 and `0x600000000` for ARM64 in XenevaOS.<br><br>

Finally, XELoader links all loaded libraries to the main executable by performing steps similar to those in the library loading stage. The main object holds the main executable. At this stage, the main _entry point_ is extracted from the main executable and stored in the main object list.

> Note: Whenever you load an executable it gets linked with required dynamic libraries. Whenever another or same executable needs the same library, it doesn't get loaded from the disk rather the it get pointed to the previously loaded dynamic library until a 'data write' to that library happens. If a 'data write' happens the exact copy of that library is created, this is called "Copy-On-Write" algorithm.

## Control Transfer
Transferring control to the main executable is only performed after the loading and linking of the main program and libraries are completed successfully. Any failure during loading or linking results in a program crash. Before transferring control to the program, XELoader updates the signal table of the current process with the Default Signal Handler. Finally, XELoader transfers control to the program by calling the _Main Entry Point_ from the main object list.

## Technical Overview
XELoader uses the PE image format for executables and libraries (meaning it is currently limited to loading PE binaries). ELF format support could be added in the future. XELoader itself is a statically linked binary, meaning it does not depend on shared libraries; everything is built statically inside XELoader. 

There are two levels of loaders built into XenevaOS:
- The kernel-level loader (_Kernel-space static executable loader_): Loads pure static binaries directly (e.g., `init.exe`, `xeldr.exe`).
- The XELoader (_User-space dynamic linker and loader_): Loads dynamically linked binaries (e.g., terminals, GUI applications) and shares libraries using the Copy-On-Write (COW) mechanism.

Whenever XenevaOS encounters the system call _```_KeProcessLoadExec```_ with the executable file path and name, it immediately loads the executable and verifies that it is a PE-formatted executable and not a library. The kernel-level loader is only capable of loading statically linked executable files, not dynamically linked executables or libraries. If it is an executable file, the loader checks if it is dynamically or statically linked. Static executable files are executed directly by the kernel-level loader, which maps each section of the executable to the process's address space and transfers control to the entry point. For dynamically linked files, XELoader acts as an intermediary. The kernel-level loader loads XELoader (_which is statically linked_), populates its argument list with the desired filename and path, and transfers control to it. XELoader then runs in user space, starts loading the executable provided by the kernel, and resolves external symbols by loading and linking the required shared libraries.
For example here are the steps how it works:

| Steps  | Description|
|--------|------------|
| (a) _```_KeProcessLoadExec(proc, "/calc.exe",0,NULL);```_ | The _```_KeProcessLoadExec```_ system call carries a pointer to the newly created process slot and the executable filename.
| (b) _```AuLoadExecToProcess(proc,"/calc.exe",0,NULL);```_ | Gets called in kernel space and receives the parameters from _```_KeProcessLoadExec```_.
| (c) _```strcmp(filename,"exe") == 0```_ | Checks the extension of the filename to see if it ends with '.exe', as the kernel loader is limited to static executables only.
| (d) _```AuVFSNodeReadBlock(fsys,filename,(uint64_t*)V2P((uint64_t)buffer))```_ | Loads the file into the current process's address space.
| (e) _```if(AuPEFileIsDynamicallyLinked(buffer));```_ | Checks if the executable is dynamically linked; if so, the loading of this executable is discarded and XELoader is loaded instead.
| (f) _```AuCreateKthread(AuProcessEntUser,...)```_ | Creates a kernel thread with the entry point set to _```AuProcessEntUser```_, which jumps to the user thread (XELoader in this case).

After the above steps execute successfully, XELoader will have the name of the executable (_```"/calc.exe"```_ in this case) in its argument list. XELoader then loads the executable, resolves external symbols, links required libraries, and starts executing the application.

## Binary Alignment
Binary alignment defines how sections of a binary are organized both in memory and on disk. XenevaOS uses the Portable Executable (PE) format. There are two key alignments considered in XenevaOS: __*SectionAlignment*__ and __*FileAlignment*__.<br><br>
__*SectionAlignment*__ specifies how sections are aligned in memory once the binary is loaded — typically, XenevaOS supports 4096 bytes (4 KiB) on ARM64 and 512 to 4096 bytes on x86 systems. __*FileAlignment*__, on the other hand, determines how sections are aligned within the file on disk — commonly 512 bytes or 4096 bytes. When a PE file is loaded, each section's `VirtualAddress` is rounded up to the nearest multiple of `SectionAlignment`, while its `PointerToRawData` is aligned according to `FileAlignment`. Proper alignment ensures that sections do not overlap and that the loader can efficiently map them into memory with correct page boundaries.

## Memory Mapping
All XenevaOS binaries are loaded at a base address of `0x600000` for x86_64 and `0x600000000` for ARM64, though this rule applies only to statically linked binaries. For dynamically linked binaries or libraries, XELoader modifies the base address to suit the runtime memory map. The system call `_KeMemMap` is used to allocate and map each section of the binary to memory from the user area. XELoader maps sections by considering the binary's __*SectionAlignment*__ and __*FileAlignment*__ values as key factors to ensure proper alignment and access permissions.

