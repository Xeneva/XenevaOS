# Big-configuration-descriptor QEMU device

`Tests/dcl_test_all.sh` normally runs on stock QEMU. Stock QEMU has no USB
device whose configuration descriptor is larger than one page, and its USB
core also caps a control data buffer at 4 KiB. The xHCI driver must read
descriptors past that boundary, so this directory carries a test-only QEMU
patch:

- `usb-storage-bigcfg.patch` adds a `usb-storage-bigcfg` device. It puts
  20 vendor-specific descriptors (5,000 bytes) in interface 0 and the real
  Bulk-Only mass-storage interface in interface 1. A host that reads only
  4,096 bytes of the configuration descriptor never sees the disk.
- The same patch raises `USBDevice.data_buf` from 4,096 to 8,192 bytes so
  QEMU can answer the full request.

The patch targets QEMU 11.1.2. Build it with an extracted QEMU source tree:

```sh
./Tests/qemu/build_qemu_bigcfg.sh /path/to/qemu-11.1.2
PATH=/tmp/opencode/qemu-bigcfg-build:$PATH \
	BYPASS_VIRTGPU=1 CLEAN=1 ./Tests/dcl_test_all.sh
```

The gate probes `qemu-system-aarch64 -device help` for the test device. If
it is present, the gate adds the big-config boot and asserts the guest reads
LBA 0 (`BIGC`) from the disk. On stock QEMU it prints a skip line and the
rest of the gate is unchanged.
