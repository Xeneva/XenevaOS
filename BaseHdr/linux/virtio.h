#ifndef __LINUX_VIRTIO_H__
#define __LINUX_VIRTIO_H__

#include <stdint.h>
#include <stddef.h>
#include <linux/kernel.h>
/*
 * struct scatterlist used to be declared here, five lines below, and now
 * lives in <linux/scatterlist.h> -- mainline's home for it, so a ported
 * source includes the header mainline tells it to. Including it means every
 * file that reaches virtio.h still sees the type (nothing here has to
 * change), and there is still exactly one definition of it.
 */
#include <linux/scatterlist.h>
/*
 * memcpy, for virtio_cread() below: a config-space read is a copy of the
 * caller's own width out of device memory, and this header's macro is where
 * that copy is spelled. Included here rather than left to the expansion site
 * so a caller need not have remembered it -- a macro expands in its own
 * context, and `virtio_cread()` reaching an undeclared memcpy() would be an
 * error attributed to the driver that never named memcpy.
 */
#include <linux/string.h>
/*
 * The next three are what mainline's <linux/virtio.h> pulls in through
 * <linux/device.h>, which DCL does not have:
 *
 *     mainline   virtio.h -> device.h -> kobject.h -> sysfs.h -> kernfs.h
 *                     \-> pm.h           \-> completion.h        \-> idr.h
 *
 * The chain was walked against the header package rather than assumed, and
 * it is why a ported driver can write `struct completion have_data`,
 * `pm_sleep_ptr(...)`, `DEFINE_IDA(...)` and `ida_alloc(...)` while including
 * only <linux/virtio.h> the way it does upstream. Flattening five headers
 * into three is the whole content of DCL's missing device layer.
 */
#include <linux/completion.h>
#include <linux/pm.h>
#include <linux/idr.h>
/*
 * module.h is needed for module_virtio_driver(), defined at the bottom of
 * this file: the macro expands to a module_init() call, and module.h is what
 * owns module_init() (it maps it to DCL_INITCALL via <linux/init.h>). No
 * cycle -- module.h reaches init.h and export.h, neither of which comes back
 * here, and kernel.h already includes module.h before it reaches this file.
 */
#include <linux/module.h>
#include <linux/fs.h>		/* struct device -- virtio_device carries one (mainline
				 * does too, as `struct device dev;`), and fs.h is where
				 * DCL's is defined. fs.h includes only <stdint.h> and
				 * <stddef.h>, so this cannot cycle. */
#include <linux/device/driver.h>	/* struct device_driver -- shared with
					 * pci.h since the scanout stage; see the
					 * note where the struct used to live,
					 * below virtio_device. */
#include <linux/list.h>	/* struct list_head -- virtio_device.vqs, virtqueue.list */
#include <Drivers/virtio.h>

/* ─── Feature bits ─── */
#define VIRTIO_F_NOTIFY_ON_EMPTY	24
#define VIRTIO_RING_F_INDIRECT_DESC 28
#define VIRTIO_RING_F_EVENT_IDX		29

/*
 * VIRTIO_DEV_ANY_ID -- the id_table wildcard.
 *
 * mainline: `#define VIRTIO_DEV_ANY_ID ((unsigned int)-1)`. It has to come
 * out as 0xFFFFFFFF, not 0 and not -1 signed, because DCL/virtio_shim.c
 * matches on it directly:
 *
 *     if (id->device == virtio_dev && id->vendor == 0xFFFFFFFF)
 *
 * (unsigned)-1 is the only spelling that satisfies both the comparison and
 * the struct field, which is __u32. A driver that matched on 0 instead would
 * only ever bind to a device reporting vendor 0, which none do -- so the
 * failure would be "probe never called", with nothing to say why.
 */
#define VIRTIO_DEV_ANY_ID	((unsigned int)-1)

