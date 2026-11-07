# Live demo

`Scripts/Linux/demo_xeneva.sh` boots the real XenevaOS desktop for a
recording. It uses the same QEMU machine and xHCI topology as the automated
gate, then attaches one extra FAT32 stick with sample files. It does not run
the gate programs.

For the short proof and pull-request command card, see
[`usb-poc.md`](usb-poc.md) and `Scripts/Linux/usb_poc.sh`.

## Start it

```sh
Scripts/Linux/demo_xeneva.sh
```

The first run builds the kernel and bootloader and reuses the userspace
binaries already in `Resources/resources`. After a gate run, `--skip-build`
is faster. At the loader menu choose **1024x768** with Down, Down, Enter.
That is the smallest mode that fits the Terminal window.

The script creates `Build/xeneva-demo-usb.img` on first use. The image is
git-ignored. `--fresh` recreates it.

## Recording order

1. **Desktop and launcher.** Move the USB mouse, click the bird button at the
   bottom left, then open **Xeneva Terminal**.
2. **Terminal.** Type:
   - `systeminfo`
   - `cd /` then `ls`
   - `cd b` then `ls`
   - `cd Demo` then `ls`
   - `cd /`, then `umount b`; `cd b` now reports the directory is missing
   - `mount b`, then `cd b` and `ls` (the same volume is back)

   `b` is the USB stick. Its `Demo` directory holds `WELCOME.TXT`,
   `XENEVA.JPG`, and `SNOW.JPG`. The letter stays reserved while the volume
   is unmounted, so `mount b` reattaches the same stick instead of allocating
   a new letter.
3. **Files.** Open Files from the launcher and choose **USB QEMU HARDDISK
   (Location: b)**. Double-click **DEMO** to list the three files. The list
   view navigates directories; this image does not open a file from the list.
4. **Optional hotplug.** With QEMU still running, remove and re-add the stick
   with the helper:

   ```sh
   Scripts/Linux/demo_usb_hotplug.sh remove demo-usb0
   # after the guest logs the disconnect:
   Scripts/Linux/demo_usb_hotplug.sh add
   ```

   The helper recreates the drive backend before adding the device, because
   QEMU deletes the original `-drive` together with the device. An unplug
   detaches the volume without flushing; a replug reattaches it automatically.
   In the Terminal, `cd b` should fail between the two commands and succeed
   again afterwards.
5. **Applications.** Open Calculator, XENotes, or XEPaints.
6. **Serial evidence.** The terminal that launched the script prints the xHCI
   topology. Useful lines:
   - `[xhci]: hub ready on port 8.8 slot ... ports 8`
   - `[xhci-hid]: report iface 0 len 110 fields 4 kind pointer ids 1`
   - `[xhci]: HID pointer ready on port 8.8.1 ...`
   - `[fat]: USB QEMU HARDDISK mounted at b`
   - `[vdisk]: USB QEMU HARDDISK unmounted from b`
   - `[xhci-hub]: child enumerated on port 8.8.1`

   Record only the QEMU window for a cleaner video, or keep the serial
   terminal visible when showing the device path.

## What the demo covers

The boot topology has a USB keyboard on a root port, a USB tablet and serial
function behind an external hub, a nested hub below that one, and a Wacom
tablet on the nested hub. The pen tablet's report descriptor uses report IDs,
so the HID parser exercises its ID path. The stick exercises Bulk-Only
Transport, SCSI INQUIRY, capacity reads, block reads, the FAT32 mount in the
VFS, and FAT directory enumeration in both Terminal and Files. The
`mount`/`umount` commands exercise the VFS mount list at runtime; the
automated gate unmounts and remounts the same stick and checks that
`GATE.TXT` disappears and returns.

The gate also covers hotplug (keyboard, tablet, mass storage, UAS, BOT
multi-LUN) and the host Bluetooth unit tests. The readiness chart is at
[`xhci-progress.html`](xhci-progress.html).

## What not to claim

These paths are implemented or partially implemented but are not proven in
QEMU, so leave them out of the recording:

- USB3 hub behavior, suspend/resume, and overcurrent handling
- composite HID devices
- a real Bluetooth dongle (the guest uses the emulated HCI and `bttest`)
- zero-scratchpad controllers and non-4K page kernels
- large configuration descriptors (the QEMU model for that case is absent)
- opening a file from the Files list; the list view navigates directories
  and shows file metadata, but does not launch a viewer

Amber rows on the chart are implemented but not gate-tested. Treat them as
work in progress, not demo material.

## Stop and clean up

Close the QEMU window. The demo stick stays in `Build/` until you delete it.
The persistent `data.img` is reused between runs; `--force-data-disk` wipes
it if you want a clean guest.
