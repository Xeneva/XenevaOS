/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/
#include <aurora.h>
#include <Drivers/uart.h>
#include <string.h>
#include "dwc2_hw.h"
#include "usb.h"
#include "usb_hid.h"

/*
 * Device enumeration and the USB 2.0 hub class driver.
 *
 * Enumeration (USB 2.0 9.1.2):
 * GET_DESCRIPTOR(device, 8) at address 0 for bMaxPacketSize0, SET_ADDRESS,
 * 10 ms, GET_DESCRIPTOR(device), GET_DESCRIPTOR(config) twice (header, then
 * wTotalLength), SET_CONFIGURATION, then hand each interface to its class
 * driver. Hubs are configured recursively (USB 2.0 chapter 11): power the
 * ports, wait bPwrOn2PwrGood, debounce 100 ms, reset each connected port
 * until it reports enabled, clear the change bits, enumerate the child.
 *
 * On the Raspberry Pi 3B+ the root port carries the LAN7515's USB2514 hub,
 * whose port 1 has a second USB2514 hub and the LAN7800 ethernet on it.
 * The external ports hang off those two hubs.
 */

static usb_device_t usb_devices[USB_MAX_DEVICES];
static uint8_t usb_next_address = 1;
static uint8_t usb_root_speed;
/* descriptor scratch, also the target of GET_DESCRIPTOR */
static uint8_t usb_desc_buf[1024] __attribute__((aligned(64)));

static const char* usb_speed_name(uint8_t speed) {
	switch (speed) {
	case DWC2_SPEED_HIGH:
		return "high";
	case DWC2_SPEED_FULL:
		return "full";
	default:
		return "low";
	}
}

static usb_device_t* usb_alloc_device() {
	for (int i = 0; i < USB_MAX_DEVICES; i++) {
		if (!usb_devices[i].used) {
			memset(&usb_devices[i], 0, sizeof(usb_device_t));
			usb_devices[i].used = 1;
			return &usb_devices[i];
		}
	}
	return NULL;
}

/*
 * usb_setup_split -- a full/low speed device that sits below a high speed
 * hub is reached through that hub's transaction translator: find the
 * nearest high speed hub upstream and the port leading to the device
 * (USB 2.0 11.14: the TT of the closest high speed hub)
 */
static void usb_setup_split(usb_device_t* dev) {
	dev->split = 0;
	if (dev->speed == DWC2_SPEED_HIGH || usb_root_speed != DWC2_SPEED_HIGH)
		return;
	usb_device_t* child = dev;
	usb_device_t* hub = dev->parent;
	while (hub && hub->speed != DWC2_SPEED_HIGH) {
		child = hub;
		hub = hub->parent;
	}
	if (!hub)
		return;
	dev->split = 1;
	dev->tt_hub = hub->address;
	dev->tt_port = child->port;
}

static int usb_get_descriptor(usb_device_t* dev, uint8_t type, uint8_t index, void* buf,
							  uint16_t len) {
	return usb_control_msg(dev, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
						   USB_REQ_GET_DESCRIPTOR, (uint16_t)((type << 8) | index), 0, buf, len);
}

static int usb_new_device(usb_device_t* parent, uint8_t port, uint8_t speed);

/* ------------------------------------------------------------------
 * hub class driver
 * ------------------------------------------------------------------ */

static int hub_port_status(usb_device_t* hub, uint8_t port, usb_port_status_t* st) {
	int ret = usb_control_msg(hub, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_OTHER,
							  USB_REQ_GET_STATUS, 0, port, st, sizeof(*st));
	return (ret == (int)sizeof(*st)) ? USB_OK : (ret < 0 ? ret : USB_EIO);
}

static int hub_port_feature(usb_device_t* hub, uint8_t port, uint8_t request, uint16_t feature) {
	int ret = usb_control_msg(hub, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER, request,
							  feature, port, NULL, 0);
	return ret < 0 ? ret : USB_OK;
}

/*
 * hub_port_reset -- reset a port and wait until it is enabled (USB 2.0
 * 11.5.1.5: 10..20 ms reset, polled every 10 ms)
 */
static int hub_port_reset(usb_device_t* hub, uint8_t port, usb_port_status_t* st) {
	for (int attempt = 0; attempt < 3; attempt++) {
		if (hub_port_feature(hub, port, USB_REQ_SET_FEATURE, USB_PORT_FEAT_RESET) != USB_OK)
			return USB_EIO;
		for (int t = 0; t < 50; t++) {
			dwc2_delay_ms(10);
			if (hub_port_status(hub, port, st) != USB_OK)
				continue;
			if (!(st->wPortStatus & USB_PORT_STAT_RESET) &&
				(st->wPortStatus & USB_PORT_STAT_ENABLE)) {
				hub_port_feature(hub, port, USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_RESET);
				/* TRSTRCY: 10 ms reset recovery before the first request */
				dwc2_delay_ms(10);
				return USB_OK;
			}
			if (!(st->wPortStatus & USB_PORT_STAT_CONNECTION))
				return USB_ENODEV;
		}
	}
	return USB_ETIME;
}

