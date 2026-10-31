# Project Architecture (MSVC)
XenevaOS codebase is compatible with both GCC and MSVC toolchain. Although both toochain uses different build style and build artifact but build the same Operating System. This document will try to provide the project dependency, build outputs and importance of one project to build the other. 

By understanding this architecture, developers can:
- Identify the responsibility of each project.
- Understand how projects depend on one another
- Locate the generated build artifacts.
- Modify or extend existing components without breaking the build process

# Kernel vs KernelAA64 
"Kernel" is targeted for x86_64 architecture but "KernelAA64" is targeted for ARM64 architecture. 

# .lib vs .exe/.dll artifact
When a project is built within the solution, Visual Studio typically generates two output artifaces: the executable binary ('.exe'/'.dll') and an import library ('.lib'). <br><br>
For example, the 'KernelAA64' project produces both 'xnkrnl.exe' and 'xnkrnl.lib'. The executable contains the actual kernel image, while the import library provides symbol information required by other projects during the linking stage. <br><br>

Kernel-mode drivers and other components that depend on kernel-exported functions link against 'xnkrnl.lib (or 'Kernel.lib') to resolve external symbols at build time. At runtime, symbol resolution is performed by the XenevaOS kernel loader, which parses the import and export tables of loaded binaries and establishes the required symbol bindings.


# Binary output and their Dependencies table

| Project Name | Build Output | Dependency | Architecture|
|--------------|--------------|-------------|--------|
| Boot         | .efi /.lib   | _None_        | x86_64 |
| BootAA64   | .efi/.lib    | _None_        | ARM64 |
| Kernel       | xnkrnl.exe/Kernel.lib | _None_ | x86_64|
| KernelAA64   | xnkrnl.exe/xnkrnl.lib | _None_ | ARM64|
| AHCI(Driver) | .dll/.lib  | _xnkrnl.lib/Kernel.lib_ | _Multi-architecture_|
| E1000(Driver) | .dll/.lib | _xnkrnl.lib/Kernel.lib_ | _Multi-architecture_|
| All Drivers..  | .dll/.lib | _xnkrnl.lib/Kernel.lib_ | _Multi-architecture_|
| init           | .exe/.lib | _None (static binary)_ | _Multi-architecture_|
| DeodhaiXR      | .exe/.lib | _chlkha.lib/ftype.lib/xecrt.lib/xeclib.lib_| _Multi-architecture_|
|Glimpse | .exe/.lib | _chlkha.lib/ftype.lib/xecrt.lib/xeclib.lib_| _Multi-architecture_|
| Chitralekha(Userspace Library) | .dll/.lib| _xecrt.lib/xeclib.lib_| _Multi-architecture_|
| XEClib(Userspace Clibrary) | .dll/.lib | _None_ | _Multi-architecture_|
| XECrt(Userspace static library) | .lib | _None_ |_Multi-architecture_|

## Static Library vs Dynamic Library in XenevaOS
Both static and dynamic library has important role in XenevaOS runtime environment. Static libraries are mostly used for standalone binary build while Dynamic libraries are used for Dynamically linked binary and can be used as shared library among all binaries that depend on the same library.


