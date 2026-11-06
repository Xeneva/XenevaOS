/*
 * DCL i2c.h -- drm_edid's DDC read path.
 *
 * struct i2c_msg matches mainline's layout so drm_edid.c's designated
 * initializers parse; i2c_transfer() fails with -ENOSYS: there is no i2c
 * adapter behind vkms (its connector has no ddc, so probe stops before the
 * transfer) and no i2c anywhere else in this tree's drm set.
 *
 * struct i2c_adapter is defined rather than only forward-declared for the one
 * member drm_edid.c reads: the failure path of drm_ddc_read_agx() logs
 * "skipping non-existent adapter %s" with adapter->name (drm_edid.c:2209),
 * which needs the complete type. mainline's adapter is a few dozen members
 * covering the algo, the locks, the device link and the clock; none of that
 * is reachable here because no adapter is ever constructed -- i2c_transfer()
 * below ignores its first argument entirely. The array form of name (rather
 * than a char *) is mainline's, kept so that when an adapter does arrive the
 * strscpy() into it still has somewhere to put the bytes.
 */
#ifndef _LINUX_I2C_H
#define _LINUX_I2C_H

#include <linux/types.h>

#define I2C_M_RD	0x0001
#define I2C_NAME_SIZE	50

struct i2c_msg {
	__u16 addr;
	__u16 flags;
	__u16 len;
	__u8 *buf;
};

struct i2c_adapter {
	char name[I2C_NAME_SIZE];
};

static inline int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			       int num)
{
	(void)adap; (void)msgs; (void)num;
	return -ENOSYS;
}

#endif /* _LINUX_I2C_H */
