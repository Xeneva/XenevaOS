# Building XenevaOS with `xdev`

> ⚠️ **Experimental Notice**: The `xdev` toolchain and current XenevaOS builds are in **experimental alpha release** (`v0.1.0-alpha`). Certain system applications, drivers, and userland features are actively under development and may be incomplete, unstable, or non-functional.

---

## 1. Toolchain Installation

`xdev` is the official cross-platform CLI toolchain used to compile XenevaOS components, generate bootable storage images, and manage QEMU emulation.

### Linux

Run the setup script in your terminal:

```bash
curl -fsSL https://raw.githubusercontent.com/avrahtac/xdev/main/scripts/install.sh | bash
```

The script automatically detects your package manager, installs required dependencies (`clang`, `lld`, `llvm-ar`, `qemu`, `mtools`), sets up `xdev`, and prompts for your repository path.

### Windows

1. Download `xdev-setup.exe` from the [Releases](https://github.com/avrahtac/xdev/releases).
2. Run the installer (requires Administrator privileges) to set up MSYS2 UCRT64 dependencies.
3. Open PowerShell and set your repository path:

```powershell
[System.Environment]::SetEnvironmentVariable("XENEVA_PROJECT", "X:\XenevaOS", "User")
```
*Replace `X:\XenevaOS` with the path where you cloned this repository, then restart your terminal.*

### macOS

Support is currently coming soon.

---

## 2. Quick Start Commands

Once installed, navigate to or verify your workspace environment:

```bash
# 1. Verify environment and toolchain installation
xdev doctor

# 2. Compile kernel, bootloader, userland processes, and construct fat.img
xdev build

# 3. Launch XenevaOS in QEMU
xdev run
```

---

## 3. Workflow Commands

```bash
xdev build clean             # Wipe all build artifacts and generated disk images
xdev build KernelAA64        # Compile a single component (e.g., KernelAA64, BootAA64)
xdev run --memory=2048M      # Launch QEMU with custom RAM allocation
xdev fetch                   # Pull latest changes inside $XENEVA_PROJECT
```

For issues or toolchain bug reports, please refer to the [Repository](https://github.com/avrahtac/xdev).
