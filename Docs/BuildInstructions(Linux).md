# AArch64 LLVM/Clang Build on Linux

This guide covers only the AArch64 LLVM compatibility build. Clang cross-compiles the bootloader, kernel, libraries, and applications for the `aarch64-unknown-windows` target so XenevaOS can keep using native PE/COFF binaries without MSVC.

## Prerequisites

For the bootloader and kernel build, install:

- Clang and LLD
- GNU Make and Git
- a checkout of `gnu-efi` for the bootloader headers

Clone `gnu-efi` beside the XenevaOS source directories:

```bash
cd XenevaOS
git clone https://github.com/vathpela/gnu-efi.git
```

The LLVM bootloader consumes the headers from this checkout but does not link `libefi` or `libgnuefi`; do not build `gnu-efi` for this workflow.

## Build the bootloader and kernel

Run the LLVM target explicitly on Linux. The `llvm` rule is currently the first target in both AArch64 Makefiles, so bare `make` also invokes LLVM despite the internal `TOOLCHAIN` variable defaulting to `gcc`. Use the explicit form below in scripts and documentation.

```bash
make -C BootAA64 clean llvm
make -C KernelAA64 clean llvm
```

The build outputs are:

- `BootAA64/Build/EFI/BOOT/BOOTAA64.efi`
- `KernelAA64/KernelAA64.exe`

The Makefiles use `clang`/`clang++` for compilation and Clang's LLD driver for linking. They emit AArch64 PE/COFF directly, so there is no ELF-to-PE conversion step in the LLVM path.

The default board is `qemu_virt`, which defines `__TARGET_BOARD_QEMU_VIRT__`. To select Raspberry Pi 3 instead, pass the same board to both builds:

```bash
make -C BootAA64 clean
make -C BootAA64 TOOLCHAIN=llvm BOARD=rpi3
make -C KernelAA64 clean
make -C KernelAA64 TOOLCHAIN=llvm BOARD=rpi3
```

## Build and run on QEMU

The repository script builds the AArch64 LLVM targets, creates `initrd2.img` and the `fat.img` EFI system image, and starts QEMU:

```bash
./Scripts/Linux/build_and_run_qemu.sh --llvm
```

LLVM is the script's default, so `--llvm` may be omitted. In addition to the compiler prerequisites, this workflow needs `qemu-system-aarch64`, `mkfs.vfat`, Mtools, and AArch64 UEFI firmware (`QEMU_EFI.fd`). The script recognizes the common Arch and Debian/Ubuntu firmware locations. Set `XENEVA_QEMU_FIRMWARE` to use another file.

On a supported Linux distribution, the script can install the selected toolchain and runtime dependencies:

```bash
./Scripts/Linux/build_and_run_qemu.sh --llvm --install-deps
```

Useful options for the LLVM workflow are:

- `--bleed` creates the benchmark-oriented AArch64 LLVM image described below.
- `--direct-scanout` rebuilds user space with an experimental compositor path that draws directly into the GOP framebuffer when its pitch exactly matches the screen width. It works with normal or bleed builds, but may be slower on non-cacheable framebuffer mappings.
- `--apps=APP[,APP...]` selectively compiles and deploys only the specified user-space application(s) (e.g. `--apps=Solitaire`), automatically excluding unlisted apps and NetSurf.
- `--no-netsurf` (deprecated) drops NetSurf from the image. Prefer `--apps` for isolating specific applications.
- `--force-user-apps` rebuilds the AArch64 LLVM user-space libraries and supported applications before packing the ramdisk.
- `--skip-build` skips every compiler invocation and repacks the disk images from the bootloader, kernel, library, and application binaries already present on disk. It is rejected if the recorded user-space profile does not match the requested normal profile.
- `--force-legacy-build` reuses an existing `initrd2.img`.
- `--initrd-size-mb=N` overrides the automatically calculated ramdisk size.
- `--headless` disables the display and bounds the QEMU run with `QEMU_TIMEOUT` (120 seconds by default).
- `--data-size-mb=N` sizes the persistent `data.img` virtio-blk disk (default 256, minimum 64). It is created once and reused, and the guest mounts it at `/data` (FAT label `XENEVADATA`), so files written there survive across runs. `--force-data-disk` recreates it, wiping persisted files; `--no-data-disk` detaches it.

`--direct-scanout` cannot be combined with `--skip-build` or `--force-legacy-build`, because the selected compositor must be rebuilt and packed into a fresh initrd. The script includes direct scanout in the user-space profile stamp so switching it on or off triggers a clean rebuild.