/*
 * ─── Device status ───
 *
 * Not defined here: <uapi/linux/virtio_config.h> is the single source for
 * the VIRTIO_CONFIG_S_* byte, and DCL's <linux/virtio_config.h> already
 * pulls it in. This header used to carry its own copy of five of them, and
 * the two copies were spelled differently -- `128` here against `0x80`
 * there -- which is a -Wmacro-redefined warning the moment a TU includes
 * both (virtio_console.c does, via virtio_console.h). Identical values do
 * not warn and differing ones do, so "make them match" was a fix that
 * stopped working the first time someone edited either side; taking the
 * duplicate out is the fix that cannot stop working.
 *
 * Everything else this header defines -- VRING_DESC_F_*, VIRTIO_RING_F_*,
 * VIRTIO_DEV_ANY_ID -- is not in the uapi header at all.
 */
#include <uapi/linux/virtio_config.h>

/* ─── Descriptor flags ─── */
#define VRING_DESC_F_NEXT	  1
#define VRING_DESC_F_WRITE	  2
#define VRING_DESC_F_INDIRECT 4

/* ─── Forward declarations ─── */
struct virtio_device;
struct virtio_driver;
struct virtqueue;

struct virtqueue_info {
	const char* name;
	void (*callback)(struct virtqueue* vq);
	bool ctx;
};

/* The .ko indexes these by fixed offset */
struct virtio_config_ops {
	void (*get)(struct virtio_device* vdev, unsigned offset, void* buf, unsigned len);
	void (*set)(struct virtio_device* vdev, unsigned offset, const void* buf, unsigned len);
	u32 (*generation)(struct virtio_device* vdev);
	u8 (*get_status)(struct virtio_device* vdev);
	void (*set_status)(struct virtio_device* vdev, u8 status);
	void (*reset)(struct virtio_device* vdev);
	int (*find_vqs)(struct virtio_device* vdev,
					unsigned nvqs,
					struct virtqueue** vqs,
					struct virtqueue_info* vqs_info,
					void* desc);
	void (*del_vqs)(struct virtio_device* vdev);
	void (*synchronize_cbs)(struct virtio_device* vdev);
	u64 (*get_features)(struct virtio_device* vdev);
	void (*get_extended_features)(struct virtio_device* vdev, u64* features);
	int (*finalize_features)(struct virtio_device* vdev);
	const char* (*bus_name)(struct virtio_device* vdev);
	int (*set_vq_affinity)(struct virtqueue* vq, const void* cpu_mask);
	const void* (*get_vq_affinity)(struct virtio_device* vdev, int index);
	bool (*get_shm_region)(struct virtio_device* vdev, void* region, u8 id);
	int (*disable_vq_and_reset)(struct virtqueue* vq);
	int (*enable_vq_after_reset)(struct virtqueue* vq);

	void* device_config_base;
};

/*
 * struct virtio_device_id -- defined *before* struct virtio_device below,
 * which carries one by value. It used to sit underneath; moving it up is the
 * only way a value member can be spelled, and nothing that references the type
 * by name cared where it was.
 *
 * mainline's is the same two __u32 words. The driver reads `vdev->id.device`
 * (virtio_console.c:332) to tell an rproc serial device from a virtio-console
 * one.
 */
struct virtio_device_id {
	__u32 device;
	__u32 vendor;
};

struct virtio_device {
	int index;
	void* drv_data;
	unsigned char _linux_prefix[0x328];
	struct virtio_config_ops* config;
	unsigned char _mid[0x30];
	void* priv;
	struct VirtioPCIDevice* au_dev;

