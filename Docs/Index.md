# Welcome to XenevaOS Documentation

Welcome to the official documentation for **XenevaOS**, a modern operating system built from scratch. This documentation portal provides all the resources needed to understand the OS architecture, compile the source code, set up virtual machines, and contribute to both kernel and user-space development.

Use the Table of Contents below to navigate through the project's documentation.

---

##  Getting Started
- [**Introduction**](Introduction.md) — An overview of XenevaOS, its architecture, and its primary goals.
- [**Contributing Guidelines**](Contributing.md) — Rules and procedures for contributing to the repository.
- [**AI Policy**](AIPolicy.md) — Policy and requirements for AI-assisted contributions and code generation.
- [**Preprocessor Macros**](Development/PreprocessorMacros.md) — Important compiler flags and definitions used across the codebase.

## ️ Build & Setup
- [**Build Instructions (Windows/MSVC)**](BuildInstructions.md) — How to build XenevaOS on Windows using Visual Studio.
- [**AArch64 LLVM Build (Linux)**](BuildInstructions(Linux).md) — Build the AArch64 PE/COFF port with Clang and run it under QEMU.
- [**AArch64 Build (Windows/MSYS2)**](BuildInstructions(ARM64).md) — Build the AArch64 bootloader and kernel with MSVC or LLVM.
- [**Populating Config**](PopulatingConfig.md) — Setting up the bootloader and kernel configuration files.
- [**VM Setup**](VMSetup.md) — Guide to configuring VirtualBox, VMWare, or QEMU manually.
- [**QEMU Automation**](QemuAutomation.md) — Using scripts for automated boot and testing on QEMU.
- [**Serial Terminal Attach**](SerialTerminalAttach.md) — How to attach to UART for low-level kernel debugging.

##  Kernel Internals
- [**About Kernel**](Kernel/AboutKernel.md) — Deep dive into the core architecture of the XenevaOS kernel.
- [**Boot Process**](Kernel/BootProcess.md) — Explanation of the hardware boot flow from UEFI to Kernel main.
- [**Memory Management**](Kernel/MemoryMangement.md) — Details on Physical, Virtual, and Heap memory allocation.
- [**Kernel Services (SCI)**](Kernel/KernelServices.md) — Overview of the System Call Interface (SCI) for user-space bridging.
- [**Virtual File System (VFS)**](Kernel/VirtualFileSystem.md) — Abstraction layer for file access, FAT32, and initrd parsing.
- [**Threads & Processes**](Kernel/Thread.md) — Internal workings of kernel threads and user processes.
- [**Drivers Subsystem**](Kernel/Drivers.md) — Core principles of the XenevaOS hardware driver model.
- [**USB Stack**](Kernel/usb.md) — How USB buses and controllers are initialized.
- [**Bluetooth**](Kernel/Bluetooth.md) — AArch64 host, `/dev/bt0`, bonds, and LE Audio.
- [**Driver Framework**](Kernel/Drivers.md#driver-framework) --- Discusses the Driver framework model of XenevaOS and how it can be used to write Hardware Drivers.
- [**Kernel Networking**](Kernel/Network.md) -- Discusses the networking model and implementation of different protocols in XenevaOS.

##  Development & User-Space
- [**XELoader (Dynamic Linker)**](XELoader.md) — The XenevaOS user-space PE linker and dynamic loading mechanism.
- [**Application Development**](Development/ApplicationDevelopment.md) — Writing user-space applications (GUI, Terminal) for XenevaOS.
- [**Driver Development**](Development/DriverDevelopment.md) — Developing dynamic drivers and interacting with kernel APIs.
- [**Project Architecture(MSVC)**](Development/ProjectArchitecture(MSVC).md)- Discusses various artifacts and project inter dependencies.
- [**DeodhaiXR Guide**](Development/DeodhaiXR-Guide.md) — Complete study guide for the DeodhaiXR window compositor and XR shell: surfaces, the shared-memory atomic door, damage algebra, composition passes, the frame loop, and the three present paths.
- [**Memory System Guide**](Development/MemorySystem-Guide.md) — Complete study guide for the AArch64 memory system: the TLSF kernel heap, its kmalloc wiring layer, the region-aware buddy physical allocator, the verification harness, and the x86 contrast set (bitmap PMM, list heap, `#if 0` buddy draft).
