# Changelog

## Unreleased

The AArch64 LLVM/Clang work below is recorded alongside the previously documented runtime fixes and known issues; unrelated history has been retained.

### Added

- Added a persistent `data.img` virtio-blk disk to `build_and_run_qemu.sh` (`--data-size-mb`, `--no-data-disk`, `--force-data-disk`), mounted by the guest at `/data` via volume label; `virtblk.dll` is now built and handles multiple block devices per driver load.
- Added `--term -- <cmd>` passthrough so guest flags that look like host options (e.g. `--selftest`) reach the TERM command line.
- Added `ntpd.exe` to the GCC user-space build, and `init` only spawns it when `/ntpd.exe` exists; the kernel seeds wall time from the board RTC only when it reads later than 2020-01-01.
- Added LLVM build support for the AArch64 user-space runtime, graphics library, and the supported applications used by the QEMU image workflow.
- Added `--llvm` toolchain selection to `Scripts/Linux/build_and_run_qemu.sh`; LLVM is the script default.
- Added the AArch64 LLVM `--bleed` streamlined build, a low-memory profile that boots today's desktop with trimmed compositor overhead and a smaller guest footprint.
- Added `Scripts/Linux/gen_compile_commands.sh` to capture the AArch64 LLVM commands in `compile_commands.json` for clangd and other Clang tooling.

### Changed

- Extended the existing bootloader and kernel LLVM targets into an end-to-end AArch64 build. Clang targets `aarch64-unknown-windows`, and LLD emits the native PE/COFF images consumed by XenevaOS.
- Expanded `Scripts/Linux/lib/llvm.sh` to build the bootloader and kernel and, when requested, the supported user-space libraries and applications.
- Updated AArch64 sources and headers to compile under Clang's stricter C and C++ diagnostics while retaining the existing MSVC build path.
- Kept the board selection independent of the compiler. `qemu_virt` remains the default, while `BOARD=rpi3` selects the Raspberry Pi 3 definitions.
- On MSYS2, `BootAA64` and `KernelAA64` select LLVM automatically because the GNU AArch64 Linux cross-toolchain is not available as an MSYS2 package. The current first/default Make target also invokes LLVM on Linux; the GCC path remains available explicitly through `make TOOLCHAIN=gcc all`.
- Added early Makefile checks for Clang and LLD with MSYS2-specific installation guidance and a clear error when `TOOLCHAIN=gcc` is requested in that environment.
- In bleed builds, kept glass composition, network, and audio services active while trimming compositor buffer pressure and boot overhead; the bootloader selects the default resolution without the interactive menu and QEMU's memory drops from 1 GiB to 384 MiB by default.
- Added a shared-client-backbuffer path in Chitralekha with release/acquire publication and a bounded acknowledgement wait, so clients paint directly into the compositor-owned surface instead of privately copying every updated row.
- Isolated bleed and normal user-space artifacts with automatic clean rebuilds on script-managed profile changes, rejected stale legacy initrds in bleed, added a bounded acknowledgement wait for direct-buffer presentation, and added a safe GOP-mode fallback.
- Coalesced overlapping compositor damage, reduced framebuffer synchronization to one barrier per presentation, and added fast paths for fully opaque and fully transparent pixel groups in both normal and bleed images.
- Added the independent `--direct-scanout` experiment, which composes into a tightly packed GOP framebuffer without a second compositor canvas and falls back safely when the framebuffer pitch is incompatible.

### Fixed

