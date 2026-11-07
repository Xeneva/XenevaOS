/* GPUSRV=1: asynchronous virtio-gpu presentation over /dev/gpusrv.
 *
 * /dev/graph maps a kernel-owned, tightly packed desktop framebuffer. It is
 * stable across server restarts and mirrored to GOP on each damage update.
 * ACQUIRE requires the caller's hwdev capability for 1af4:1050. The ABI never
 * returns a physical framebuffer address owned by the kernel: the server
 * supplies DMA backing pages allocated through its own /dev/hwdev capability.
 * TAKE copies pending damage from the stable kernel framebuffer into that
 * backing, then freezes the frame until ACK.
 * Only one frame may be in flight. New damage coalesces while it is busy.
 * The server must reset the device before ACQUIRE and close its hwdev handle
 * after any failed/timed-out command. It must never ACK unfinished device I/O.
 * The kernel never waits for the server and never frees either framebuffer.
 */
#ifndef __GPUSRV_ABI_H__
#define __GPUSRV_ABI_H__
#include <stdint.h>

#define GPUSRV_VERSION 1
#define GPUSRV_VENDOR 0x1AF4
#define GPUSRV_DEVICE 0x1050
#define GPUSRV_RESOURCE 1
#define GPUSRV_MAX_DIM 4096
#define GPUSRV_MAX_BACKING 16
#define GPUSRV_MAX_BACKING_BYTES (1024 * 1024)

#define GPUSRV_IOC_ACQUIRE 0x5801
#define GPUSRV_IOC_TAKE    0x5802
#define GPUSRV_IOC_ACK     0x5803
#define GPUSRV_IOC_STATUS  0x5804
/* Damage is deliberately kept at the old compositor value: the node is
 * different (/dev/gpusrv rather than /dev/virtiogpu), so existing private
 * ioctl callers can migrate without another number translation. */
#define GPUSRV_IOC_DAMAGE  0x202
#define GPUSRV_IOC_FLUSH   0x203
#define GPUSRV_IOC_RESOURCE 0x204

#define GPUSRV_OK 0
#define GPUSRV_E_PERM -1
#define GPUSRV_E_INVAL -2
#define GPUSRV_E_BUSY -3
#define GPUSRV_E_EMPTY -4
#define GPUSRV_E_UNAVAILABLE -5

typedef struct {
	uint32_t x, y, width, height;
} GpuSrvDamage;

typedef struct {
	uint64_t phys;
	uint32_t length;
	uint32_t reserved;
} GpuSrvBacking;

typedef struct {
	uint32_t version;
	int32_t hw_handle;
	uint32_t width, height, pitch, resource;
	uint64_t backing_bytes, generation;
	uint32_t backing_count, reserved;
	GpuSrvBacking backing[GPUSRV_MAX_BACKING];
} GpuSrvAcquire;

typedef struct {
	uint64_t sequence;
	uint32_t x, y, width, height;
} GpuSrvFrame;

typedef struct {
	uint64_t sequence;
} GpuSrvAck;

typedef struct {
	uint32_t version, attached, ready, reserved;
	uint32_t width, height, pitch, resource;
	uint64_t submitted, completed, generation;
} GpuSrvStatus;
#endif