static int usb_hub_configure(usb_device_t* hub) {
	usb_hub_desc_t hd;
	memset(&hd, 0, sizeof(hd));
	int ret = usb_control_msg(hub, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE,
							  USB_REQ_GET_DESCRIPTOR, USB_DT_HUB << 8, 0, &hd, sizeof(hd));
	if (ret < (int)sizeof(hd)) {
		UARTDebugOut("[usb]: hub %d: no hub descriptor (%d) \r\n", hub->address, ret);
		return USB_EIO;
	}
	uint32_t pgood_ms = (uint32_t)hd.bPwrOn2PwrGood * 2;
	if (pgood_ms < 100)
		pgood_ms = 100;
	UARTDebugOut("[usb]: hub %d: %d ports \r\n", hub->address, hd.bNbrPorts);

	for (uint8_t p = 1; p <= hd.bNbrPorts; p++)
		hub_port_feature(hub, p, USB_REQ_SET_FEATURE, USB_PORT_FEAT_POWER);
	dwc2_delay_ms(pgood_ms);

	for (uint8_t p = 1; p <= hd.bNbrPorts; p++) {
		usb_port_status_t st;
		if (hub_port_status(hub, p, &st) != USB_OK)
			continue;
		if (st.wPortChange & USB_PORT_STAT_C_CONNECTION)
			hub_port_feature(hub, p, USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_CONNECTION);
		if (!(st.wPortStatus & USB_PORT_STAT_CONNECTION))
			continue;

		/* connect debounce (USB 2.0 7.1.7.3, TATTDB) */
		dwc2_delay_ms(100);
		if (hub_port_reset(hub, p, &st) != USB_OK) {
			UARTDebugOut("[usb]: hub %d port %d: reset failed \r\n", hub->address, p);
			continue;
		}
		hub_port_feature(hub, p, USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_ENABLE);

		uint8_t speed = DWC2_SPEED_FULL;
		if (st.wPortStatus & USB_PORT_STAT_LOW_SPEED)
			speed = DWC2_SPEED_LOW;
		else if (st.wPortStatus & USB_PORT_STAT_HIGH_SPEED)
			speed = DWC2_SPEED_HIGH;
		usb_new_device(hub, p, speed);
	}
	return USB_OK;
}

/* ------------------------------------------------------------------
 * device enumeration
 * ------------------------------------------------------------------ */

/*
 * usb_configure_interfaces -- walk the configuration and start a driver
 * for every interface (alternate setting 0) we have one for
 */
static void usb_configure_interfaces(usb_device_t* dev, uint8_t* cfg, uint16_t total) {
	uint16_t off = 0;
	usb_interface_desc_t* intf = NULL;
	uint16_t hid_report_len = 0;
	while (off + 2 <= total) {
		uint8_t len = cfg[off];
		uint8_t type = cfg[off + 1];
		if (len < 2 || off + len > total)
			break;
		if (type == USB_DT_INTERFACE && len >= sizeof(usb_interface_desc_t)) {
			usb_interface_desc_t* d = (usb_interface_desc_t*)&cfg[off];
			intf = (d->bAlternateSetting == 0) ? d : NULL;
			hid_report_len = 0;
			if (intf && intf->bInterfaceClass == USB_CLASS_HUB) {
				usb_hub_configure(dev);
				return;
			}
		} else if (type == USB_DT_HID && intf && len >= 9) {
			/* HID descriptor: bNumDescriptors entries of (type, wLength)
			 * from offset 6; the report descriptor is type 0x22 */
			for (uint8_t i = 0; i < cfg[off + 5] && 6 + i * 3 + 2 < len; i++) {
				if (cfg[off + 6 + i * 3] == USB_DT_REPORT)
					hid_report_len = (uint16_t)(cfg[off + 7 + i * 3] | (cfg[off + 8 + i * 3] << 8));
			}
		} else if (type == USB_DT_ENDPOINT && intf && len >= sizeof(usb_endpoint_desc_t)) {
			usb_endpoint_desc_t* ep = (usb_endpoint_desc_t*)&cfg[off];
			if (intf->bInterfaceClass == USB_CLASS_HID && (ep->bmAttributes & 3) == DWC2_EP_INTR &&
				(ep->bEndpointAddress & USB_DIR_IN)) {
				usb_hid_attach(dev, intf, ep, hid_report_len);
				intf = NULL; /* one interrupt IN endpoint per HID interface */
			}
		}
		off += len;
	}
}

