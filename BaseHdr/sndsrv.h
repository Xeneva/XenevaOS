/**
 * Shared kernel/user ABI between the kernel sound proxy card and a user-space
 * sound server (Process/sndsrv).
 *
 * The server allocates one DMA buffer through /dev/hwdev, lays a SndRingHdr at
 * its start and a power-of-two data area at SNDSRV_DATA_OFFSET, then passes
 * the buffer's physical address to /dev/sndsrv (SNDSRV_IOC_ATTACH). The kernel
 * only accepts a buffer that really is one of the caller's own DMA
 * allocations. From then on the kernel sound card "virtio-sound" copies the
 * mixer's PCM into the ring (single producer) and the server drains it into
 * the device (single consumer). head/tail are free-running byte counters.
 */
#ifndef __SNDSRV_H__
#define __SNDSRV_H__

#include <stdint.h>

#define SNDSRV_RING_MAGIC   0x31565253u      /* 'SRV1' */
#define SNDSRV_DATA_OFFSET  4096

#define SNDSRV_IOC_ATTACH   0x5301           /* arg: SndsrvAttach */
#define SNDSRV_IOC_DETACH   0x5302
#define SNDSRV_IOC_STATS    0x5303           /* arg: SndsrvStats  */

#define SNDSRV_OK        0
#define SNDSRV_E_PERM   -1
#define SNDSRV_E_INVAL  -2
#define SNDSRV_E_BUSY   -3

typedef struct {
	uint32_t magic;
	uint32_t size;                /* data area bytes, a power of two */
	uint32_t period;              /* PCM period in bytes */
	uint32_t reserved;
	volatile uint32_t head;       /* bytes produced by the kernel */
	volatile uint32_t tail;       /* bytes consumed by the server */
	volatile uint32_t running;    /* 1 between start_output and stop_output */
	volatile uint32_t dropped;    /* bytes the kernel discarded: server too slow */
} SndRingHdr;

typedef struct { uint64_t ring_phys; uint64_t ring_len; } SndsrvAttach;
typedef struct { uint64_t written; uint64_t dropped; uint64_t calls; } SndsrvStats;

#endif