	/*
	 * Appended, and appended is the point: everything above is the layout
	 * the prebuilt modules were assembled against, `_linux_prefix` included,
	 * so no existing offset moves and the static asserts elsewhere still
	 * describe the same bytes. mainline carries `id` and `dev` inside the
	 * region DCL covers with `_linux_prefix`, but a blob cannot answer a
	 * *member* reference -- `vdev->dev.parent` (virtio_console.c:433) and
	 * `vdev->id.device` (:332) are field accesses the preprocessor never
	 * sees -- so the three members below exist for them, and they are the
	 * only three a ported source names today.
	 *
	 *   id     mainline's device identity. Zeroed by virtio_device_register()
	 *          and set to the device type there; safe to read as zero even
	 *          when it is not, because the only reader
	 *          (is_rproc_serial(), :332) sits behind `is_rproc_enabled &&
	 *          ...` and is_rproc_enabled is IS_ENABLED(CONFIG_REMOTEPROC)
	 *          with CONFIG_REMOTEPROC 0 -- a constant fold, so the load
	 *          never happens at runtime.
	 *
	 *   dev    mainline's embedded device. Never registered with anything
	 *          (DCL has no device model to register it with); its
	 *          `parent` is set by virtio_device_register() to the PCI
	 *          fabric's device for the function behind it -- the relation
	 *          virtio-gpu's probe reads (drm_dev_alloc()'s parent
	 *          argument, then its dev_is_pci() quirk gate). The DMA path
	 *          at virtio_console.c:433 reads `parent` only under
	 *          is_rproc_serial(), whose first term is constant-false on
	 *          this port: it never runs, and no NULL here could change
	 *          that.
	 *
	 *   vqs    the head of this device's virtqueue list, so mainline's
	 *          `virtio_device_for_each_vq()` (used once, in remove_vqs())
	 *          can walk real queues instead of a list that does not exist.
	 *          Initialised by virtio_device_register() and populated by
	 *          shim_find_vqs(). See the note on virtqueue.list for why the
	 *          snapshot copy in _virtio_devices[] needs its own head.
	 */
	struct virtio_device_id id;
	struct device dev;
	struct list_head vqs;
};

/*
 * struct device_driver now lives at upstream's path, <linux/device/driver.h>
 * (included above), because pci_driver writes `.driver.pm` too and two
 * definitions of the tag would collide in any translation unit that includes
 * both buses -- the vendored virtio-gpu driver includes <linux/pci.h>
 * alongside this header. Everything this comment used to promise moved with
 * the struct: `name` at offset 0, 152 bytes exactly, `_rest` covering
 * mainline's unmodelled bind machinery, and virtio_driver's field offsets
 * (id_table at 152, probe at 200) untouched. One thing changed while moving:
 * the 144-byte tail is 136 now, and the eight bytes that went missing are
 * `pm` -- the one field bochs.c's pci_driver initialises through the same
 * tag. Nothing read the padding, so no existing caller sees a difference.
 */

struct virtio_driver {
	/*
	 * mainline's first member is `struct device_driver driver;`, so this
	 * is the same shape. See the note above for why it is nested now.
	 */
	struct device_driver driver;
	const struct virtio_device_id* id_table;
	/*
	 * mainline's five members between id_table and probe, spelled out
	 * instead of `unsigned char _gap[40]`.
	 *
	 * The gap was never arbitrary: mainline's layout there is
	 *
	 *     const unsigned int *feature_table;        8   160
	 *     unsigned int feature_table_size;           4   168
	 *     const unsigned int *feature_table_legacy;  8   176
	 *     unsigned int feature_table_size_legacy;    4   184
	 *     int (*validate)(struct virtio_device *);   8   192
	 *
	 * which is 8 + 4 + pad4 + 8 + 4 + pad4 + 8 = 40 bytes, and probe lands
	 * at 200 -- exactly where the gap put it. So this replaces padding with
	 * the fields the padding was standing in for and moves nothing: the
	 * offsets after it are identical, which is the condition the note above
	 * says has to hold.
	 *
	 * virtio_console.c:2171/:2185 initialise feature_table and
	 * feature_table_size by name ("field designator '_gap' does not refer to
	 * any field" is the error a designator cannot make against padding), and
	 * feature_table_legacy/validate are here for the same reason -- leaving
	 * two of the five as padding would mean the next mainline driver that
	 * names them hit a wall DCL had already decided how to take down.
	 *
	 * Nothing in DCL *reads* feature_table: register_virtio_driver() only
	 * ever touches ->probe, ->scan and ->id_table, and feature negotiation
	 * does not happen here (see __virtio_test_bit() in <linux/virtio_config.h>).
	 * The members exist so the initialiser compiles and the layout matches.
	 */
	const unsigned int* feature_table;
	unsigned int feature_table_size;
	const unsigned int* feature_table_legacy;
	unsigned int feature_table_size_legacy;
	int (*validate)(struct virtio_device* vdev);
	int (*probe)(struct virtio_device* vdev);
	void (*scan)(struct virtio_device* vdev);
	void (*remove)(struct virtio_device* vdev);
	void (*config_changed)(struct virtio_device* vdev);
	/*
	 * DCL delta: mainline's .shutdown slot, added with the virtio-gpu port
	 * -- virtgpu_drv.c:178 initialises it by designator. Appended here,
	 * after the pinned prefix the note above describes, so the offsets the
	 * prebuilt module indexes do not move; no DCL path drives a device
	 * shutdown, so nothing reads it.
	 */
	void (*shutdown)(struct virtio_device* vdev);
	int (*freeze)(struct virtio_device* vdev);
	int (*restore)(struct virtio_device* vdev);
};