/**
 * @brief usb_new_device -- enumerate the device just reset on a port
 * @param parent -- hub it sits on, NULL for the root port
 * @param port -- port on that hub
 * @param speed -- DWC2_SPEED_*
 */
static int usb_new_device(usb_device_t* parent, uint8_t port, uint8_t speed) {
	uint8_t depth = parent ? parent->depth + 1 : 0;
	if (depth > USB_MAX_DEPTH)
		return USB_EIO;
	usb_device_t* dev = usb_alloc_device();
	if (!dev) {
		UARTDebugOut("[usb]: too many devices \r\n");
		return USB_EIO;
	}
	dev->parent = parent;
	dev->port = port;
	dev->depth = depth;
	dev->speed = speed;
	dev->address = 0;
	/* 8 bytes is a single packet for any bMaxPacketSize0 */
	dev->mps0 = 8;
	usb_setup_split(dev);

	int ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, usb_desc_buf, 8);
	if (ret < 8) {
		UARTDebugOut("[usb]: port %d (hub %d): no device descriptor (%d) \r\n", port,
					 parent ? parent->address : 0, ret);
		dev->used = 0;
		return USB_EIO;
	}
	uint8_t mps0 = usb_desc_buf[7];
	if (mps0 != 8 && mps0 != 16 && mps0 != 32 && mps0 != 64)
		mps0 = 8;

	uint8_t address = usb_next_address++;
	ret = usb_control_msg(dev, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
						  USB_REQ_SET_ADDRESS, address, 0, NULL, 0);
	if (ret < 0) {
		UARTDebugOut("[usb]: SET_ADDRESS %d failed (%d) \r\n", address, ret);
		dev->used = 0;
		return ret;
	}
	/* SetAddress recovery (USB 2.0 9.2.6.3: at least 2 ms) */
	dwc2_delay_ms(10);
	dev->address = address;
	dev->mps0 = mps0;

	ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dev->desc, sizeof(usb_device_desc_t));
	if (ret < (int)sizeof(usb_device_desc_t)) {
		UARTDebugOut("[usb]: device %d: short device descriptor (%d) \r\n", address, ret);
		return USB_EIO;
	}
	UARTDebugOut("[usb]: device %d: %x:%x class %x, %s speed, port %d of hub %d%s \r\n", address,
				 dev->desc.idVendor, dev->desc.idProduct, dev->desc.bDeviceClass,
				 usb_speed_name(speed), port, parent ? parent->address : 0,
				 dev->split ? " (split)" : "");

	ret = usb_get_descriptor(dev, USB_DT_CONFIG, 0, usb_desc_buf, sizeof(usb_config_desc_t));
	if (ret < (int)sizeof(usb_config_desc_t))
		return USB_EIO;
	uint16_t total = ((usb_config_desc_t*)usb_desc_buf)->wTotalLength;
	if (total > sizeof(usb_desc_buf))
		total = sizeof(usb_desc_buf);
	ret = usb_get_descriptor(dev, USB_DT_CONFIG, 0, usb_desc_buf, total);
	if (ret < (int)sizeof(usb_config_desc_t))
		return USB_EIO;
	total = (uint16_t)ret;
	uint8_t config_value = ((usb_config_desc_t*)usb_desc_buf)->bConfigurationValue;

	ret = usb_control_msg(dev, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
						  USB_REQ_SET_CONFIGURATION, config_value, 0, NULL, 0);
	if (ret < 0) {
		UARTDebugOut("[usb]: device %d: SET_CONFIGURATION failed (%d) \r\n", address, ret);
		return ret;
	}
	/* data toggles start at DATA0 after SET_CONFIGURATION */
	memset(dev->toggle_in, DWC2_PID_DATA0, sizeof(dev->toggle_in));
	memset(dev->toggle_out, DWC2_PID_DATA0, sizeof(dev->toggle_out));

	/* the descriptor buffer is reused by child devices: keep a copy */
	static uint8_t cfg_copy[USB_MAX_DEPTH + 1][sizeof(usb_desc_buf)];
	memcpy(cfg_copy[depth], usb_desc_buf, total);
	usb_configure_interfaces(dev, cfg_copy[depth], total);
	return USB_OK;
}

/**
 * @brief usb_enumerate_root -- reset the root port and enumerate the tree
 */
int usb_enumerate_root() {
	uint8_t speed;
	int ret = dwc2_root_port_reset(&speed);
	if (ret != USB_OK)
		return ret;
	usb_root_speed = speed;
	UARTDebugOut("[usb]: root port enabled, %s speed device \r\n", usb_speed_name(speed));
	return usb_new_device(NULL, 1, speed);
}