- **fix(ARM64 merge)**: reconciled the native/Linux futex and pthread work with the SMP scheduler, retaining CPU statistics syscall 86 and native futex syscall 94. Waiter publication, wake/requeue, timeout results, and saved exception-frame updates now share the scheduler lock; Linux clone initialization stays locked until the child is fully published.
- **fix(virtionet)**: the TX path round-robined 8 buffers without ever consuming completions, so bursts rewrote descriptors QEMU had not finished with and the wire saw spliced frames. Transmit now reclaims `tx_used` before reusing a slot (bounded spin, drop-and-retry past it) and propagates the result through the NIC write callback.
- **fix(XEClib)**: removed per-call serial debug prints from `fopen`/`fwrite`/`puts` that flooded the console during TLS handshakes and distorted timing.
- **fix(BootAA64)**: `ExitBootServices` retry loop maybe livelock through all 16 retries and hang before the kernel ever ran — the `EFI_BUFFER_TOO_SMALL`
  reallocation path gave the memory-map buffer zero headroom, unlike the
  initial allocation, so the `FreePool`/`AllocatePool` churn could itself
  perturb the map and trigger the same failure again. Reproduced
  intermittently (2/5 headless boots hung); fixed by matching the initial
  allocation's headroom. (`BootAA64/xnldr.cpp`)

- **fix(Mm/tlsf)**: `tlsf_fls()` computed size-class buckets using
  `__builtin_clzl()` against a `size_t`, but the kernel targets
  `aarch64-unknown-windows` where `long` is 32-bit — every bucket index was
  wrong, so `tlsf_malloc()` could never find memory that provably existed in
  the pool. `kmalloc()` failed on the very first real allocation after boot.
  (`KernelAA64/Mm/tlsf.c`)

- **fix(Mm/tlsf)**: `tlsf_malloc()` removed the found free block from the
  free-list bucket computed from the *requested* size instead of the block's
  *actual* size. Since the search can (correctly) fall back to a larger
  bucket, this frequently removed from the wrong index — a silent no-op —
  leaving the block registered as free while already handed out, causing
  double-allocation on the next call. (`KernelAA64/Mm/tlsf.c`)

- **fix(Mm/tlsf)**: `tlsf_malloc()`/`tlsf_realloc()` treated the caller's
  requested payload size as the total block size, never adding room for the
  16-byte block header. Every allocation returned a buffer 16 bytes smaller
  than requested; allocations landing close to a block boundary silently
  overflowed into the next block's header, corrupting heap metadata (root
  cause of the intermittent post-boot crashes / "memory management" hangs).
  (`KernelAA64/Mm/tlsf.c`)

- **fix(list)**: `initialize_list()` / `list_add()` dereferenced `kmalloc()`'s result without checking for `NULL`, turning any allocation failure into an unrecoverable NULL-pointer crash instead of a graceful failure. (`KernelAA64/list.c`)

- **fix(Init)**: `SplashScreenShow()` opened `/xelogo.bmp` and went through file-stat, mmap, and BMP-header parsing without checking any intermediate result for failure. A missing/unreadable logo file crashed `init.exe` outright on boot. Now it just skips the splash if the file is missing. (`Process/Init/splash.cpp`)

- **fix(Net/socket, Mm/mmap)**: two call sites allocated `kmalloc(strlen(x))` immediately before `strcpy(dst, x)`, one byte short of the null terminator, corrupting the next heap block's header by one byte on every call. (`KernelAA64/Net/socket.c`, `KernelAA64/Mm/mmap.c`)

