#ifndef QEMU_EGL_CAPTURE_H
#define QEMU_EGL_CAPTURE_H

#include <cstdint>

/* Steal QEMU scanout via dbus Display1 (ScanoutDMABUF / CPU Scanout).
 * Pair with: qemu -display dbus [-vnc :0] -monitor unix:PATH,server,nowait
 */
bool qemu_egl_connect(const char* dbus_addr);
bool qemu_egl_poll(uint32_t** rgba, int* w, int* h);
uint64_t qemu_egl_last_capture_ns();
/* Absolute pointer + buttons on the registered console (same path VNC
 * uses). No-ops until qemu_egl_connect succeeds. Coordinates are guest
 * framebuffer pixels. */
void qemu_mouse_abs(uint32_t x, uint32_t y);
void qemu_mouse_button(uint32_t button, bool down);

#endif