struct virtqueue {
	unsigned char _linux_prefix[32];
	struct virtio_device* vdev;
	struct VirtqDesc* desc;
	struct VirtqAvailHdr* avail;
	struct VirtqUsedHdr* used;
	uint16_t free_head;
	uint16_t num_free;
	uint16_t index;
	uint16_t qsize;
	const char* name;
	void (*callback)(struct virtqueue* vq);
	void* cookies[256];
	uint16_t event_idx_write;
	/*
	 * Whether the device behind this queue has gone away -- mainline's
	 * own member, and the whole of virtqueue_is_broken()'s answer. It is
	 * set by virtio_break_device(), which is what virtcons_remove() calls
	 * first (virtio_console.c:1920) so the flush loops below it stop
	 * waiting on a device that has already left; before that call it is
	 * false, and the `while (!virtqueue_get_buf() && !is_broken()) cpu_relax()`
	 * spins exit on the first condition as they are meant to.
	 */
	bool broken;
	/*
	 * This queue's node in its device's virtqueue list -- what
	 * virtio_device_for_each_vq() walks in virtio_console.c's remove_vqs().
	 * Appended last for the reason every append in this header is: the
	 * `_linux_prefix` up front is the region a prebuilt module indexes by
	 * fixed offset, and nothing before it may move.
	 *
	 * kmalloc(sizeof(*vq)) in shim_find_vqs() sizes the object from this
	 * very struct, so the extra 16 bytes are allocated with the rest and
	 * there is no fixed-size container anywhere to overrun. list_add() sets
	 * both links, so the memset() beside it need not.
	 */
	struct list_head list;
};

/*
 * is_virtio_device(dev) -- mainline asks `dev->bus == &virtio_bus` (v7.2
 * virtio.h). DCL names no bus object to compare against -- struct device
 * is fs.h's pinned ABI layout, and the bus field mainline reads is not a
 * member the pin preserves -- and there is one device family in the tree:
 * every struct device* a vendored driver holds here came out of this
 * stack. Added with the virtio-gpu port, whose vram path guards its UUID
 * work with it; any non-NULL device is one of ours.
 */
static inline bool is_virtio_device(struct device* dev)
{
	return dev != NULL;
}

int register_virtio_driver(struct virtio_driver* driver);
void unregister_virtio_driver(struct virtio_driver* driver);

/*
 * module_virtio_driver(drv) -- register a static struct virtio_driver at
 * boot. This is how virtio-rng.c ends: `module_virtio_driver(virtio_rng_driver);`
 *
 * mainline expands it through module_driver() into an init function *and* a
 * matching exit function. DCL has the init half only, and the reason the exit
 * half is dropped rather than stubbed is mechanical: <linux/module.h>'s
 * module_exit() is an empty macro here (DCL has no unload path), so a
 * `static void __exit drv##_exit(void)` would be a function nothing ever
 * references -- -Wunused-function on every build of every driver that uses
 * this macro. An unload path that cannot run is worse than no unload path.
 *
 * The init half is real, though, and it is worth being precise about *how*:
 * module.h maps module_init() to DCL_INITCALL(), which stores the function in
 * the named global __dcl_initcall_<fn>. Nothing discovers or schedules it --
 * DclRunInitcalls() (DCL/linux_mm_shim.c) calls that global by name. So
 * bringing up a driver through this macro has two halves: it compiles here,
 * and its line goes into DclRunInitcalls() there. The second half is not
 * automatic, and forgetting it produces exactly the symptom a prebuilt .ko
 * used to produce: everything builds, and probe is never called.
 *
 * Returns register_virtio_driver()'s int, which is what DclRunInitcalls()
 * expects from an initcall.
 */
