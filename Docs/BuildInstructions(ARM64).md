# XenevaOS Build Instructions for AArch64 on Windows

This guide covers the two supported Windows workflows for the AArch64 bootloader and kernel: the Visual Studio/MSVC projects and the standalone LLVM/Clang Makefiles. The LLVM workflow cross-compiles directly to PE/COFF with the `aarch64-unknown-windows` target; it does not require the MSVC compiler.

## Visual Studio 2019 Community edition

Visual Studio 2019 Community Edition is required to open and edit XenevaOS projects. The Community Edition is a free version of Microsoft Visual Studio. You can also use any paid edition of 2019. If a later version of Visual Studio is used, projects must be configured manually to meet compiler version requirements. This is done by going to:
- Open Visual Studio and open the desired solution
- Select the desired Project from Solution Explorer and right click
- ```Project -> Properties -> General -> Platform Toolset```

## LLVM tools used by Visual Studio

The Visual Studio projects use Clang for AArch64 assembly. Install a current LLVM release and add its `bin` directory to `PATH` so Visual Studio can find the tools.

## Standalone LLVM build with MSYS2

The AArch64 Makefiles can build the UEFI bootloader and kernel without opening Visual Studio. Use an MSYS2 environment with `make`, Git, Clang, and LLD available on `PATH`. For example, from a CLANG64 shell:

```bash
pacman -S --needed git make mingw-w64-clang-x86_64-toolchain
```

Use the toolchain package matching the active MSYS2 shell if you use UCRT64, CLANGARM64, or another environment. Reopen the shell after installation, then verify the required tools:

```bash
clang --version
ld.lld --version
make --version
```

The bootloader uses headers from a `gnu-efi` checkout at the repository root. The LLVM build does not link the GNU-EFI libraries, so the checkout does not need to be built first:

```bash
git clone https://github.com/vathpela/gnu-efi.git
make -C BootAA64 clean llvm
make -C KernelAA64 clean llvm
```

MSYS2 sets `MSYSTEM`, so plain `make` also selects LLVM in `BootAA64` and `KernelAA64`. Prefer `make llvm` in scripts and documentation because it is explicit and behaves the same on every host. `TOOLCHAIN=gcc` is rejected under MSYS2 because the required `aarch64-linux-gnu-*` cross-toolchain is not packaged there.

The build produces:

- `BootAA64/Build/EFI/BOOT/BOOTAA64.efi`
- `KernelAA64/KernelAA64.exe`

Both files are native AArch64 PE/COFF images. The default board is QEMU Virt; pass `BOARD=rpi3` to both Make invocations when building for Raspberry Pi 3.

This standalone section builds only the bootloader and kernel. The repository's full user-space deployment and QEMU image orchestration are currently documented for Linux in [AArch64 LLVM/Clang Build on Linux](BuildInstructions(Linux).md), not as an MSYS2 workflow.

## ImDisk as disk imager
The XenevaOS bootloader expects a FAT32 ramdisk image inside the boot partition. A QEMU Virt build opens `initrd2.img`; a Raspberry Pi 3 build made with `BOARD=rpi3` opens `initrd.img` instead. The ramdisk contains the system files at its filesystem root.

The current normal resource tree fits in a 96 MiB image with headroom. Increase the size when adding resources. Bleed images are assembled by the Linux workflow and use a separately enforced 36 MiB minimum.

To create a 96 MiB `initrd2.img` for QEMU Virt, create a batch file with the following contents and run it from Command Prompt:

```bat
@echo off
setlocal

set IMG=initrd2.img
set SIZE_BYTES=100663296

echo [*] Creating 96 MiB blank image: %IMG%
fsutil file createnew %IMG% %SIZE_BYTES%
```

For Raspberry Pi 3, change `IMG` to `initrd.img`. Mount and format the image with ImDisk:

```bat
imdisk -a -f initrd2.img -m M: -o rem
format M: /FS:FAT32 /A:512 /Q /V:BOOTIMG /Y
```

The 512-byte allocation unit keeps a 96 MiB volume above FAT32's minimum cluster count and matches the small-image layout used by the Linux image builder. If creating `initrd.img`, use that filename in the `imdisk` command. Once mounted, the relevant Visual Studio projects can copy their outputs to `M:\` through their post-build steps.

> [!NOTE]
> The MSVC project configurations (e.g., `Boot`, `BootAA64`, `XELoader`) utilize post-build copy scripts to deploy binaries to `M:\`. To prevent build failures on machines without a mounted `M:` drive, these copy steps are defensively guarded using conditional checks (`if exist M:\ ...`). Building the solutions will succeed regardless of whether the drive is mounted.

## QEMU Virt boot-partition structure

The bootloader, kernel, and ramdisk are separate files in the boot partition:

```text
/
├── EFI/
│   ├── BOOT/
│   │   └── BOOTAA64.EFI
│   └── XENEVA/
│       └── xnkrnl.exe
└── initrd2.img
```

For an `rpi3` build, the ramdisk filename in the root is `initrd.img`.

## NOTE
Visual Studio might fail to build if the Platform of each required project is not set to ARM64. This can be verified by right-clicking the project and opening the properties dialog. The Platform combo box must be set to ARM64 in the properties dialog.