The bootloader's screen-resolution menu waits for the emulated USB or virtio keyboard in an ordinary build. It queries GOP first, marks unavailable resolutions, and lets Up/Down navigation wrap across only supported modes. A normal headless run without injected keyboard input therefore remains a bounded pre-kernel smoke test. Bleed bypasses the menu and supports a complete automated headless boot.

## Benchmark with the bleed build

Use `--bleed` when measuring boot-to-desktop performance on the AArch64 QEMU path:

```bash
./Scripts/Linux/build_and_run_qemu.sh --bleed
```

Bleed is an explicit benchmark and low-memory configuration, not the normal user experience. It forces a fresh LLVM build of user space, defines `__XENEVA_BLEED__` for the bootloader, kernel, graphics library, init, compositor, launcher, and desktop, and enables `-O2` for the EFI bootloader. At runtime it skips the splash and startup sound, removes the fixed startup sleeps between init, DeodhaiXR, XELnch, and Namdapha, and omits the PMM validation/self-test passes after allocator initialization.

The profile boots QEMU with 256 MiB instead of 1 GiB and automatically selects 640×480, which also makes headless boots non-interactive. If that exact GOP mode is unavailable, the loader safely retains the firmware's current mode. Chitralekha draws directly into each shared compositor backbuffer instead of allocating a duplicate client staging buffer; updates use release/acquire publication and a bounded compositor-acknowledgement wait of up to approximately 250 ms before the client may begin its next frame. DeodhaiXR disables glass blur surfaces. Init does not launch the network or audio daemons in this profile, and the font manager preloads only Calibri, Forte, and Consolas. Set `XENEVA_QEMU_MEMORY` to test another memory limit.

The bleed image also omits `Resources/resources/MUSIC`, the unused startup `snd.wav`, `Resources/resources/ARCH_X64`, and the four fonts not preloaded by this profile, producing a 36 MiB AArch64-only initrd by default. This is deliberately kept above the minimum usable size for Xeneva's FAT32 driver; smaller FAT16 images are detected but not currently mounted. Pass `--initrd-size-mb=N` with a value of at least 36 if additional benchmark resources need more room. `--bleed` cannot be combined with `--gcc`, `--skip-build`, or `--force-legacy-build`. The build script records the deployed AArch64 user-space profile under the ignored `Build` directory; changing between normal and bleed through this script automatically triggers a clean user-space rebuild. Manually built or copied binaries are outside that protection. Ordinary builds preserve the interactive resolution menu, 1 GiB QEMU memory, diagnostics, validation, rendering effects, delays, configured service-launch behavior, fonts, and resources.

## Build user space with LLVM

To rebuild the supported AArch64 libraries and applications and place their binaries in `Resources/resources`, use:

```bash
./Scripts/Linux/build_and_run_qemu.sh --llvm --force-user-apps
```

The LLVM helper builds `XEClib`, `Chitralekha`, and the application set listed in `Scripts/Linux/lib/llvm.sh`. This is the maintained full-build entry point; individual component Makefiles also expose `make llvm` for targeted work.

### Selective Application Compilation

To quickly build and test individual applications without compiling the entire user-space suite, pass `--apps=<name[,name...]>`:

```bash
./Scripts/Linux/build_and_run_qemu.sh --apps=Solitaire
```

This compiles only the specified target(s) alongside the mandatory desktop shell (`Init`, `DeodhaiXR`, `Terminal`, `Namdapha`, `XELnch`, and `XEShell`). When assembling `initrd2.img`, the build script dynamically filters `lncher.cnf` so unbuilt or missing applications are excluded from the launcher menu, guaranteeing that only functional applications appear in the UI.

## Generate Clang tooling metadata

Install Bear and Python 3, then generate a compilation database containing the AArch64 LLVM commands. The helper currently depends on terminal-style variables from the Linux script environment, so invoke both files in the same Bash process:

```bash
bash -c 'source Scripts/Linux/lib/enviorment_variables.sh; source Scripts/Linux/gen_compile_commands.sh'
```

The script writes `compile_commands.json` at the repository root for clangd and other Clang-based tooling. Set and export `BEAR_OUTPUT` before the command to choose another output path. Component build failures are reported as warnings, so check the reported entry count before relying on the database.

## Toolchain selection by host

- The AArch64 Makefiles assign `gcc` as the default value of `TOOLCHAIN`, but their current default target is `llvm`; bare `make` therefore invokes LLVM on Linux as well as MSYS2. Use `make TOOLCHAIN=gcc all` when explicitly testing the separate GCC path.
- MSYS2 also assigns LLVM as the toolchain default and reports a targeted error if Clang or LLD is missing. `TOOLCHAIN=gcc` is rejected there.
- `make llvm` is the portable, unambiguous command on both hosts.