- Corrected AArch64 code that depended on MSVC extensions, implicit declarations, incompatible pointer conversions, or compiler-specific variadic handling and therefore failed to compile with Clang.
- Corrected LLVM/AArch64 assembly and low-level synchronization routines that were missing from, or incompatible with, the Clang build.
- Fixed TLSF size-class calculation for the `aarch64-unknown-windows` LLP64 data model, where `long` is 32-bit even though pointers and `size_t` are 64-bit.
- Excluded superseded allocator implementations from the AArch64 Makefile so the LLVM kernel links only the active TLSF allocator.
- Corrected the EFI file loader's page-count rounding so non-page-aligned files cannot overrun their allocation.
- Fixed the AArch64 graphics `_fastcpy` loop, which advanced by 16 bytes after copying 32 bytes, and removed its per-scanline full-system barrier in favor of the presentation-level framebuffer commit.
- Fixed mixed-alpha NEON groups being blended twice and corrected framebuffer clipping for updates that begin outside the upper or left screen boundary.
- Reworked the ordinary AArch64 resolution menu to wait on UEFI keyboard events, redraw without flicker, expose only selectable GOP modes, skip unavailable entries, and preserve the firmware mode when none of the listed resolutions is supported.
- Fixed the loader formatter's `h`/`hh` modifier comparisons, its missing LLVM `va_end`, the backward-overlap path in its local copy routine, and an uninitialized file-metadata pointer after an unexpected UEFI `GetInfo` result.
- Hardened EFI file loading against empty and partial reads, matched page allocations with `FreePages`, and closed error-path handles. Moved AArch64 cache synchronization from generic source-file buffers to the kernel's final mapped PE image, avoiding a pointless initrd sweep while correctly preparing executable bytes.
- Corrected the `ExitBootServices` failure diagnostic to pass an actual UTF-16 string to the UEFI console.

### Detailed engineering notes

This section records the motivation, failure mode, implementation, tradeoffs, and validation behind the AArch64 LLVM and performance work. It is intentionally more detailed than a conventional changelog so it can serve as source material for a technical write-up.

#### End-to-end AArch64 LLVM/Clang support

**Starting point.** The repository already contained pieces of an LLVM path, but it was not an end-to-end replacement for the Visual Studio/MSVC workflow. The Linux helper did not consistently build the EFI loader, kernel, graphics library, runtime, and applications as one matched image. Several sources also relied on MSVC behavior that Clang correctly rejected or interpreted differently.

**What changed.**

- The maintained LLVM target is `aarch64-unknown-windows`. This is deliberate: XenevaOS consumes PE/COFF images and uses the Windows AArch64 LLP64 data model, so targeting generic AArch64 ELF would change the ABI and require a separate conversion path.
- Clang compiles the loader, kernel, runtime, Chitralekha, DeodhaiXR, and supported applications. LLD emits EFI and native PE/COFF images directly with the required entry point, image base, and subsystem.
- The Linux orchestration helper now builds the bootloader and kernel every time, and builds/deploys all supported user-space components when requested. The same `BLEED` value is forwarded through every nested Make invocation so a ramdisk cannot accidentally mix normal and bleed objects.
- MSYS2 defaults the AArch64 loader and kernel to LLVM because an `aarch64-linux-gnu-*` cross-toolchain is not normally packaged there. The Makefiles fail early with actionable Clang/LLD installation guidance instead of failing much later at link time.
- `BOARD` remains independent of `TOOLCHAIN`: `qemu_virt` is still the default, and `BOARD=rpi3` selects Raspberry Pi 3 definitions without changing the compiler model.
- The loader now includes the canonical GOP protocol header rather than maintaining a duplicate GUID definition, and the unconditional 32-bit `SIZE_MAX` redefinition was removed. That matters under LLP64: pointers and `size_t` are 64-bit even though `long` is 32-bit.

**Why this matters.** A compiler port is also a portability audit. Clang exposed implicit declarations, incompatible pointer conversions, missing assembly helpers, assumptions about variadic arguments, and assumptions about fundamental type widths that the prior build had tolerated. Fixing those issues produced a reproducible AArch64 image rather than merely suppressing diagnostics.

#### Build-profile integrity and the `bleed` image

**Problem.** Compile-time profiles are unsafe if old binaries can be silently reused. A normal Chitralekha library paired with a bleed compositor, or a bleed application packed into a normal initrd, can have different allocation and presentation behavior even when the public structures happen to be the same.

**What changed.**

