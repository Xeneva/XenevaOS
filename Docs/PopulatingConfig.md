# Populating Runtime Configuration File

Runtime configuration files are loaded by the kernel to obtain descriptions of the system files required to load. For example, they define which driver files are available in the build image and how they relate to each hardware device. 

## Aurora Driver Configuration Files
Aurora Driver configuration files are divided into two types: PCIe-based configuration files and board-based configuration files. 

- **`board.cnf` (Predefined Hardware):** Used for SoC boards (like Raspberry Pi) where hardware is fixed. It statically links predefined hardware directly to software drivers, making the boot process extremely fast and small in size. Board-based configuration files need to be modified according to the target board (e.g., an i.MX8MP-based board needs configuration matching its specific hardware).
- **`audrv.cnf` (Dynamic Hardware):** Used for dynamic buses like PCIe. The kernel first discovers available hardware dynamically, then looks up this file to find the matching driver software location. A PCIe-based configuration file can hold any driver file along with its PCIe subclass/class code or vendor ID/device ID entry.

## The `audrv.cnf` File
This file must be present in the root folder of the ramdisk image. It holds PCIe-based driver file entries. For example:
```
(1)[03,128]/virtgpu.dll |
(2)[02,00]/virtnet.dll |
(3)[6900,4185]/virtsnd.dll |
(0)
```
Here, virtgpu.dll holds its _[classCode,subClassCode]_ value, while virtsnd.dll holds its _[vendorID,deviceID]_ value. Please note that entry numbers are in integer format, not hexadecimal format. The file path must be specified correctly; for instance, specifying "virtgpu.dll" will cause the entry to be skipped by the Aurora Driver Manager, whereas specifying "/virtgpu.dll" will succeed because the file resides in the root folder of the ramdisk. 
### Marker Points:
As you can see, there are certain marker points present in the configuration file. There are three types of marker points: numeric bullets, the [class/subclass] holder, and the end-of-entry marker. Numeric bullets start from (1) and end at (0), indicating there are no further entries and that the Driver Manager should stop parsing. The ClassSubClass holder holds either the subclass and class code values of the PCIe configuration or the vendor ID and device ID values for each device. The end-of-entry _"|"_ marker defines the end of an entry so that the Driver Manager can proceed to the next entry. 

## The `board.cnf` File

The board configuration file follows a slightly different format than the `audrv.cnf` file. This file also needs to be present in the root folder of the ramdisk. Since it deals with predefined hardware, it maps static hardware identifiers directly to their respective driver files, bypassing dynamic discovery for maximum boot speed and efficiency.

## The `lncher.cnf` File

The launcher configuration file (`/lncher.cnf`) defines the application shortcuts presented in the Xeneva Launcher (`xelnch.exe`). It is sourced from `Resources/resources/lncher.cnf` and deployed to the ramdisk root.

### Entry Format
Each application entry begins with `{` and contains four pipe-delimited (`|`) keyword lines:

```ini
{[Title]Calculator|
[Icon]/icons/calcl.bmp|
[Param]Null|
[App]/calc.exe}
```

- **`[Title]`**: Application display name (maximum 42 characters).
- **`[Icon]`**: Path to the launcher icon inside the ramdisk (e.g. `/icons/calcl.bmp`).
- **`[Param]`**: Optional startup argument passed to the process, or `Null` if none.
- **`[App]`**: Path to the executable binary (e.g. `/calc.exe`).

### The Critical End-of-List Marker (`>`)
Every intermediate entry terminates with `}`. **The very last entry in the file must terminate with `>`** instead of `}`:

```ini
{[Title]Solitaire|
[Icon]/icons/solitrl.bmp|
[Param]Null|
[App]/solitr.exe>
```

In `XELnch` (`Process/XELnch/lconfig.cpp`), detecting `>` signals `_last_entry_ = true`, which safely stops the parser loop.

### Dynamic Build-Time Filtering
When building with selective applications (via `--apps`), `build_and_run_qemu.sh` filters `lncher.cnf` so that any application whose binary is absent from `Resources/resources/` or excluded by build flags is omitted. The build script automatically rewrites the closing delimiter of the last surviving entry to `>` to ensure `XELnch` always receives a valid configuration.

