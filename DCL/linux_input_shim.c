/**
 * DCL/linux_input_shim.c -- the bridge between the vendored Linux input core
 * and Xeneva's /dev/kybrd + /dev/mice event stream.
 *
 * The vendored core (Vendored/drivers/input/input.c and its helpers) is
 * mainline's event state machine: it filters duplicates, tracks key state,
 * handles SYN_REPORT and multi-touch slots, and fans events out to handlers.
 * This file supplies the two pieces mainline keeps outside that core:
 *
 *   - the handful of driver-core symbols the core links against but DCL does
 *     not otherwise need (uevents, sysfs paths, poller bookkeeping), and
 *   - one input_handler that turns the core's EV_KEY/EV_ABS/EV_SYN stream
 *     into the scancode and mouse packets Xeneva's desktop already reads.
 *
 * The handler is registered before virtio_input probes, so every input
 * device is attached at input_register_device() time.  Event delivery is
 * driven by virtio_poll_input_vqs() from the timer tick: DCL's virtio queues
 * are polled, never interrupt-driven, and the input device's event queue is
 * no exception.
 *
 * Only the __XENEVA_BYPASS_VIRTIO_INPUT__ image registers the vendored
 * driver (DCL/linux_mm_shim.c and DCL/modload_test.c).  The objects still
 * compile in every image so the gate can build one kernel with either input
 * path selected at compile time rather than maintaining two images.
 */

#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/virtio.h>
#include <linux/errno.h>
#include <Drivers/uart.h>
#include <Fs/Dev/devinput.h>
#include <aucon.h>
#include <string.h>

/*
 * Every input device matches: an all-zero evbit bitmap is a subset of every
 * device's evbit.  The flags word has to be non-zero or input_match_device()
 * treats the entry as the table terminator.
 */
static const struct input_device_id dcl_input_ids[] = {
	{ .flags = INPUT_DEVICE_ID_MATCH_EVBIT },
	{ },
};

enum dcl_input_kind {
	DCL_INPUT_KEYBOARD = 1,
	DCL_INPUT_TABLET,
};

struct dcl_input_state {
	enum dcl_input_kind kind;
	/* Tablet state, accumulated until SYN_REPORT. */
	int x, y;
	int left, right, middle;
	int scroll_up, scroll_down;
	int wheel;
	int seen;
};

/* The mapping native virtiokbd.c uses for the extended key range.  Keeping
 * it byte-for-byte identical means the desktop sees the same scancodes from
 * either input path. */
static const uint8_t dcl_ext_key_map[256] = {
	[0x60] = 0x1c, /* keypad enter */
	[0x62] = 0x35, /* keypad slash */
	[0x63] = 0x37, /* print screen */
	[0x66] = 0x47, /* home */
	[0x67] = 0x48, /* up */
	[0x68] = 0x49, /* page up */
	[0x6c] = 0x50, /* down */
	[0x69] = 0x4B, /* left */
	[0x6a] = 0x4D, /* right */
	[0x6b] = 0x4F, /* end */
	[0x6d] = 0x51, /* page down */
	[0x6e] = 0x52, /* insert */
	[0x6f] = 0x53, /* delete */
	[0x7d] = 0x5b, /* left super */
};

static uint32_t dcl_input_key_logs;
static uint32_t dcl_input_ptr_logs;

static void dcl_input_emit_key(unsigned int code, int value)
{
	AuInputMessage msg;
	uint32_t scancode;

	memset(&msg, 0, sizeof(msg));
	msg.type = AU_INPUT_KEYBOARD;

	if (code == KEY_ENTER || code == KEY_KPENTER) {
		scancode = 0x1c;
	} else if (code < 0x59) {
		scancode = (uint8_t)code;
	} else if (code < 256 && dcl_ext_key_map[code]) {
		scancode = (0xE0u << 8) | dcl_ext_key_map[code];
	} else {
		return;
	}
	if (value == 0)
		scancode |= 0x80;

	msg.code = scancode;
	AuDevWriteKybrd(&msg);

	if (dcl_input_key_logs < 8) {
		dcl_input_key_logs++;
		UARTDebugOut("[virtio-input]: key code %d value %d\r\n", code,
					 value);
	}
}

static void dcl_input_emit_pointer(struct dcl_input_state* st)
{
	AuInputMessage msg;
	/* The native tablet driver's AuInputMessage button bits. */
	unsigned char buttons =
		(st->left ? 0x01 : 0) |
		(st->right ? 0x02 : 0) |
		(st->middle ? 0x04 : 0) |
		(st->scroll_up ? 0x10 : 0) |
		(st->scroll_down ? 0x20 : 0);

	memset(&msg, 0, sizeof(msg));
	msg.type = AU_INPUT_MOUSE;
	msg.xpos = st->x;
	msg.ypos = st->y;
	msg.button_state = buttons;

	AuDevWriteMice(&msg);

	if (dcl_input_ptr_logs < 8) {
		dcl_input_ptr_logs++;
		UARTDebugOut("[virtio-input]: pointer %d,%d buttons %x\r\n",
					 msg.xpos, msg.ypos, msg.button_state);
	}

	st->scroll_up = st->scroll_down = 0;
	st->wheel = 0;
}

/*
 * Map an absolute axis value into the live desktop, clamping to its extent.
 * A zero screen dimension falls back to the console's boot size; a zero
 * maximum means the device did not report one, so use the value as-is.
 */
static int dcl_input_abs_to_screen(int value, int max, uint32_t screen,
				   int fallback)
{
	int extent = screen ? (int)screen : fallback;
	int scaled = max > 0 ? (value * extent) / max : value;

	if (scaled < 0)
		return 0;
	if (scaled >= extent)
		return extent - 1;
	return scaled;
}