- `--bleed` propagates `__XENEVA_BLEED__` through the EFI loader, kernel, Chitralekha, init, compositor, launcher, and desktop.
- The build script records the deployed user-space profile, including toolchain, bleed state, and direct-scanout state. Changing profiles forces a clean user-space rebuild.
- Bleed rejects `--skip-build` and `--force-legacy-build`; both options could otherwise reuse an initrd whose binaries do not match the selected profile.
- Direct scanout also rejects those reuse modes because the compositor itself must be rebuilt with `__XENEVA_DIRECT_SCANOUT__`.
- Normal behavior is preserved when neither flag is selected. Bleed is not implemented as a log level and does not suppress general diagnostic output.

**Lesson.** A build profile is part of an artifact's identity. Treating it only as a shell option, without invalidating previously compiled objects, makes behavior differ in ways that are hard to attribute.

#### Bleed boot and memory trimming

**What stays.** Glass windows, network and audio daemons, the full startup sequence, and all guest services. The desktop is visually and functionally identical to the ordinary image.

**What changed.**

- The EFI loader selects the default resolution directly instead of blocking on the interactive resolution menu. If the default mode is absent, GOP retains the current firmware mode. Ordinary images keep the menu.
- QEMU uses 384 MiB by default instead of 1 GiB for the bleed profile. The override remains available through `XENEVA_QEMU_MEMORY`.
- DeodhaiXR removes its 100 ms input-device delay and the 500 ms gap between launching XELnch and Namdapha.
- Two compositor allocations — 6 MiB and 50,560 bytes — were allocated and zeroed but never consumed by the shown startup path. They are omitted from bleed.
- The physical-memory manager still constructs and recounts the buddy allocator, but bleed omits the boot-only full validation walk and allocation/free self-test. Normal builds retain both checks.
- The font manager parses the configured list but preloads only Calibri, Forte, and Consolas, then reports the actual loaded count. This keeps the desktop and bundled tools usable without pinning every configured font at boot.
- The bleed initrd omits x86_64 payloads, music, unused startup audio, and fonts the profile does not preload. It defaults to 48 MiB, which accommodates the AArch64 initrd plus userspace; the override remains available through `--initrd-size-mb`.

These changes reduce startup latency and minimum memory footprint without altering the feature set. They are isolated behind `--bleed` instead of applied silently to the ordinary image.

#### Shared client backbuffers and presentation ordering

**Old path.** A Chitralekha client painted into a private canvas and copied every updated row into the compositor-owned shared window backbuffer. That duplicates each window's pixel storage and adds another memory-bandwidth pass.

**Bleed path.** The client canvas points directly at the shared compositor backbuffer. `ChWindowUpdate` detects that the source and destination are identical and skips the redundant copy. Popup windows use the same rule. A zero-sized `bufferSz` marks the canvas as non-owning so destruction does not unmap compositor memory.

**Race that had to be solved.** Zero-copy changes ownership timing. Without a protocol, the client can start the next frame while DeodhaiXR is still reading the previous one, and ordinary C++ loads/stores of `dirty` or `updateEntireWindow` do not publish the preceding pixel writes across processes.

**Synchronization fix.**

- Clients write pixels and dirty-rectangle metadata first, then publish the update flags with release stores.
- The compositor observes those flags with acquire loads and clears them with release stores after consumption. Animation, hide/show, ordinary windows, and always-on-top windows use the same accessors.
- Bleed clients wait for compositor acknowledgement before reusing the shared buffer. The wait is bounded to approximately 250 ms, preventing a dead compositor from hanging a client forever.
- Dirty-rectangle insertion is capped at the 256 entries available in the shared structure, preventing metadata from walking beyond the shared allocation.

**Tradeoff.** This is single-buffered zero-copy, not page flipping. It saves memory and a client-to-compositor copy, but acknowledgement can introduce back-pressure. A future two-buffer or mailbox protocol could preserve zero-copy publication without making the client wait for the compositor to finish reading the same surface.

#### AArch64 copy primitive and framebuffer barriers

