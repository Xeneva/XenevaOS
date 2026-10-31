# Preprocessor Macros for Kernel

The Kernel project is divided into three categories: __*Kernel, KernelAA64, and KernelRISCV64*__. It is heavily dependent on preprocessor macros to enable or disable specific code blocks during compile time. For example, if the kernel is targeted for a specific hardware SoC, it can be built for that SoC by simply enabling a specific macro. Conflicting macros must not be defined simultaneously; otherwise, the kernel will behave unexpectedly.

### `__TARGET_BOARD_X__` Definitions
 Supported target boards are listed below. Please note that only one board can be defined at a time during build; defining multiple board definitions will cause the kernel to behave unexpectedly:
 - `__TARGET_BOARD_QEMU_VIRT__` : Builds the kernel for qemu virt board
 - `__TARGET_BOARD_RPI3__` : Builds the kernel for Raspberry Pi 3b plus
 - `__TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__` : Builds the kernel targeting Verdin Dahlia board with nxp.imx8mp SoC
 - `__TARGET_BOARD_IMX8MP_SOC__` : Builds the kernel targeting nxp.imx8mp SoC <br><br>

 (_More board supports are work in progress_)

## `_USE_XALLOC` Definitions

The legacy kernel trees contain selection macros for `dlmalloc` and `liballoc`, though only one can be active at a time:

- `_USE_DLMALLOC` — selects Doug Lea's allocator where that allocator is still compiled.
- `_USE_LIBALLOC` — selects liballoc where that allocator is still compiled.

These macros do not select the AArch64 kernel's current heap. `KernelAA64` excludes both legacy implementations and uses TLSF through `Mm/kmalloc.c`.

## `ARCH_X` Definitions
 Architecture-specific definitions are mostly used in header files shared across `x86_64`, `arm64`, and `riscv64` targets:
- `ARCH_X64` - will force the project to use x86_64 targeted code
- `ARCH_ARM64` -- will force the project to use arm64 targeted code <br>

## KERNEL PROFILER definitions
Kernel uses inbuilt profiler to measure the latency values while performing certain tasks. Profiling heavily uses serial output to print profiling results. Profiler can be enabled or disabled as per need. 
- `__KERNEL_PROFILER_ON__` -- will tell the kernel to use profiler, without this kernel will bypass profiling outputs.

## AArch64 benchmark profile

- `__XENEVA_BLEED__` — selects the AArch64 LLVM benchmark and low-memory profile. It removes deliberate boot delays and selected diagnostics, enables the reduced startup configuration, and activates the direct shared compositor-buffer path.
- `__XENEVA_DIRECT_SCANOUT__` — makes DeodhaiXR use the mapped GOP framebuffer as its canvas when the framebuffer is 32-bit and tightly pitched. Select it through `--direct-scanout`; it is independent of bleed and falls back to the cached canvas when the mode is incompatible.
- `__XENEVA_UNIKERNEL__` — one user-space XR shell process: DeodhaiXR links XELnch and Namdapha as threads and does not `LoadExec` them. The kernel, virtio, and other apps stay separate processes. Select it through `--unikernel`. Do not mix unikernel and normal `deodxr.exe` in one image.
- `__XENEVA_STATIC_COMPONENTS__` — kernel-side unikernel profile: the QEMU virtio drivers (`virtnet`, `virtgpu`, `virtsnd`, `virtblk`) are compiled into `KernelAA64.exe` instead of shipping as PE `.dll` files. Each driver is built with `AuDriverMain`/`AuDriverUnload`/`AuDriverFlags` renamed to `<name>`-suffixed symbols so they coexist, and `KernelAA64/staticcomp.c` lists them; `AuDriverLoad` consults that table (`AuStaticComponentFind`) before reading a file. Static components keep the load-by-name lifecycle but stay resident: they cannot be unloaded, so the user-space servers (`SNDSRV`/`NETSRV`) do not apply. Select it through `--static-components` (LLVM only). It is independent of `__XENEVA_UNIKERNEL__`; the two can be combined.

Use `Scripts/Linux/build_and_run_qemu.sh --bleed` to select this profile. The script propagates the macro consistently to the bootloader, kernel, Chitralekha, init, DeodhaiXR, XELnch, and Namdapha and records the deployed user-space profile. Do not define it manually for only part of the system or mix normal and bleed binaries.

## Default Kernel expected macros
- `ARCH_ARM64` -- for arm64 build
- `__TARGET_BOARD_QEMU_VIRT__` - can be modified as per need.
- `__STDC_LIMIT_MACROS` -- Default kernel expected

`__XENEVA_BLEED__` is not a normal default; it is defined only by the explicit bleed workflow.
