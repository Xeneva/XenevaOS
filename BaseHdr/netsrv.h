/**
 * netsrv.h -- shared kernel/user ABI between the kernel network proxy
 * (KernelAA64/Net/netproxy.c) and a user-space NIC server (Process/netsrv).
 *
 * The server allocates one DMA buffer through /dev/hwdev, lays a NetRingHdr at
 * its start followed by two rings of fixed-size frame slots, and passes the
 * buffer's physical address to /dev/netsrv (NETSRV_IOC_ATTACH). The kernel only
 * accepts a buffer that is one of the caller's own DMA allocations. The kernel
 * then registers the adapters "e1000" and "virtio-net" (what the in-kernel
 * driver registered) and the network stack cannot tell the difference.
 *
 *   TX ring: kernel produces (tx_head), server consumes (tx_tail)
 *   RX ring: server produces (rx_head), kernel consumes (rx_tail)
 *
 * head/tail are free-running slot counters. A slot is a u32 frame length, a
 * u32 pad, then the Ethernet frame. The kernel never trusts a length read
 * from the ring and copies a frame out before parsing it.
 */
#ifndef __NETSRV_H__
#define __NETSRV_H__

#include <stdint.h>

#define NETSRV_RING_MAGIC    0x3154454Eu     /* 'NET1' */
#define NETSRV_SLOTS         32              /* per ring, a power of two */
#define NETSRV_SLOT_SIZE     2048
#define NETSRV_SLOT_HDR      8
#define NETSRV_MAX_FRAME     (NETSRV_SLOT_SIZE - NETSRV_SLOT_HDR)
#define NETSRV_TX_OFFSET     4096
#define NETSRV_RX_OFFSET     (NETSRV_TX_OFFSET + NETSRV_SLOTS * NETSRV_SLOT_SIZE)
#define NETSRV_RING_BYTES    (NETSRV_RX_OFFSET + NETSRV_SLOTS * NETSRV_SLOT_SIZE)

#define NETSRV_IOC_ATTACH    0x5501          /* arg: NetsrvAttach */
#define NETSRV_IOC_DETACH    0x5502
#define NETSRV_IOC_STATS     0x5503          /* arg: NetsrvStats  */

#define NETSRV_OK        0
#define NETSRV_E_PERM   -1
#define NETSRV_E_INVAL  -2
#define NETSRV_E_BUSY   -3

typedef struct {
	uint32_t magic;
	uint32_t slots;               /* NETSRV_SLOTS */
	uint32_t slot_size;           /* NETSRV_SLOT_SIZE */
	volatile uint32_t link;       /* 1 = link up (server writes) */
	uint8_t  mac[6];              /* filled in by the server before ATTACH */
	uint16_t reserved;
	volatile uint32_t tx_head, tx_tail;
	volatile uint32_t rx_head, rx_tail;
	volatile uint32_t tx_dropped;     /* frames the kernel discarded: ring full */
	volatile uint32_t rx_dropped;     /* frames the server discarded: ring full */
} NetRingHdr;

typedef struct { uint64_t ring_phys; uint64_t ring_len; } NetsrvAttach;
typedef struct { uint64_t tx_frames, rx_frames, tx_dropped, rx_bad; } NetsrvStats;

#endif