The Chitralekha `_fastcpy` assembly contained a correctness bug in its 16-byte loop: `ldp q0, q1` and `stp q0, q1` transfer 32 bytes, but the pointers and remaining count advanced by only 16. Each iteration overlapped the previous one and the final iteration could write beyond the requested range. The loop now uses one 128-bit `ldr`/`str` pair and advances exactly 16 bytes.

Both graphics copies also ended with `dmb sy`. `_fastcpy` is used for ordinary RAM as well as framebuffer writes, and the screen-update path calls it once per scanline. A 480-line update therefore issued as many as 480 full-system barriers. The barriers were removed from the generic copy primitive and replaced with `ChCanvasScreenCommit()`, called once after the complete dirty batch. This preserves ordering for the Normal Non-cacheable GOP mapping while avoiding device-strength synchronization for every row and every ordinary memory copy.

The resulting objects were disassembled to verify that both `_fastcpy` implementations use a 16-byte `ldr`/`str` loop and contain no embedded `dmb`.

#### Damage tracking and framebuffer clipping

**Old behavior.** DeodhaiXR appended every dirty rectangle independently. When the fixed array filled, it reset the count to zero and silently discarded all pending damage. Negative coordinates were clamped to zero without reducing width or height, changing the rectangle's right or bottom edge and potentially copying the wrong pixels.

**New behavior.**

- Touching or overlapping rectangles are merged transitively before presentation, reducing duplicate composition and framebuffer copies.
- Empty or negative-sized rectangles are rejected.
- If the fixed array still fills, all pending damage is collapsed into one conservative bounding rectangle instead of being lost.
- Clipping uses 64-bit left/top/right/bottom coordinates. Moving the left or top edge inward now reduces the resulting dimensions correctly, and fully off-screen rectangles are discarded.
- `ChCanvasScreenUpdate` now rejects null canvases, missing framebuffers, non-positive regions, and direct-scanout aliases before copying. Framebuffer pitch conversion is computed once per region instead of once per row, and non-owning canvases are not passed to `_KeMemUnmap` during destruction.
- The back-surface damage list uses the same merging and overflow rules.
- GPU-enabled state is sampled once per presentation rather than repeatedly for every rectangle.
- The framebuffer commit barrier executes once after all surviving rectangles have been copied.

The bounding rectangle may include undamaged pixels when several shapes only touch at an edge or corner. That is a deliberate correctness-first fallback: copying a little extra is preferable to leaving stale pixels on screen.

#### NEON alpha-compositing correction and fast paths

The original four-pixel NEON path blended and stored a vector before deciding whether the group was opaque. Mixed-alpha groups then fell through to a scalar loop and blended the same pixels a second time, using the already modified destination. Its opaque test also examined only half of the four lanes.

The revised path checks all four alpha lanes before touching the destination:

- Four opaque pixels are copied with one vector store.
- Four transparent pixels are skipped without reading or writing the destination.
- A mixed group is blended exactly once from the original destination.
- The scalar tail retains the same opaque, transparent, and partial-alpha behavior.

This is both a visual correctness fix and a useful optimization for a desktop dominated by opaque surfaces.

#### Optional direct GOP scanout

`--direct-scanout` builds DeodhaiXR with an experimental path where the compositor canvas aliases the mapped GOP framebuffer. It is independent of bleed and can be used with either image profile.

The path is enabled only when the framebuffer is present, 32-bit, and tightly pitched (`pitch == width * 4`). Otherwise DeodhaiXR reports the incompatibility and allocates the normal cached canvas. Screen-copy calls become no-ops when the canvas already aliases the framebuffer, while the presentation-level barrier remains.

At 640×480×32 bpp, direct scanout removes a 1,228,800-byte compositor canvas. It is not the default because the kernel maps GOP as Normal Non-cacheable memory: direct composition performs alpha blending and destination reads against that slower mapping, whereas the cached path composes in normal RAM and performs one dirty copy at presentation. In one controlled QEMU A/B boot, direct scanout reached the desktop about 14 ms earlier (approximately 5.963 s versus 5.977 s), which is too small to distinguish from run-to-run noise. The memory reduction is real; a boot-time performance claim is not made from that sample.

