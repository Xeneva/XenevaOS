# USB POC runbook

This is the short, repeatable proof for the USB mass-storage and HID work.
Use it for the first-release pull request and for the recorded demo.

Mass storage and HID are the first-release scope. The gate covers both the
SuperSpeed and USB2 paths, and the live demo shows the same stack from the
desktop. Edge paths that QEMU cannot prove stay amber on the readiness chart
and are not part of this claim.

`Scripts/Linux/usb_poc.sh` is the entry point:

```sh
Scripts/Linux/usb_poc.sh demo      # boot the live desktop
Scripts/Linux/usb_poc.sh verify    # run the automated gate
Scripts/Linux/usb_poc.sh commands  # print the XEShell command card
Scripts/Linux/usb_poc.sh evidence  # summarize the latest gate log
```

The live demo is the interactive sibling of the gate: same QEMU machine,
same xHCI controller, plus one FAT32 stick on the controller. The demo
stick is `Build/xeneva-demo-usb.img`; `--fresh` recreates it.

## Live demo

Start it:

```sh
Scripts/Linux/usb_poc.sh demo --skip-build
```

At the loader menu choose **1024x768** with Down, Down, Enter. The starred
entries (`1280x720 *`, `1920x1080 *`, and so on) need the virtio-GPU driver
to repoint the console; this image keeps the firmware framebuffer, so use
an unstarred mode for the POC.

Then open **Xeneva Terminal** from the launcher and type:

```text
systeminfo
cd /
ls
cd b
ls
cd Demo
ls
cd /
umount b
cd b
mount b
cd b
ls
```

Expected results:

- `cd /` then `ls` lists the root.
- `cd b` enters the USB volume; `ls` lists `DEMO`.
- `cd Demo` then `ls` lists `WELCOME.TXT`, `XENEVA.JPG`, and `SNOW.JPG`.
- `umount b` flushes and detaches the volume.
- `cd b` fails while the volume is unmounted.
- `mount b` reattaches the same volume at the same letter.
- `cd b` then `ls` shows `DEMO` again.

HID is shown by the input devices, not by a shell command:

- Move the USB mouse; the pointer follows the tablet.
- Click the launcher; the Terminal window opens.
- Type the commands above; the USB keyboard drives the shell.

The guest log prints the device path as it enumerates:

```text
[xhci-hid]: report iface 0 len 63 fields 2 kind keyboard ids 0
[xhci]: HID keyboard ready on port 5 slot 1 ep=81 mps=8
[xhci-hid]: report iface 0 len 110 fields 4 kind pointer ids 1
[xhci]: HID pointer ready on port 8.8.1 slot ... ep=... mps=...
[fat]: USB QEMU HARDDISK mounted at b oem XENEVA
[vdisk]: USB QEMU HARDDISK unmounted from b
[xhci-msc]: vdisk reuse slot ...
```

The Wacom tablet on the nested hub uses report IDs, so `ids 1` is the
report-ID path. The tablet on the external hub exercises the hub path.

### Optional hotplug

With QEMU still running, remove and re-add the stick from the host:

```sh
Scripts/Linux/demo_usb_hotplug.sh remove demo-usb0
# wait for the guest to log the disconnect
Scripts/Linux/demo_usb_hotplug.sh add
```

The helper recreates the drive backend before adding the device because
QEMU deletes the original `-drive` along with the device. The guest detaches
`/b` on unplug and reattaches it on replug.

## Automated proof

```sh
CLEAN=1 Scripts/Linux/usb_poc.sh verify
```

`CLEAN=1` forces a full kernel and userspace rebuild, so the result does not
depend on stale objects. The gate boots the normal USB topology and the
USB2/UAS topology, then checks the guest log. A passing run ends with:

```text
test_all: ALL GREEN
```

The first boot reports `[dcltest] SUMMARY 147 ok, 0 failed` and
`[bttest] SUMMARY 22 ok, 0 failed` when the default gate devices are present.
The USB assertions include:

- FAT directory listing, `GATE.TXT` lookup, unmount, absence, remount, and
  lookup again
- keyboard and pointer HID reports, including report IDs
- hotplug of the keyboard, tablet, mass storage, UAS, and multi-LUN BOT
- BOT and UAS transport, multi-LUN, and vdisk reuse across replug

After a run, copy the evidence lines:

```sh
Scripts/Linux/usb_poc.sh evidence
```

## Pull request summary

Suggested description:

```text
Adds xHCI USB mass storage and HID support for the first release.

Mass storage:
- Bulk-Only Transport and UAS
- SCSI INQUIRY, READ CAPACITY, READ/WRITE, REQUEST SENSE, REPORT LUNS
- GPT scan, FAT32 mount, directory enumeration, runtime mount/unmount
- surprise unplug detaches the volume and replug reuses the vdisk

HID:
- boot keyboard and pointer/tablet
- report descriptor parser with report-ID support
- generic keyboard, absolute pointer, and relative pointer decoders
- keyboard LEDs, poll thread, and hotplug while polling

Verification:
- Tests/dcl_test_all.sh: ALL GREEN
- live demo: Scripts/Linux/usb_poc.sh demo
- POC runbook: Docs/Development/usb-poc.md
```

Do not claim the paths listed under "What not to claim" in
[`demo.md`](demo.md): USB3 hub behaviour, suspend/resume, overcurrent,
composite HID, a real Bluetooth dongle, zero-scratchpad controllers,
non-4K pages, or large configuration descriptors.
