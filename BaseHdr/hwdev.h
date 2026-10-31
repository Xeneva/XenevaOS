/**
 * Shared kernel/user ABI for /dev/hwdev -- hardware access for user-space drivers.
 *
 * Model: a process in the MISC_DAEMONS group (or root) opens /dev/hwdev and
 * asks for a PCI function with HWDEV_IOC_PCI_OPEN. On success it gets back a
 * NEW file descriptor, a capability for exactly that one device. Every
 * further operation (config access, BAR mapping, DMA buffers) is an ioctl on
 * that descriptor, and everything it grants is bounded by the device:
 *
 *   - config writes are limited to the command register and capability space,
 *     never the BARs, so a driver cannot re-point a device at kernel memory;
 *   - BAR mappings must lie inside a BAR of that device;
 *   - only PCI functions on the kernel's allowlist can be opened at all
 *     (default deny), and one that an in-kernel driver still owns is refused
 *     with HWDEV_E_BUSY: unload that driver first (see /dev/drvctl).
 *
 * Closing the descriptor, or exiting, unmaps and frees everything granted.
 */
#ifndef __HWDEV_H__
#define __HWDEV_H__

#include <stdint.h>

#define HWDEV_IOC_PCI_OPEN    0x4801   /* on /dev/hwdev; arg: HwdevPciOpen */
#define HWDEV_IOC_CFG_READ    0x4802   /* on a handle;   arg: HwdevCfg     */
#define HWDEV_IOC_CFG_WRITE   0x4803   /* on a handle;   arg: HwdevCfg     */
#define HWDEV_IOC_BAR_INFO    0x4804   /* on a handle;   arg: HwdevBar     */
#define HWDEV_IOC_MAP_BAR     0x4805   /* on a handle;   arg: HwdevMap     */
#define HWDEV_IOC_DMA_ALLOC   0x4806   /* on a handle;   arg: HwdevDma     */
#define HWDEV_IOC_IRQ_SETUP   0x4807   /* on a handle;   arg: HwdevIrq     */
#define HWDEV_IOC_IRQ_WAIT    0x4808   /* on a handle;   arg: HwdevIrqWait */
#define HWDEV_IOC_IRQ_ACK     0x4809   /* on a handle;   arg: HwdevIrqAck  */

#define HWDEV_OK        0
#define HWDEV_E_PERM   -1   /* caller is not a driver process / lacks the right */
#define HWDEV_E_NODEV  -2   /* no such PCI function */
#define HWDEV_E_BUSY   -3   /* owned by an in-kernel driver or another process */
#define HWDEV_E_INVAL  -4   /* bad argument, out-of-range, or forbidden offset */
#define HWDEV_E_NOMEM  -5

#define HWDEV_BAR_MEM     0x1
#define HWDEV_BAR_IO      0x2
#define HWDEV_BAR_64BIT   0x4

/* instance selects the Nth function with that vendor/device id (0 = first), for
 * machines with several identical devices, such as a virtio keyboard and tablet */
typedef struct { uint16_t vendor; uint16_t device; int32_t handle; uint32_t instance; uint32_t reserved; } HwdevPciOpen;
typedef struct { uint32_t offset; uint32_t size; uint32_t value; }   HwdevCfg;   /* size 1, 2 or 4 */
typedef struct { uint32_t bar; uint32_t flags; uint64_t base; uint64_t size; } HwdevBar;
typedef struct { uint32_t bar; uint32_t reserved; uint64_t offset; uint64_t length; uint64_t uaddr; } HwdevMap;
typedef struct { uint64_t length; uint64_t uaddr; uint64_t phys; }   HwdevDma;

/* Interrupts. IRQ_SETUP points the device's MSI/MSI-X at a kernel-allocated
 * interrupt; every vector the device uses then arrives as the same interrupt,
 * so the driver assigns MSI-X vector index 0 to its queues. IRQ_WAIT sleeps
 * until an interrupt arrives or timeout_ms passes (a short timeout bounds any
 * lost wakeup); its own return value is not reliable after a sleep, so read
 * the result with IRQ_ACK, which returns and clears the pending count. */
typedef struct { uint32_t spi; uint32_t reserved; }                  HwdevIrq;
typedef struct { uint32_t timeout_ms; uint32_t reserved; }           HwdevIrqWait;
typedef struct { uint32_t pending; uint32_t reserved; uint64_t total; } HwdevIrqAck;

#endif
