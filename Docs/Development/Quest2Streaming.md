# Quest 2 streaming (Linux host)

XenevaOS stays in QEMU. The Quest 2 is a display. OpenXR runs **on the host** via [WiVRn](https://wivrn.github.io/).

```
DeodhaiXR (guest scanout)
  → xeneva-xr-view (host OpenXR quad)
  → WiVRn / Monado
  → Quest 2 client
```

DeodhaiXR **does** emit OpenXR (`xrWaitFrame` / swapchains / `xrEndFrame`) when built with `OPENXR=1`. The QEMU in-process runtime (`xr_qemu.cpp`) is a stand-in for a vendor runtime on embedded hardware. It transports the flat desktop at full scanout resolution; the host viewer presents it to both eyes while WiVRn runs on the **host**.

```bash
Scripts/Linux/build_and_run_qemu.sh --llvm --xr-demo
# opens the XR TUI; Enter on Launch uses the recommended profile
export XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json
Tools/xeneva-xr-view/xeneva-xr-view --egl   # HMD, true framebuffer, ocular L|R
Tools/xeneva-xr-client/xeneva-xr-client     # interactive monitor (keys, info)
```

The default profile starts a VNC-compatible CPU-backed QEMU D-Bus display,
selects 1024x768 automatically, opens localhost VNC, and enables right-hand pointer/pinch,
right-controller pointer/trigger, and the solid two-hand joint mesh. The TUI can independently
toggle those modules, VNC, the telnet monitor, guest variants, resolution, and
pointer gain. Scaling is selectable: `sharp` uses nearest-neighbor sampling for
crisp desktop text and is the default; `smooth` uses linear sampling for images.
Press `D` to restore defaults. For scripts and CI, use:

```bash
Scripts/Linux/build_and_run_qemu.sh --xr-demo-defaults
Scripts/Linux/xr-demo.sh --print-defaults
```

QEMU cannot combine VNC with an active GL display context. Turning VNC off in
the TUI selects `egl-headless` plus DMA-BUF instead; both transports feed the
same OpenXR viewer.

Choose `manual` resolution if you want to operate the bootloader yourself via
`xeneva-xr-client` or the default VNC service.
GOP entries (640x480–1024x768) come from firmware; `*` entries
(1280x720–1920x1080) are manual desktop overrides the GPU driver applies
independently of GOP. The viewer resizes its swapchain to whatever the
guest emits. Note: 1080p costs ~50ms/frame of guest CPU compose time
(vs ~8ms at 640x480), and dbus ships ~8MB per full frame, damage-driven.

QEMU carries one full-resolution flat desktop. The XR demo passes `--mono` so
both eyes sample that complete frame instead of losing every other horizontal
pixel to an SBS squeeze. `--stereo` remains available for external SBS sources.

## Install

PC: `flatpak install flathub io.github.wivrn.wivrn`
Headset: WiVRn from the Meta Store (Quest 2). Versions must match.

## Run

1. WiVRn dashboard + Quest client, wait until connected.
2. `Scripts/Linux/build_and_run_qemu.sh --llvm --xr-demo`: choose the modules,
   then launch. The default builds and boots the guest at 1024x768 and starts
   the fully wired EGL/OpenXR viewer.
   (A child process cannot export into your shell, so it also writes
   `/tmp/xeneva-xr-demo.env` — `source` it for manual viewer runs.)
3. Interact with the OS while streaming through the hand/controller pointer,
   or `Tools/xeneva-xr-client/xeneva-xr-client` (scriptable REPL:
   `.boot`, `.key ret`, `info status`, ...).
   Enable VNC or the telnet monitor in the TUI only when those services are
   useful.
4. The default viewer invocation drives the guest cursor
   from right-hand tracking (aim position + pinch), `--controllers` does
   the same from the right controller (aim + trigger, wins while valid);
   and displays both tracked hand meshes. Injected over D-Bus like VNC input. Needs hand
   tracking enabled in WiVRn and on the Quest for `--hands`.

Details: `Tools/xeneva-xr-view/README.md`, `Tools/xeneva-xr-client/README.md`.

## Steal frames (EGL / DMA-BUF, not VNC)

```bash
Scripts/Linux/build_and_run_qemu.sh --llvm --egl-headless
Tools/xeneva-xr-view/xeneva-xr-view --desktop --egl
```

QEMU exports `org.qemu.Display1` on `$XDG_RUNTIME_DIR/xeneva-qemu-display`. The viewer registers a Listener and prefers `ScanoutDMABUF` (EGL import + FBO read). If virtio-gpu 2D only sends pixman `Scanout`, that blob is used instead. Do not VNC-recapture.
