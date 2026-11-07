/**
 * Shared kernel/user ABI between the kernel input layer and a user-space input
 * server (Process/inputsrv), via /dev/inputsrv (daemons group only).
 *
 * The server owns the input hardware through /dev/hwdev and does all the device
 * work: virtio negotiation, interrupts, and decoding raw device events. It hands
 * the kernel DECODED events, which the kernel feeds into the same input layer the
 * in-kernel drivers used (AuDevWriteKybrd / AuDevWriteMice), so the compositor and
 * everything else is unaware of the difference.
 */
#ifndef __INPUTSRV_H__
#define __INPUTSRV_H__

#include <stdint.h>

#define INPUTSRV_IOC_INJECT  0x5401          /* arg: InputsrvEvent */
#define INPUTSRV_IOC_STATS   0x5402          /* arg: InputsrvStats */

#define INPUTSRV_OK        0
#define INPUTSRV_E_PERM   -1
#define INPUTSRV_E_INVAL  -2

#define INPUTSRV_KEY   1    /* code = scancode, bit 7 set = key release */
#define INPUTSRV_ABS   2    /* x/y are raw absolute values in 0..maxx/maxy */

/* button_state bits, identical to the in-kernel tablet driver's */
#define INPUTSRV_BTN_LEFT         0x01
#define INPUTSRV_BTN_RIGHT        0x02
#define INPUTSRV_BTN_MIDDLE       0x04
#define INPUTSRV_BTN_SCROLL_UP    0x05
#define INPUTSRV_BTN_SCROLL_DOWN  0x06

typedef struct {
	uint8_t  type;
	uint8_t  buttons;
	uint16_t reserved;
	uint32_t code;        /* INPUTSRV_KEY: scancode */
	int32_t  x, y;        /* INPUTSRV_ABS: raw position */
	uint32_t maxx, maxy;  /* INPUTSRV_ABS: the device's range; the kernel scales to the screen */
} InputsrvEvent;

typedef struct { uint64_t keys, mice, rejected; } InputsrvStats;

#endif