#### EFI resolution-menu repair

**Old behavior.** The ordinary loader displayed four hard-coded resolutions whether GOP supported them or not. It polled `ReadKeyStroke` continuously until input arrived, cleared the entire console after every key, ignored unexpected input errors, and silently retained another mode when the requested one could not be found. This caused flicker, poor keyboard behavior, misleading choices, and an apparent selection that did not necessarily match the resulting framebuffer.

**New behavior.**

- The loader queries every GOP mode before drawing the menu and records which listed resolutions are actually selectable.
- Unsupported entries are shown dimmed and marked `(unavailable)`.
- Up/Down navigation wraps and skips unavailable entries.
- Input waits on the UEFI `WaitForKey` event instead of consuming a CPU in a readiness loop.
- The screen is cleared once. Subsequent selections redraw stable, fixed-width rows, preventing the full-screen flash on every keypress.
- Input status is checked, the cursor is hidden during selection, and its prior visibility is restored afterward.
- If none of the listed resolutions exists, the loader states that it will keep the firmware mode rather than presenting a false choice.
- `XESetGraphicsMode` starts from the active firmware mode, checks `QueryMode` results, frees every returned mode-information pool, and only marks graphics initialized after `SetMode` and graphics setup succeed.

Visual QEMU verification showed 640×480 and 1024×768 as available and 1280×1024 and 1920×1080 as unavailable. A synthetic Down key moved the highlight to 1024×768; Enter applied that mode and the system reached the desktop at 1024×768. A final default-selection run also reached DeodhaiXR, XELnch, and Namdapha at 640×480.

#### EFI loader, formatter, and cache-coherency fixes

- File-page allocation changed from `(FileSize + 1) / 4096` to `(FileSize + 4095) / 4096`. The previous expression rounded down for most non-page-aligned sizes and could even request zero pages for a small file; only exact-page sizes and the accidental `size % 4096 == 4095` case rounded correctly.
- File and protocol pointers are initialized before use. The first `GetInfo` call must return `EFI_BUFFER_TOO_SMALL` with a nonzero size before metadata storage is allocated; an unexpected firmware response no longer leaves `FileInfo` uninitialized.
- Empty files and partial reads are rejected. Every error path closes the file and root directory handles and frees any pages already allocated.
- `XEFile` records its page count. Content allocated with `AllocatePages` is now released with `FreePages`, rather than incorrectly passing the address to `FreePool`.
- The local overlapping-copy path moved both source and destination pointers backward. It previously decremented the destination while incrementing the source, reading beyond the end of the source range and corrupting overlapping moves.
- The custom UTF-16 formatter used assignment in two `h`/`hh` modifier branches. Those conditions are now comparisons, and the LLVM variadic path calls `va_end` after output.
- The `ExitBootServices` failure diagnostic now passes a genuine UTF-16 literal to UEFI rather than casting an 8-bit string pointer and producing garbled characters.
- Cache maintenance was removed from generic file reading. The old code operated on the PE source and initrd buffers and accidentally passed a page count to functions expecting a byte count. PE bytes are not executed from the source buffer: sections are copied into the final mapped kernel image. After all sections are installed, the loader now cleans the final image's data-cache lines to the point of unification, issues the required barrier, invalidates the corresponding instruction-cache lines, and finishes with `dsb`/`isb` before control can transfer to the kernel. This is the correct AArch64 self-modifying-code sequence and avoids sweeping the large initrd unnecessarily.
- The `ExitBootServices` retry path gives a resized memory-map buffer the same descriptor headroom as the initial allocation. Without headroom, freeing and reallocating the buffer could perturb the map again and cause repeated stale-map failures; this had produced intermittent pre-kernel hangs.

