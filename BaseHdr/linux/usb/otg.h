#ifndef __LINUX_USB_OTG_H__
#define __LINUX_USB_OTG_H__

#include <linux/usb/ch9.h>

#define OTG_VERSION_1_3 0x0130
#define OTG_VERSION_2_0 0x0200

enum usb_dr_mode {
	USB_DR_MODE_UNKNOWN,
	USB_DR_MODE_HOST,
	USB_DR_MODE_PERIPHERAL,
	USB_DR_MODE_OTG,
};

struct usb_otg_caps {
	u16 otg_rev;
	bool hnp_support;
	bool srp_support;
	bool adp_support;
};

enum usb_otg_state {
	OTG_STATE_UNDEFINED = 0,
	OTG_STATE_B_IDLE,
	OTG_STATE_B_SRP_INIT,
	OTG_STATE_B_PERIPHERAL,
	OTG_STATE_B_WAIT_ACON,
	OTG_STATE_B_HOST,
	OTG_STATE_A_IDLE,
	OTG_STATE_A_WAIT_VRISE,
	OTG_STATE_A_WAIT_BCON,
	OTG_STATE_A_HOST,
	OTG_STATE_A_SUSPEND,
	OTG_STATE_A_PERIPHERAL,
	OTG_STATE_A_WAIT_VFAIL,
	OTG_STATE_A_VBUS_ERR,
};

struct usb_otg {
	u8 default_a;
	struct usb_phy* phy;
	struct usb_bus* host;
	struct usb_gadget* gadget;
	enum usb_otg_state state;

	int (*set_host)(struct usb_otg* otg, struct usb_bus* host);

};

#endif