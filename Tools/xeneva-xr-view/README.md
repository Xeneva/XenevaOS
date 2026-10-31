# xeneva-xr-view

Host-only OpenXR tool: shows the live QEMU desktop as a world-locked quad in a Quest 2 (or any OpenXR HMD). **Not** part of the guest image. The XR runtime stays on Linux (WiVRn).

## Stage 0 — headset to PC

```bash
flatpak install flathub io.github.wivrn.wivrn
flatpak run io.github.wivrn.wivrn
```

On the Quest 2, install **WiVRn** from the Meta Store (same version as the PC). Connect over 5 GHz Wi-Fi or USB+ADB. Confirm a stock OpenXR sample before involving Xeneva.

WiVRn sets `XR_RUNTIME_JSON` while the headset is connected.

## Laptop preview (no headset)

```bash
make -C Tools/xeneva-xr-view
# live QEMU GTK window (title matching qemu, case-insensitive):
Tools/xeneva-xr-view/xeneva-xr-view --desktop --x11
# stereo test chart only (checker + yellow disc, not a rainbow):
Tools/xeneva-xr-view/xeneva-xr-view --desktop --pattern
```

Preferred capture (no X11, no VNC): QEMU `egl-headless` + dbus DMA-BUF.

```bash
Scripts/Linux/build_and_run_qemu.sh --llvm --egl-headless
Tools/xeneva-xr-view/xeneva-xr-view --desktop --egl
```

That imports `ScanoutDMABUF` through EGL (`eglCreateImageKHR` / `GL_TEXTURE_2D`) and falls back to the CPU `Scanout` blob if QEMU does not export a dmabuf (2D virtio-gpu without virgl).

Side-by-side by default (white seam down the middle, green near-squares offset by IPD). `--anaglyph` for red/cyan. `[` `]` IPD, `s` layout, `q` quit. This is a fake two-eye shift of one 2D plane, not Quest depth.

## Stage 1 — Xeneva as a floating panel

1. Boot the guest: `Scripts/Linux/build_and_run_qemu.sh --llvm`
2. Leave the QEMU GTK window visible (title contains `QEMU`).
3. With WiVRn connected:

```bash
make -C Tools/xeneva-xr-view
Tools/xeneva-xr-view/xeneva-xr-view --x11
```

Optional:

- `--window SUBSTR`  match a different window title
- `--ppm FILE`       read a `P6` PPM (for a dump path later)
- `--pattern`        RGB test card (no QEMU)
- `--size WxH`       test-pattern size

The quad sits about 2 m in front of headset-relative `VIEW` space, with an
explicit layer for each eye. Head motion is applied by the runtime (timewarp).
The guest does not need IMU yet.

### Hand and controller pointer

With QEMU D-Bus capture active, the viewer can inject an absolute guest mouse
while treating the right hand or controller like a floating trackpad:

```bash
Tools/xeneva-xr-view/xeneva-xr-view --egl --hands
Tools/xeneva-xr-view/xeneva-xr-view --egl --controllers
Tools/xeneva-xr-view/xeneva-xr-view --egl --hands --controllers
```

`--hands` uses right-hand aim position plus pinch and displays a simple solid
hand for both tracked hands. `--controllers` uses right-controller aim plus
trigger and takes priority while its pose is valid. Both pointer sources use
adaptive jitter filtering; `--gain N` controls controller pixels per meter
(default 20000), while bare-hand input uses half that gain to suppress tremor.
Pass `--no-hand-mesh` to keep hand input without the visual, or
`--hand-mesh` to show tracked hands without enabling hand pointer input.

### Guest input ring

The XR compositor consumes the non-blocking `/dev/input-ring` stream when it is
available. Virtio tablet and keyboard producers publish fixed-size input records
to a bounded SPSC queue; motion may be coalesced under pressure, while button
and keyboard edges are retained. Legacy `/dev/mice` and `/dev/kybrd` remain the
fallback for older images.

The mesh uses standard `XR_EXT_hand_tracking` joints. If the runtime does not
offer hand interaction or joint tracking, that feature is disabled while the
panel and any other requested input path continue running.

The current DeodhaiXR guest transports its flat desktop at full resolution and
the XR demo selects `--mono`, showing that complete frame to both eyes. Use
`--stereo` only for an external side-by-side L|R source.

The HMD path scales into the runtime's recommended native per-eye allocation.
`--filter nearest` (default) keeps desktop text crisp; `--filter linear`
smooths image-heavy content. The XR TUI exposes these as `sharp` and `smooth`.

## Headless (xr-demo)

```bash
Scripts/Linux/build_and_run_qemu.sh --llvm --xr-demo  # interactive feature TUI
# or use the complete recommended profile directly:
Scripts/Linux/build_and_run_qemu.sh --llvm --xr-demo-defaults
export XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json
Tools/xeneva-xr-view/xeneva-xr-view --egl   # true guest framebuffer over dbus
Tools/xeneva-xr-client/xeneva-xr-client     # interactive monitor (keys, info)
```

No GTK window: QEMU runs its D-Bus display and a Unix monitor socket. The
default VNC-enabled profile uses CPU scanout because QEMU cannot combine VNC
with a GL display context; disabling VNC selects `egl-headless` DMA-BUF. The
default profile selects 1024x768, sharp scaling, and enables hand pointer,
controllers, tracked hand meshes, and localhost VNC. The launcher keeps the
viewer on WiVRn's GPU so its OpenGL swapchain textures share one Mesa device.
The telnet monitor remains an optional TUI module.
`--egl` steals the real scanout (CPU `Scanout` blobs, or DMA-BUF when EGL
import works), not an X11 screenshot.

## Stage 2 (not this tool)

Stereo IPD split is a DeodhaiXR side-by-side dump plus two projection views. Do that only after Stage 1 is readable in the headset.

## Dependencies

`libopenxr-loader`, X11, GLX (`pkg-config openxr x11 gl`).