#define module_virtio_driver(drv)					\
	static int __init drv##_init(void)				\
	{								\
		return register_virtio_driver(&(drv));			\
	}								\
	module_init(drv##_init)

void virtio_poll_vqs(void);
void virtio_poll_input_vqs(void);

/*
 * virtio_find_single_vq / virtio_device_ready / virtio_reset_device.
 *
 * mainline defines the first two as static inline here and the third in
 * <linux/virtio.h> too; DCL declares them and puts the bodies in
 * DCL/virtio_shim.c and DCL/linux_kmod_shim.c instead.
 *
 * The reason is include order, not taste: all three are one line each, but
 * find_single_vq has to report a failed find_vqs as an error pointer, and
 * <linux/err.h> reaches back into <linux/kernel.h> -- which includes *this*
 * file. A definition here would be expanded against a half-processed
 * kernel.h whenever something includes kernel.h first, and kernel.h always
 * comes first. In a .c the same header is fully read, and a driver cannot
 * tell the difference: virtio-rng.c writes `if (IS_ERR(vi->vq))` and gets
 * an extern with the same signature either way.
 *
 * virtio_reset_device's body already existed (linux_kmod_shim.c:19) with no
 * declaration anywhere -- it worked only because the prebuilt .ko carried its
 * own inline copy of it.
 */
struct virtqueue* virtio_find_single_vq(struct virtio_device* vdev,
					void (*cb)(struct virtqueue* vq),
					const char* name);
void virtio_device_ready(struct virtio_device* vdev);
void virtio_reset_device(struct virtio_device* dev);

void virtio_rng_detect(void);
int virtio_input_detect(void);

/*
 * virtio_cread(vdev, type, field, ptr) -- copy `sizeof(*ptr)` bytes of config
 * space, starting at offsetof(type, field), into *ptr.
 *
 * The previous version of this macro cast the offset address to `type *` and
 * dereferenced it:
 *
 *     *(ptr) = *((type *)(base + offsetof(type, field)));
 *
 * which yields a `type` -- the whole struct the field belongs to -- and
 * assigns it to *ptr. virtio_console.c:1794/:1795 are exactly that shape
 * gone wrong: `virtio_cread(vdev, struct virtio_console_config, cols, &cols)`
 * with cols a u16, so the assignment was u16 = struct virtio_console_config
 * and clang said so three times. Taking the caller's width instead of the
 * container's is what mainline's `sizeof(virtio_cread_v)` does
 * (linux/virtio_config.h:466), so this is the same read with mainline's
 * arithmetic and DCL's copy -- the difference being that DCL has no
 * might_sleep() to make, because nothing here can block.
 *
 * The `char *` cast matters: device_config_base is `void *`, and while clang
 * permits void-pointer arithmetic as an extension it does so one element at a
 * time, which for a one-byte element happens to be correct -- by accident.
 */
#define virtio_cread(vdev, type, field, ptr)				\
	do {								\
		memcpy((ptr),						\
		       (char*)(vdev)->config->device_config_base +	\
			       offsetof(type, field),			\
		       sizeof(*(ptr)));					\
	} while (0)

/*
 * virtio_cread_bytes() -- mainline's arbitrary-length config read.  The
 * virtio-input driver uses it for the device name/serial and for the raw
 * capability bitmaps, which are byte streams rather than fields of a known
 * type.
 *
 * The window is Device memory. memcpy() of 32 bytes or more compiles to
 * `ldp q0, q1` (one 32-byte SIMD load). The bitmap/string union starts at
 * offset 8, so that address is only 8-byte aligned, and Device memory
 * raises an alignment fault on the load. QEMU's virtio-tablet key bitmap
 * is 43 bytes, which is why the keyboard probe (every copy shorter than
 * 32 bytes, so plain 8-byte loads) finished and the tablet probe died in
 * virtinput_cfg_bits. One volatile byte load per byte is the access width
 * this window accepts, and it cannot be widened back into that pair.
 *
 * The base is this device's own BAR. device_config_base lives on the one
 * shared virtio_config_ops, and virtio_device_register() overwrites it for
 * every new function; reading it here would return the last device's window.
 */
static inline void virtio_cread_bytes(struct virtio_device* vdev,
				      unsigned int offset, void* buf,
				      size_t len)
{
	void* base = (vdev->au_dev && vdev->au_dev->deviceCfg)
			     ? vdev->au_dev->deviceCfg
			     : vdev->config->device_config_base;
	volatile const uint8_t* src = (volatile const uint8_t*)base + offset;
	uint8_t* dst = buf;

	for (size_t i = 0; i < len; i++)
		dst[i] = src[i];
}

/*
 * struct scatterlist moved to <linux/scatterlist.h>; see the note there for
 * why DCL's is { buf, len } rather than mainline's page_link/offset/length.
 */

int virtio_find_vqs(struct virtio_device* vdev,
					unsigned nvqs,
					struct virtqueue** vqs,
					struct virtqueue_info* vqs_info,
					void* desc);
void virtio_del_vqs(struct virtio_device* vdev);

/*
 * mainline's fifth argument is gfp_t, and both callers pass one
 * (virtio-rng.c:65 GFP_KERNEL, virtio_console.c:490 GFP_ATOMIC). It is
 * accepted and dropped for the reason slab.h drops its own flags: Xeneva's
 * allocator has no GFP_KERNEL against GFP_ATOMIC to distinguish. The `sg` +
 * `num` pair bounds the walk DCL's way: mainline wraps this chain head into
 * a one-element sgs[] and the chain's own end bounds the walk, while DCL's
 * scatterlist has no end marker (see <linux/scatterlist.h>), so `num` is
 * the bound -- the same entries either way for the single-entry chains
 * every caller passes.
 */
int virtqueue_add_inbuf(
	struct virtqueue* vq, struct scatterlist* sg, unsigned int num_in, void* data, gfp_t gfp);

/*
 * The cache-clean spelling mainline added for DMA-coherent architectures.
 * DCL has no per-buffer cache metadata to hand the ring code, so the body
 * is the plain add; correctness for device-written buffers is handled by
 * the invalidate in virtqueue_get_buf().
 */
int virtqueue_add_inbuf_cache_clean(
	struct virtqueue* vq, struct scatterlist* sg, unsigned int num_in, void* data, gfp_t gfp);

unsigned int virtqueue_get_vring_size(const struct virtqueue* vq);

/*
 * virtqueue_add_sgs() -- mainline's multi-chain submit: `sgs[]` is an array
 * of scatterlist heads, the first `out_cnt` readable by the device and the
 * next `in_cnt` writable. The vendored virtio-gpu command path passes the
 * array whole (virtgpu_vq.c builds `struct scatterlist *sgs[3]` at :361,
 * :415 and :566), and an earlier spelling of this argument as one flat
 * `struct scatterlist sg[]` is what made the shape wrong: the walk in
 * virtio_shim.c read the pointer array as though it were buffers, QEMU
 * rejected the control queue's first command with "zero sized buffers are
 * not allowed", and the device went silent before the display-info wait at
 * the end of virtio_gpu_init() could be answered.
 *
 * DCL's scatterlist has no chain terminator, so a chain is exactly one
 * entry here: every producer in this tree builds single-entry chains (the
 * vcmd/vresp/vout slots at virtgpu_vq.c:437-495, the cursor submit's
 * :560-566), and the one longer-chain producer, the vmalloc_to_sgt()
 * branch at :474, sits behind is_vmalloc_addr(), constant false here --
 * arming that branch means teaching the walk to continue through the chain
 * it produces, and nothing else reads differently.
 *
 * The last argument is mainline's gfp_t; the old `void *ctx` slot took the
 * callers' GFP_ATOMIC as an integer-into-pointer constraint violation, and
 * nothing reads the value either way, same trade as add_inbuf() above.
 */
int virtqueue_add_sgs(struct virtqueue* vq,
					  struct scatterlist* sgs[],
					  unsigned int out_cnt,
					  unsigned int in_cnt,
					  void* data,
					  gfp_t gfp);

bool virtqueue_kick(struct virtqueue* vq);
bool virtqueue_kick_prepare(struct virtqueue* vq);
void virtqueue_notify(struct virtqueue* vq);
void* virtqueue_get_buf(struct virtqueue* vq, unsigned int* len);

/*
 * The five virtqueue/virtio entry points virtio_console.c calls that had no
 * declaration anywhere in DCL. Bodies are in DCL/virtio_shim.c beside the
 * ones above; the signatures are mainline's, character for character, because
 * a mismatch here is invisible to the linker (C does not mangle) and would
 * show up as a call that links and reads the wrong register.
 *
 *   add_outbuf        the transmit half of add_inbuf -- a *device*-bound
 *                     buffer, num_out of them, and mainline's trailing gfp_t
 *                     (not the `void *ctx` DCL's add_inbuf carries). The
 *                     distinction matters at the two call sites, which pass
 *                     GFP_ATOMIC == 1: an int 1 into a `void *` parameter is
 *                     a constraint violation, and it would have been reported
 *                     as an implicit-conversion error at every call rather
 *                     than at the declaration.
 *   disable_cb/enable_cb  mask and unmask used-buffer notifications. Neither
 *                     has a call site in this driver *today* -- the four
 *                     virtqueue_disable_cb() calls all sit inside
 *                     virtcons_freeze(), which #ifdef CONFIG_PM_SLEEP removes
 *                     (and DCL sets no such config) -- which is why they are
 *                     declared and not defined: a body nothing can reach is
 *                     one more thing to get wrong for no caller, and the
 *                     next driver that uncomments a suspend path will find
 *                     the declaration where mainline puts it rather than an
 *                     implicit-int error.
 *   detach_unused_buf returns a buffer still owned by the queue so it can be
 *                     freed; NULL when there is none, which is the whole of
 *                     remove_vqs()'s inner loop condition.
 *   is_broken         a queue whose device went away. DCL's is always false:
 *                     there is no removal path to break a queue, and a
 *                     `while (!virtqueue_get_buf() && !is_broken()) cpu_relax()`
 *                     spin that could never exit on the second condition is
 *                     exactly the loop it should be -- it exits on the first.
 */
int virtqueue_add_outbuf(struct virtqueue* vq, struct scatterlist sg[],
			 unsigned int num, void* data, gfp_t gfp);
void virtqueue_disable_cb(struct virtqueue* vq);
bool virtqueue_enable_cb(struct virtqueue* vq);
void* virtqueue_detach_unused_buf(struct virtqueue* vq);
bool virtqueue_is_broken(const struct virtqueue* vq);

/*
 * virtio_break_device() -- mark the device's queues broken so callers stop
 * waiting on them. mainline: drivers/virtio/virtio.c, and it is the one
 * virtio entry point with no DCL implementation behind it until now.
 *
 * DCL's body marks the device's live queues, which is what the one caller
 * (virtio_console.c:1920, the probe-failure unwind) wants: after it runs,
 * every `while (... && !virtqueue_is_broken(vq))` in the driver stops
 * spinning. Declared here rather than in <linux/virtio_config.h> because
 * mainline declares it in <linux/virtio.h>, which is this file.
 */
void virtio_break_device(struct virtio_device* dev);

void virtio_device_register(struct virtio_device* vdev,
							struct VirtioPCIDevice* au_dev,
							int dev_index);

#endif
