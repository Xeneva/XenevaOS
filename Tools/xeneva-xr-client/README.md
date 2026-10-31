# xeneva-xr-client

Interactive client for the QEMU monitor socket. The XR demo runs QEMU with
no visible window (`-display dbus`), so this is how you drive the guest:
bootloader menus, `sendkey`, `info status`, `screendump`, ...

```bash
make -C Tools/xeneva-xr-client
Tools/xeneva-xr-client/xeneva-xr-client            # interactive REPL
Tools/xeneva-xr-client/xeneva-xr-client --exec "sendkey ret"   # one shot
```

Local dot-commands: `.help`, `.quit`, `.key NAME`, `.boot` (= `sendkey ret`).
Anything else goes to the QEMU monitor verbatim.

Default socket is `/tmp/qemu-mon.sock` (see `--xr-demo`); override with
`--monitor PATH`.