#### Kernel and runtime correctness fixes retained in this release

- **TLSF first-level mapping:** `__builtin_clzl` follows the width of `long`, which is 32-bit in the selected Windows AArch64 ABI. Applying it to 64-bit sizes produced incorrect free-list buckets. The mapping now uses an operation whose width matches `size_t`.
- **TLSF removal bucket:** a search may satisfy a request from a larger bucket. Removal must use the found block's actual size, not the original requested size; otherwise the block remains registered as free and can be handed out twice.
- **TLSF allocation accounting:** requested payload sizes now include the 16-byte block header before rounding and splitting. Previously every returned payload was 16 bytes too short and could overwrite the next block's metadata.
- **Allocator selection:** obsolete allocator objects are excluded from the AArch64 link so only the active TLSF-backed heap supplies allocation symbols.
- **List allocation failures:** list initialization and growth check `kmalloc` results before dereferencing them, turning low-memory failure into a reportable error rather than an immediate null-pointer exception.
- **String ownership:** socket and mmap paths allocate `strlen(string) + 1` before `strcpy`. The missing byte previously overwrote the following heap metadata with the terminator.
- **Missing splash resource:** init checks open, stat, mapping, and BMP parsing results. A missing `/xelogo.bmp` now skips the splash instead of crashing the first user process.

#### Validation performed

- Clean LLVM builds completed for `BootAA64`, Chitralekha, and DeodhaiXR in cached and direct-scanout configurations.
- Bleed booted headlessly under QEMU with 256 MiB through the launcher and desktop.
- Cached and direct-scanout compositor images both booted through DeodhaiXR, XELnch, and Namdapha.
- The ordinary resolution menu was captured from QEMU, navigated through the emulated keyboard, and verified at both 640×480 and 1024×768.
- The final loader containing file-lifetime and executable-cache changes booted through the compositor and desktop applications.
- Shell syntax validation and `git diff --check` pass. Compiler warnings that remain are existing cleanup work and are not treated as successful validation by themselves.

#### Takeaways for a postmortem or blog post

1. **A toolchain port finds runtime bugs.** ABI width differences and stricter diagnostics exposed allocator, variadic, pointer, and assembly defects that were not merely build-system problems.
2. **Place synchronization at ownership boundaries.** A barrier inside a generic copy loop is both expensive and conceptually wrong; the correct boundary is the completed framebuffer presentation. Similarly, executable cache maintenance belongs on the final executable image, not its source file.
3. **Zero-copy is a protocol, not just pointer aliasing.** Removing a copy requires explicit publication, consumption, acknowledgement, and lifetime rules.
4. **Benchmark profiles must be reproducible artifacts.** Flags, binaries, resources, memory size, and service policy must change together, and stale outputs must be rejected.
5. **Firmware capabilities must be queried.** A boot menu should present what GOP can actually set, not a hard-coded wish list.
6. **Separate measured results from hypotheses.** Direct scanout demonstrably saves one framebuffer-sized allocation, but the single-run boot difference was noise and the non-cacheable mapping may reduce steady-state rendering performance.

### Build outputs

- Bootloader: `BootAA64/Build/EFI/BOOT/BOOTAA64.efi`
- Kernel: `KernelAA64/KernelAA64.exe`

See `Docs/BuildInstructions(Linux).md` for the Linux LLVM and QEMU workflow, or `Docs/BuildInstructions(ARM64).md` for the Windows/MSYS2 LLVM workflow.

### Known issues

- `Process/Control/ctrl.exe` crashes on a non-null fault address partway through window creation; the cause has not yet been isolated.
- `BootAA64/xnldr.cpp`'s `_need_fdt_hardcode` fallback triggers on an FDT magic mismatch but not when the FDT is entirely absent from the UEFI configuration table.
- `/xelogo.bmp`, referenced by `Process/Init/splash.cpp`, is currently missing from the resources tree.
