#ifndef __LINUX_GPIO_CONSUMER_H__
#define __LINUX_GPIO_CONSUMER_H__

/*
 * DCL <linux/gpio/consumer.h> -- the direction enum and the two calls
 * serial_core.c makes for RS485 transceiver control.
 *
 * There is no GPIO controller in Xeneva, and this header is the whole
 * distance between that fact and a driver that must still compile:
 *
 *   devm_gpiod_get_optional()  returns NULL, which is mainline's own answer
 *                              for an *optional* descriptor on a system with
 *                              no line behind that name -- the callers at
 *                              serial_core.c:3573 and :3583 read it into a
 *                              desc and hand it straight to...
 *   gpiod_set_value_cansleep() ...which does nothing.  Both are written to
 *                              tolerate a NULL desc, because that is the only
 *                              value they will ever be given here.
 *
 * So the RS485 term and rx-during-tx lines are absent rather than simulated,
 * which is what an unconnected transceiver-enable pin is: nothing to assert,
 * nothing to deassert, and the "works fine for short cables" comment at
 * :3571 becomes "works for the half-duplex case nobody configured".
 *
 * The enum is mainline's bit-for-bit (the GPIOD_FLAGS_BIT_* values matter --
 * `gpiod_get` returns -EPROBE_DEFER based on them upstream, and a wrong
 * number here would be read back by nothing but is cheap to keep right).
 *
 *   upstream  include/linux/gpio/consumer.h  (mainline v7.2)
 */

struct device;
struct gpio_desc;

/* mainline's flag bits, verbatim: these are OR-able inputs to gpiod_get(),
 * and GPIOD_OUT_* are defined in terms of them rather than as fresh numbers so
 * the two spellings can never disagree. */
#define GPIOD_FLAGS_BIT_DIR_SET		(1 << 0)
#define GPIOD_FLAGS_BIT_DIR_OUT		(1 << 1)
#define GPIOD_FLAGS_BIT_DIR_VAL		(1 << 2)
#define GPIOD_FLAGS_BIT_OPEN_DRAIN	(1 << 3)

enum gpiod_flags {
	GPIOD_ASIS	= 0,
	GPIOD_IN	= GPIOD_FLAGS_BIT_DIR_SET,
	GPIOD_OUT_LOW	= GPIOD_FLAGS_BIT_DIR_SET | GPIOD_FLAGS_BIT_DIR_OUT,
	GPIOD_OUT_HIGH	= GPIOD_FLAGS_BIT_DIR_SET | GPIOD_FLAGS_BIT_DIR_OUT |
			  GPIOD_FLAGS_BIT_DIR_VAL,
	GPIOD_OUT_LOW_OPEN_DRAIN = GPIOD_OUT_LOW | GPIOD_FLAGS_BIT_OPEN_DRAIN,
	GPIOD_OUT_HIGH_OPEN_DRAIN = GPIOD_OUT_HIGH | GPIOD_FLAGS_BIT_OPEN_DRAIN,
};

static inline struct gpio_desc* devm_gpiod_get_optional(struct device* dev,
							const char* con_id,
							enum gpiod_flags flags)
{
	(void)dev;
	(void)con_id;
	(void)flags;
	return NULL;	/* no controller: the line is simply not there */
}

static inline void gpiod_set_value_cansleep(struct gpio_desc* desc, int value)
{
	(void)desc;
	(void)value;
}

#endif /* __LINUX_GPIO_CONSUMER_H__ */