static void dcl_input_event(struct input_handle* handle, unsigned int type,
							unsigned int code, int value)
{
	struct dcl_input_state* st =
		(struct dcl_input_state*)handle->private;

	if (!st)
		return;

	if (st->kind == DCL_INPUT_KEYBOARD) {
		if (type == EV_KEY && code != KEY_RESERVED)
			dcl_input_emit_key(code, value);
		return;
	}

	switch (type) {
	case EV_ABS:
		if (code == ABS_X)
			st->x = dcl_input_abs_to_screen(
				value, input_abs_get_max(handle->dev, ABS_X),
				AuConsoleGetScreenWidth(), 640);
		else if (code == ABS_Y)
			st->y = dcl_input_abs_to_screen(
				value, input_abs_get_max(handle->dev, ABS_Y),
				AuConsoleGetScreenHeight(), 480);
		st->seen = 1;
		break;
	case EV_REL:
		if (code == REL_X)
			st->x += value;
		else if (code == REL_Y)
			st->y += value;
		else if (code == REL_WHEEL)
			st->wheel += value;
		st->seen = 1;
		break;
	case EV_KEY:
		if (code == BTN_LEFT)
			st->left = value;
		else if (code == BTN_RIGHT)
			st->right = value;
		else if (code == BTN_MIDDLE)
			st->middle = value;
		else if (code == BTN_GEAR_DOWN)
			st->scroll_down = value != 0;
		else if (code == BTN_GEAR_UP)
			st->scroll_up = value != 0;
		st->seen = 1;
		break;
	case EV_SYN:
		if (code == SYN_REPORT && st->seen) {
			if (st->wheel > 0)
				st->scroll_up = 1;
			else if (st->wheel < 0)
				st->scroll_down = 1;
			dcl_input_emit_pointer(st);
			st->seen = 0;
		}
		break;
	default:
		break;
	}
}

static int dcl_input_connect(struct input_handler* handler,
							 struct input_dev* dev,
							 const struct input_device_id* id)
{
	struct input_handle* handle;
	struct dcl_input_state* st;
	bool is_tablet;
	int err;

	(void)id;

	is_tablet = test_bit(EV_ABS, dev->evbit) &&
				test_bit(ABS_X, dev->absbit) &&
				test_bit(ABS_Y, dev->absbit);

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;
	st->kind = is_tablet ? DCL_INPUT_TABLET : DCL_INPUT_KEYBOARD;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle) {
		kfree(st);
		return -ENOMEM;
	}
	handle->dev = dev;
	handle->handler = handler;
	handle->name = "xeneva-input";
	handle->private = st;

	err = input_register_handle(handle);
	if (err) {
		kfree(handle);
		kfree(st);
		return err;
	}

	err = input_open_device(handle);
	if (err) {
		input_unregister_handle(handle);
		kfree(handle);
		kfree(st);
		return err;
	}

	UARTDebugOut("[virtio-input]: attached %s (%s)\r\n",
				 is_tablet ? "tablet" : "keyboard",
				 dev->name ? dev->name : "unnamed");
	return 0;
}

static void dcl_input_disconnect(struct input_handle* handle)
{
	struct dcl_input_state* st;

	if (!handle)
		return;
	st = (struct dcl_input_state*)handle->private;
	input_unregister_handle(handle);
	kfree(st);
	kfree(handle);
}

static struct input_handler dcl_input_handler = {
	.event = dcl_input_event,
	.connect = dcl_input_connect,
	.disconnect = dcl_input_disconnect,
	.name = "xeneva-input",
	.id_table = dcl_input_ids,
};

void dcl_input_bridge_init(void)
{
	int err = input_register_handler(&dcl_input_handler);

	if (err)
		UARTDebugOut("[virtio-input]: handler registration failed: %d\r\n",
					 err);
	else
		UARTDebugOut("[virtio-input]: bridge handler registered\r\n");
}

/* ─── driver-core / input-core symbols DCL does not model ─────────────── */

void add_input_randomness(unsigned int type, unsigned int code, int value)
{
	(void)type;
	(void)code;
	(void)value;
	/* Xeneva's entropy comes from the hardware RNG; input timing is not a
	 * second source on this build. */
}

void dump_stack(void)
{
}

void __module_get(void* module)
{
	(void)module;
}

int add_uevent_var(struct kobj_uevent_env* env, const char* fmt, ...)
{
	(void)env;
	(void)fmt;
	return 0;
}

int register_chrdev_region(dev_t from, unsigned int count, const char* name)
{
	(void)from;
	(void)count;
	(void)name;
	/* input_init() is compiled but not run: input.c's lists are statically
	 * initialised and the DCL bridge emits to devfs directly. */
	return 0;
}

void unregister_chrdev_region(dev_t from, unsigned int count)
{
	(void)from;
	(void)count;
}

/*
 * input.c calls the poller hooks from input_open_device(), input_inhibit()
 * and input_free_device().  No device in this tree asks to be polled --
 * virtio-input is event driven -- and Vendored/drivers/input/input-poller.c
 * is deliberately not vendored because it depends on workqueue semantics
 * DCL has not implemented.  Empty hooks preserve the call sites without
 * pretending a poller runs.
 */
void input_dev_poller_finalize(struct input_dev_poller* poller)
{
	(void)poller;
}

void input_dev_poller_start(struct input_dev_poller* poller)
{
	(void)poller;
}

void input_dev_poller_stop(struct input_dev_poller* poller)
{
	(void)poller;
}

struct attribute_group input_poller_attribute_group = {
	.name = "poll",
};
