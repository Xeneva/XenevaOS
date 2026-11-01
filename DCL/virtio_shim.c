#include <linux/virtio.h>
#include <linux/virtio_ids.h>	/* VIRTIO_ID_GPU: the console hand-off gate */
#include <linux/pci.h>	/* vdev->dev.parent: the fabric's device for the function */
#include <linux/err.h>	/* ERR_PTR -- virtio_find_single_vq reports find_vqs failures */
#include <Drivers/virtio.h>
#include <pcie.h>
#include <Mm/kmalloc.h>
#include <Mm/vmmngr.h>
#include <string.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>

#define MAX_VIRTIO_DRIVERS 16
#define MAX_VIRTIO_DEVICES 16

static struct virtio_driver* _registered_drivers[MAX_VIRTIO_DRIVERS];
static int _num_drivers = 0;

static struct virtio_device _virtio_devices[MAX_VIRTIO_DEVICES];
static int _num_devices = 0;
/* The virtio_device probe actually runs on. Queues point here, not at
 * the snapshot copy stored in _virtio_devices[]. */
static struct virtio_device* _probed_vdevs[MAX_VIRTIO_DEVICES];
static int _num_probed_vdevs = 0;

/*
 * Every queue any vendored driver sets up lands here, and the poll that
 * reaps completions walks the whole list.  The widest device QEMU attaches
 * to this machine is virtio-serial: virtio_console.c's init_vqs() asks for
 * 2 + 2 * max_nr_ports queues, and QEMU advertises 31 ports, so the
 * console alone registers 64.  At 16 the table filled up with the
 * console's first queues, and whatever probed later -- the virtio-gpu's
 * two control/cursor queues, which is how virtio_gpu_init()'s display-info
 * wait came to spin out its whole budget against a reply that had already
 * arrived -- was dropped silently.  128 holds a full virtio-serial device
 * plus the rng, the gpu, and room to spare.
 */
#define MAX_LIVE_VQS 128
static struct virtqueue* _live_vqs[MAX_LIVE_VQS];
static int _num_live_vqs = 0;

/* cookies[] is a fixed 256. A used-ring id past that walks off the object. */
#define VQ_COOKIE_MAX 256

/*
 * The timer walks this list from IRQ context. A smashed slot's vdev is
 * not one of the devices we registered: following it is a kernel data
 * abort (FAR = vdev + 0x380, the device-id load) and fixup has nothing
 * to retry. The pointer queues store is the device probe ran on. The
 * _virtio_devices[] entry is a snapshot copy taken before any queue
 * exists, so requiring vdev to land inside that array dropped every
 * live queue, the control ring was never reaped, and
 * virtio_gpu_queue_ctrl_sgs waited out its budget forever.
 */
static int vdev_is_ours(struct virtio_device* vdev) {
	int i;

	if (!vdev)
		return 0;
	for (i = 0; i < _num_probed_vdevs; i++) {
		if (vdev == _probed_vdevs[i])
			return 1;
	}
	for (i = 0; i < _num_devices; i++) {
		if (vdev == &_virtio_devices[i])
			return 1;
	}
	return 0;
}

static int vq_slot_ok(struct virtqueue* vq) {
	uint64_t p;

	p = (uint64_t)vq;
	if ((p & 7) != 0 || p < 0xFFFF000000000000ULL)
		return 0;
	return vdev_is_ours(vq->vdev);
}

static void virtio_drop_bad_vqs(void) {
	int w = 0;
	int i;
	static int logged;

	for (i = 0; i < _num_live_vqs; i++) {
		struct virtqueue* vq = _live_vqs[i];
		if (!vq_slot_ok(vq)) {
			if (!logged) {
				UARTDebugOut("[dcl-virtio]: dropping queue with a bad device pointer\r\n");
				logged = 1;
			}
			continue;
		}
		_live_vqs[w++] = vq;
	}
	_num_live_vqs = w;
}

static void virtio_poll_vq(struct virtqueue* vq) {
	if (!vq->used)
		return;
	dc_ivac((uint64_t)&vq->used->idx);
	dsb_sy_barrier();
	if (vq->used->idx == vq->event_idx_write)
		return;
	if (vq->callback)
		vq->callback(vq);
}

void virtio_poll_vqs(void) {
	int i;
	virtio_drop_bad_vqs();
	for (i = 0; i < _num_live_vqs; i++)
		virtio_poll_vq(_live_vqs[i]);
}

/*
 * virtio_poll_input_vqs() -- the timer tick's half of the input bridge.
 * Unlike virtio_poll_vqs(), which exists for probe-time waits and console
 * I/O and walks every queue, this walks only queues owned by a virtio-input
 * function.  The device id is the discriminator, so a keyboard and a tablet
 * added later both reach their own callback without the console or the GPU
 * being re-entered from interrupt context.
 */
void virtio_poll_input_vqs(void) {
	int i;
	virtio_drop_bad_vqs();
	for (i = 0; i < _num_live_vqs; i++) {
		struct virtqueue* vq = _live_vqs[i];
		if (vq->vdev->id.device != VIRTIO_ID_INPUT)
			continue;
		virtio_poll_vq(vq);
	}
}

/*
 * dcl_wait_pump() -- the hook <linux/wait.h>'s bounded poll calls on every
 * iteration; the declaration there has the why. The whole of the work is
 * virtio_poll_vqs(), so the two are adjacent here: completions on this port
 * are reaped by polling, and a probe-time wait with no console read loop
 * above it would otherwise spin out its budget against a reply that has been
 * sitting in the used ring the entire time. virtio_gpu_init()'s display-info
 * wait is the first caller that has no other pump.
 */
void dcl_wait_pump(void) {
	virtio_poll_vqs();
}

#ifdef __XENEVA_BYPASS_VIRTGPU__
/*
 * The console-side half of the bypass bring-up: DCL/virtgpu_console.c hands
 * the kernel console to the vendored virtio-gpu's scanout (as scanout 0) the
 * moment its probe succeeds, so the desktop size the loader asked for has a
 * scanout to land on. Only a bypass image reaches the call site.
 */
void dcl_virtgpu_console_attach(struct virtio_device* vdev);
#endif

/* ─── Driver registration ─── */

/*
 * The PCI device id a virtio function sits on: 0x1040 is VIRTIO_ID 0
 * (reserved), so the entropy source is 0x1044, the console 0x1043, and the
 * GPU 0x1050. virtio_device_type() strips the base to get the virtio id,
 * and virtio_device_register() adds it back to ask the PCI fabric for the
 * device behind the function.
 */
#define VIRTIO_PCI_DEV_BASE 0x1040

static uint32_t virtio_device_type(const struct VirtioPCIDevice* au_dev) {
	uint16_t did =
		au_dev->address
			? AuPCIERead(au_dev->address, PCI_DEVICE_ID, au_dev->bus, au_dev->dev, au_dev->func)
			: 0;
	return (uint32_t)did - VIRTIO_PCI_DEV_BASE;
}

static bool virtio_id_match(const struct virtio_driver* drv, uint32_t virtio_dev) {
	if (!drv->id_table)
		return false;
	for (const struct virtio_device_id* id = drv->id_table; id->device || id->vendor; id++) {
		if (id->device == virtio_dev && id->vendor == 0xFFFFFFFF)
			return true;
	}
	return false;
}

int register_virtio_driver(struct virtio_driver* driver) {
	if (_num_drivers >= MAX_VIRTIO_DRIVERS)
		return -1;
	_registered_drivers[_num_drivers++] = driver;
	for (int d = 0; d < _num_devices; d++) {
		struct virtio_device* vdev = &_virtio_devices[d];
		if (vdev->priv)
			continue; /* already probed */
		if (!virtio_id_match(driver, virtio_device_type(vdev->au_dev)))
			continue;
		vdev->priv = driver;
		if (driver->probe && driver->probe(vdev) == 0 && driver->scan)
			driver->scan(vdev);
	}
	return 0;
}

void unregister_virtio_driver(struct virtio_driver* driver) {
	for (int i = 0; i < _num_drivers; i++) {
		if (_registered_drivers[i] == driver) {
			_registered_drivers[i] = _registered_drivers[_num_drivers - 1];
			_num_drivers--;
			return;
		}
	}
}

/* ─── Device operations ─── */

static uint8_t shim_get_status(struct virtio_device* vdev) {
	return vdev->au_dev->common->DeviceStatus;
}

static void shim_set_status(struct virtio_device* vdev, uint8_t status) {
	vdev->au_dev->common->DeviceStatus = status;
	isb_flush();
	dsb_ish();
}

/*
 * Device config is Device memory. memcpy() of 32 bytes or more is an
 * `ldp q0, q1` from an 8-byte-aligned union (virtio-input offset 8) and
 * takes an alignment fault. Byte accesses are the width the window allows.
 * See virtio_cread_bytes().
 */
static void shim_cfg_read(void* dst, const void* src, unsigned len) {
	const volatile uint8_t* s = src;
	uint8_t* d = dst;

	for (unsigned i = 0; i < len; i++)
		d[i] = s[i];
}

static void shim_cfg_write(void* dst, const void* src, unsigned len) {
	const uint8_t* s = src;
	volatile uint8_t* d = dst;

	for (unsigned i = 0; i < len; i++)
		d[i] = s[i];
}

static void shim_get(struct virtio_device* vdev, unsigned offset, void* buf, unsigned len) {
	shim_cfg_read(buf, (const char*)vdev->au_dev->deviceCfg + offset, len);
}

static void shim_set(struct virtio_device* vdev, unsigned offset, const void* buf, unsigned len) {
	shim_cfg_write((char*)vdev->au_dev->deviceCfg + offset, buf, len);
	isb_flush();
	dsb_ish();
}

static u32 shim_generation(struct virtio_device* vdev) {
	(void)vdev;
	return 0;
}

static u64 shim_get_features(struct virtio_device* vdev) {
	struct VirtioCommonCfg* c = vdev->au_dev->common;

	c->DevFeatureSelect = 0;
	isb_flush();
	dsb_ish();
	uint32_t lo = c->DevFeature;
	c->DevFeatureSelect = 1;
	isb_flush();
	dsb_ish();
	uint32_t hi = c->DevFeature;

	return ((u64)hi << 32) | lo;
}

static int shim_finalize_features(struct virtio_device* vdev) {
	struct VirtioCommonCfg* c = vdev->au_dev->common;

	c->GuestFeatureSelect = 1;
	c->GuestFeature = c->DevFeature | (1U << (VIRTIO_F_VERSION_1_BIT - 32));
	isb_flush();
	dsb_ish();
	c->GuestFeatureSelect = 0;
	c->GuestFeature = c->DevFeature;
	isb_flush();
	dsb_ish();

	c->DeviceStatus |= VIRTIO_STATUS_FEATURES_OK;
	isb_flush();
	dsb_ish();

	if (!(c->DeviceStatus & VIRTIO_STATUS_FEATURES_OK)) {
		UARTDebugOut("[dcl-virtio]: device rejected feature set\r\n");
		c->DeviceStatus |= VIRTIO_STATUS_FAILED;
		return -1;
	}
	return 0;
}

static void shim_reset(struct virtio_device* vdev) {
	vdev->au_dev->common->DeviceStatus = 0;
	isb_flush();
	dsb_ish();
}

static int shim_find_vqs(struct virtio_device* vdev,
						 unsigned nvqs,
						 struct virtqueue** vqs,
						 struct virtqueue_info* vqs_info,
						 void* desc) {
	(void)desc;

	for (unsigned i = 0; i < nvqs; i++) {
		struct virtqueue* vq = (struct virtqueue*)kmalloc(sizeof(*vq));
		if (!vq)
			return -1;
		memset(vq, 0, sizeof(*vq));

		vq->name = (vqs_info && vqs_info[i].name) ? vqs_info[i].name : "vq";
		vq->vdev = vdev;
		vq->index = i;
		vq->callback = vqs_info ? vqs_info[i].callback : NULL;

		/* cookies[] cannot name a descriptor past VQ_COOKIE_MAX. Ask the
		 * device for a shorter ring before the queue is programmed. */
		if (vdev->au_dev && vdev->au_dev->common) {
			struct VirtioCommonCfg* cfg = vdev->au_dev->common;
			cfg->QueueSelect = (uint16_t)i;
			isb_flush();
			dsb_ish();
			if (cfg->QueueSize > VQ_COOKIE_MAX)
				cfg->QueueSize = VQ_COOKIE_MAX;
		}

		uint16_t qsize = AuVirtioPCISetupQueue(
			vdev->au_dev, i, &vq->desc, &vq->avail, &vq->used, VIRTIO_MSI_NO_VECTOR);

		if (qsize == 0) {
			UARTDebugOut("[dcl-virtio]: vq setup failed\r\n");
			kfree(vq);
			return -1;
		}

		vq->qsize = qsize;
		vq->num_free = qsize;
		vq->free_head = 0;

		for (uint16_t j = 0; j < qsize - 1; j++) {
			vq->desc[j].next = j + 1;
		}
		vq->desc[qsize - 1].next = 0xFFFF;

		vqs[i] = vq;
		/*
		 * Link it into its device's queue list -- the list
		 * virtio_device_for_each_vq() walks in remove_vqs()
		 * (virtio_console.c:1895) before freeing every in-flight
		 * buffer. virtio_device_register() initialises the head, and
		 * list_add writes both links itself, so the memset() above
		 * has nothing to do with this one.
		 */
		list_add(&vq->list, &vdev->vqs);
		if (_num_live_vqs < MAX_LIVE_VQS) {
			_live_vqs[_num_live_vqs++] = vq;
		} else {
			/*
			 * A queue outside this table is never polled, so its
			 * driver waits out every budget against a completion
			 * that is already sitting in the used ring; a full
			 * table must be a boot that says so, not one that
			 * limps.
			 */
			UARTDebugOut("[dcl-virtio]: _live_vqs full, queue %d not polled\r\n", i);
		}
	}

	return 0;
}

static void shim_del_vqs(struct virtio_device* vdev) {
	/* TODO: tear down individual vqs and free memory */
	(void)vdev;
}

/*
 * virtio_find_vqs / virtio_find_single_vq / virtio_device_ready -- the
 * mainline wrappers a ported driver calls, declared in <linux/virtio.h> and
 * defined here.
 *
 * virtio_find_vqs had a declaration in virtio.h and no definition anywhere:
 * nothing in the tree referenced it, because the prebuilt virtio_rng.ko
 * carried its own inline copy of the whole helper family. It is the dispatch
 * point -- mainline's vp_find_vqs() is what walks the config ops -- so the
 * body is the config-op call and nothing more. The NULL guards are not
 * defensive padding: probe runs before anyone has validated that a device
 * advertises find_vqs, and a NULL call there would be a fault with no
 * register to point at.
 *
 * virtio_find_single_vq is what virtio-rng.c:177 actually uses. It reports
 * failure as an error pointer rather than NULL, which is why the driver can
 * write `if (IS_ERR(vi->vq)) { err = PTR_ERR(vi->vq);` and unwind -- returning
 * NULL would sail past IS_ERR() and then fault on the first
 * virtqueue_add_inbuf(). ERR_PTR/PTR_ERR stay 32-bit-clean on this target
 * because ERR_PTR sign-extends through `(long)` before widening to a pointer,
 * so the top 12 bits are all ones and IS_ERR's range check still holds.
 *
 * virtio_device_ready sets DRIVER_OK, which is the transition that lets the
 * device start filling the ring. It is called between find_vqs and the first
 * add_inbuf, and doing it out of order (or not at all) leaves the device in
 * DRIVER state with an enabled ring nobody is allowed to use -- the symptom
 * is a virtqueue that never produces a completion, which reads exactly like a
 * broken interrupt.
 */
int virtio_find_vqs(struct virtio_device* vdev,
					unsigned nvqs,
					struct virtqueue** vqs,
					struct virtqueue_info* vqs_info,
					void* desc) {
	if (!vdev || !vdev->config || !vdev->config->find_vqs)
		return -1;
	return vdev->config->find_vqs(vdev, nvqs, vqs, vqs_info, desc);
}

struct virtqueue* virtio_find_single_vq(struct virtio_device* vdev,
					void (*cb)(struct virtqueue* vq),
					const char* name) {
	struct virtqueue* vq = NULL;
	struct virtqueue_info info = {
		.name = name,
		.callback = cb,
		.ctx = false,
	};
	int err = virtio_find_vqs(vdev, 1, &vq, &info, NULL);
	if (err)
		return (struct virtqueue*)ERR_PTR(err);
	return vq;
}

void virtio_device_ready(struct virtio_device* vdev) {
	u8 status;
	if (!vdev || !vdev->config || !vdev->config->get_status ||
	    !vdev->config->set_status)
		return;
	status = vdev->config->get_status(vdev);
	vdev->config->set_status(vdev, status | VIRTIO_CONFIG_S_DRIVER_OK);
}

/* ─── Virtqueue operations ─── */

static uint16_t alloc_desc(struct virtqueue* vq) {
	if (vq->num_free == 0)
		return 0xFFFF;

	uint16_t idx = vq->free_head;
	vq->free_head = vq->desc[idx].next;
	vq->num_free--;
	return idx;
}

static void free_desc_chain(struct virtqueue* vq, uint16_t head) {
	while (vq->desc[head].flags & VRING_DESC_F_NEXT) {
		uint16_t next = vq->desc[head].next;
		vq->desc[head].next = vq->free_head;
		vq->free_head = head;
		vq->num_free++;
		head = next;
	}
	vq->desc[head].next = vq->free_head;
	vq->free_head = head;
	vq->num_free++;
}

static uint64_t buf_phys(void* va) {
	uint64_t a = (uint64_t)va;
	uint64_t base = (uint64_t)AuGetPhysicalAddress(a);
	return base ? base + (a & 0xFFF) : 0;
}

/*
 * vq_publish() -- close a chain and hand its head to the avail ring. Both
 * add walks end here so the publish order lives in one place: descriptor
 * writes and the ring slot first, then the index that announces them, then
 * the barriers that make the order hold for the device.
 */
static void vq_publish(struct virtqueue* vq, uint16_t head, void* data) {
	if (head >= vq->qsize || head >= VQ_COOKIE_MAX)
		return;
	vq->cookies[head] = data;

	uint16_t avail_idx = vq->avail->idx;
	vq->avail->ring[avail_idx % vq->qsize] = head;
	isb_flush();
	dsb_ish();
	vq->avail->idx = avail_idx + 1;
	isb_flush();
	dsb_ish();
}

/*
 * virtqueue_add_sgs() -- mainline's multi-chain submit: `sgs[]` holds
 * scatterlist heads, the first `out_cnt` chains the device reads and the
 * next `in_cnt` it writes. DCL's scatterlist has no chain terminator (see
 * the note in <linux/scatterlist.h>), so each head is one entry and the
 * walk is the array itself -- the shape virtgpu_vq.c hands in, and the
 * shape the declaration in <linux/virtio.h> carries with the whole story
 * of the flat spelling it replaced.
 *
 * A failed reservation tears the chain down through free_desc_chain(),
 * which follows VRING_DESC_F_NEXT -- that is why the flag is OR-ed in only
 * after ->next holds a real slot: a half-built chain must never say NEXT
 * over a stale next. The return is mainline's -ENOSPC, which is exactly
 * what virtio_gpu_queue_cursor() tests for when it waits for room and
 * retries (virtgpu_vq.c:568).
 */
int virtqueue_add_sgs(struct virtqueue* vq, struct scatterlist* sgs[],
					  unsigned int out_cnt,
					  unsigned int in_cnt,
					  void* data,
					  gfp_t gfp) {
	(void)gfp;
	uint16_t head = alloc_desc(vq);
	if (head == 0xFFFF)
		return -ENOSPC;

	uint16_t cur = head;
	unsigned int total = out_cnt + in_cnt;
	for (unsigned int n = 0; n < total; n++) {
		struct scatterlist* sg = sgs[n];

		vq->desc[cur].addr = buf_phys(sg->buf);
		vq->desc[cur].len = sg->len;
		vq->desc[cur].flags = n >= out_cnt ? VRING_DESC_F_WRITE : 0;

		if (n < total - 1) {
			uint16_t next = alloc_desc(vq);
			if (next == 0xFFFF) {
				free_desc_chain(vq, head);
				return -ENOSPC;
			}
			vq->desc[cur].next = next;
			vq->desc[cur].flags |= VRING_DESC_F_NEXT;
			cur = next;
		}
	}

	vq_publish(vq, head, data);
	return 0;
}

/*
 * virtqueue_add_flat() -- the add_inbuf()/add_outbuf() walk: `cnt`
 * consecutive entries starting at `sg`, every one the same direction.
 * mainline's wrappers take a chain head and let the chain's end bound the
 * walk; DCL's scatterlist has no end marker, so the bound is the count in
 * the wrapper's signature -- and that is the same set of entries for every
 * caller in the tree, because each passes a chain whose entries are
 * consecutive: sg_init_one()'s single entry, or the page chain
 * __send_to_port() hands add_outbuf() at virtio_console.c:611. That count
 * is load-bearing there: a one-entry walk would put only the first page of
 * a larger write on the queue.
 */
static int virtqueue_add_flat(struct virtqueue* vq, struct scatterlist* sg,
			      unsigned int cnt, bool write, void* data) {
	uint16_t head = alloc_desc(vq);
	if (head == 0xFFFF)
		return -ENOSPC;

	uint16_t cur = head;
	for (unsigned int i = 0; i < cnt; i++) {
		vq->desc[cur].addr = buf_phys(sg[i].buf);
		vq->desc[cur].len = sg[i].len;
		vq->desc[cur].flags = write ? VRING_DESC_F_WRITE : 0;

		if (i < cnt - 1) {
			uint16_t next = alloc_desc(vq);
			if (next == 0xFFFF) {
				free_desc_chain(vq, head);
				return -ENOSPC;
			}
			vq->desc[cur].next = next;
			vq->desc[cur].flags |= VRING_DESC_F_NEXT;
			cur = next;
		}
	}

	vq_publish(vq, head, data);
	return 0;
}

int virtqueue_add_inbuf(
	struct virtqueue* vq, struct scatterlist* sg, unsigned int num_in, void* data, gfp_t gfp) {
	/* gfp dropped -- see the note on the declaration. The walk is the flat
	 * one: mainline's add_inbuf() wraps the chain head into a one-element
	 * sgs[] and lets the chain's own end bound the walk, and with no end
	 * marker in DCL's scatterlist the count in this signature does that
	 * job instead -- num_in entries from sg, which is the same set for
	 * the single-entry chains every caller passes (virtio-rng.c:65,
	 * virtio_console.c:490). */
	(void)gfp;
	return virtqueue_add_flat(vq, sg, num_in, true, data);
}

int virtqueue_add_inbuf_cache_clean(
	struct virtqueue* vq, struct scatterlist* sg, unsigned int num_in, void* data, gfp_t gfp) {
	/* The ring code has no DMA cache metadata to carry the "clean after
	 * write" hint; the receive-side invalidate in virtqueue_get_buf() is
	 * where correctness is kept. */
	return virtqueue_add_inbuf(vq, sg, num_in, data, gfp);
}

unsigned int virtqueue_get_vring_size(const struct virtqueue* vq) {
	return vq ? vq->qsize : 0;
}

bool virtqueue_kick_prepare(struct virtqueue* vq) {
	uint16_t used_idx = vq->used->idx;
	uint16_t avail_idx = vq->avail->idx;

	if (vq->used->flags & 0x1) {
		return true;
	}

	return used_idx != avail_idx;
}

void virtqueue_notify(struct virtqueue* vq) {
	AuVirtioPCINotifyQueue(vq->vdev->au_dev, vq->index);
}

bool virtqueue_kick(struct virtqueue* vq) {
	if (virtqueue_kick_prepare(vq)) {
		virtqueue_notify(vq);
		return true;
	}
	return false;
}

/*
 * virtqueue_disable_cb() / virtqueue_enable_cb() -- mainline's
 * callback-suppression pair, added with the virtio-gpu port. That driver
 * brackets each reclamation with them: disable, drain until get_buf()
 * returns NULL, then enable, looping if enable reports that the ring moved
 * while it drained (virtgpu_vq.c's dequeue functions). Mainline's
 * callbacks are interrupts and the loop exists to not be interrupted
 * mid-drain.
 *
 * There is no interrupt here: completions are reaped by the
 * virtio_poll_vqs() pump, which re-reads the used ring on every pass and
 * has no enable bit to test. So disable() has nothing to turn off and
 * enable() nothing to turn back on. enable()'s answer is the one the loop
 * reads -- mainline's "true = no buffer arrived between the last get_buf
 * and this call" -- and true is the honest sequencing: the drain above
 * just emptied the ring, and anything that lands after this returns
 * advances the used index past the consumer's, which the next pump pass
 * turns into another callback. The loop runs once past a drain; the pump
 * covers everything after it.
 */
void virtqueue_disable_cb(struct virtqueue* vq) {
	(void)vq;
}

bool virtqueue_enable_cb(struct virtqueue* vq) {
	(void)vq;
	return true;
}

void* virtqueue_get_buf(struct virtqueue* vq, unsigned int* len) {
	dc_ivac((uint64_t)&vq->used->idx);
	dsb_sy_barrier();
	uint16_t used_idx = vq->used->idx;
	uint16_t last_used = vq->event_idx_write;

	if (last_used == used_idx)
		return NULL;

	struct VirtqUsedElem* elem = &vq->used->ring[last_used % vq->qsize];
	dc_ivac((uint64_t)elem);
	dsb_sy_barrier();
	uint16_t desc_idx = (uint16_t)elem->id;

	if (len)
		*len = elem->len;

	/* A device-reported id past the ring is not a descriptor. Indexing
	 * cookies[] with it writes off the end of this virtqueue. */
	if (desc_idx >= vq->qsize || desc_idx >= VQ_COOKIE_MAX) {
		vq->event_idx_write = last_used + 1;
		return NULL;
	}

	bool device_written = (vq->desc[desc_idx].flags & VRING_DESC_F_WRITE) != 0;
	void* cookie = vq->cookies[desc_idx];

	/*
	 * A device-writable descriptor is the only one the device has just
	 * stored into.  Cortex-A72 under QEMU does not snoop its caches for
	 * virtio DMA, so invalidate the lines before the driver reads the
	 * payload.  The range covers mainline's scatterlist length and is
	 * rounded out to cache lines; dc_ivac is the IRQ-safe form.
	 */
	if (device_written && cookie && len && *len) {
		uint64_t start = (uint64_t)cookie & ~0x3FULL;
		uint64_t end = (uint64_t)cookie + *len;
		for (uint64_t p = start; p < end; p += 64)
			dc_ivac(p);
		dsb_sy_barrier();
	}

	free_desc_chain(vq, desc_idx);
	vq->cookies[desc_idx] = NULL;

	vq->event_idx_write = last_used + 1;
	return cookie;
}

/* ─── Device registration & probe ─── */

/*
 * The vendor and the three function ids this shim scans for. They sit here
 * rather than beside the scan because virtio_device_register() needs the
 * vendor too: the parent lookup below turns the device's reported type back
 * into a raw id and asks the PCI fabric for the function it came from.
 */
#define VIRTIO_PCI_VENDOR_ID   0x1AF4
#define VIRTIO_PCI_DEV_ENTROPY 0x1044
#define VIRTIO_PCI_DEV_CONSOLE 0x1043
#define VIRTIO_PCI_DEV_GPU     0x1050

static bool virtio_pci_matches(uint64_t addr, int bus, int dev, int func,
			       uint16_t pci_id) {
	uint16_t vendor = AuPCIERead(addr, PCI_VENDOR_ID, bus, dev, func);
	uint16_t device = AuPCIERead(addr, PCI_DEVICE_ID, bus, dev, func);

	return vendor == VIRTIO_PCI_VENDOR_ID && device == pci_id;
}

void virtio_device_register(struct virtio_device* vdev,
							struct VirtioPCIDevice* au_dev,
							int dev_index) {
	memset(vdev, 0, sizeof(*vdev));
	vdev->index = dev_index;
	vdev->au_dev = au_dev;

	/*
	 * Two members the memset just put into a state that must not survive
	 * it.
	 *
	 * id.device is mainline's device identity, filled by its virtio core
	 * from the PCI/PV device ID; DCL has no equivalent step, and the only
	 * reader -- is_rproc_serial() at virtio_console.c:332 -- would
	 * otherwise compare against 0 for every device.
	 *
	 * vqs is the queue list head, and a zeroed list_head is not an empty
	 * one: list_empty() is `head->next == head`, and NULL never equals the
	 * address of the head, so a walk started on a zeroed head reads its
	 * NULL next as though it were a struct virtqueue and faults on the
	 * first member. INIT_LIST_HEAD makes it the empty list it is meant to
	 * be -- the list itself costs nothing, only its being initialised.
	 */
	vdev->id.device = virtio_device_type(au_dev);
	INIT_LIST_HEAD(&vdev->vqs);

	/*
	 * The device's parent: mainline's virtio-pci probe sets the same
	 * relation when it creates a virtio device on a PCI function, and
	 * the vendored virtio-gpu driver is the first consumer that needs
	 * it. virtio_gpu_probe() calls
	 * drm_dev_alloc(&driver, vdev->dev.parent), whose drm_dev_init()
	 * refuses a NULL parent (drm_drv.c:715), and the quirk gate just
	 * after it asks dev_is_pci(parent); drm_dev_init() also reads the
	 * parent's name for dev->unique, and the fabric's busid string is
	 * what that field should carry. The console's lone reader
	 * (virtio_console.c:433) stays unreachable either way: its arm sits
	 * behind is_rproc_serial()'s constant-false first term.
	 *
	 * The type virtio_device_type() just reported is the raw function
	 * id minus the base, so adding the base back is that conversion
	 * run the other way; pci_get_device() then finds the struct pci_dev
	 * the fabric built for the function -- by (vendor, device)
	 * identity, the lookup mainline's init uses, scanning the bus
	 * itself if this is its first caller. A NULL answer (a function
	 * the fabric never enumerated) leaves the member NULL, where
	 * mainline leaves it for its non-PCI virtio devices.
	 *
	 * Set before the snapshot copy below, so the live device and the
	 * _virtio_devices[] entry -- the one register_virtio_driver()
	 * probes when a driver arrives after its device -- carry the same
	 * parent.
	 */
	{
		uint16_t did = (uint16_t)(vdev->id.device + VIRTIO_PCI_DEV_BASE);
		struct pci_dev* pdev = pci_get_device(VIRTIO_PCI_VENDOR_ID, did, NULL);

		if (pdev)
			vdev->dev.parent = &pdev->dev;
	}

	static struct virtio_config_ops shim_ops = {
		.get = shim_get,
		.set = shim_set,
		.generation = shim_generation,
		.get_status = shim_get_status,
		.set_status = shim_set_status,
		.reset = shim_reset,
		.find_vqs = shim_find_vqs,
		.del_vqs = shim_del_vqs,
		.get_features = shim_get_features,
		.finalize_features = shim_finalize_features,
	};
	vdev->config = &shim_ops;

	vdev->config->device_config_base = vdev->au_dev->deviceCfg;

	/* Queues created in probe() store this address. The array below is
	 * a copy, and a later driver may be probed on the copy instead. */
	if (_num_probed_vdevs < MAX_VIRTIO_DEVICES)
		_probed_vdevs[_num_probed_vdevs++] = vdev;

	if (_num_devices < MAX_VIRTIO_DEVICES) {
		_virtio_devices[_num_devices++] = *vdev;
		/*
		 * The copy above is a lookup snapshot, and this line is what
		 * keeps it safe to look at: a struct assignment copies the
		 * list_head's *links*, which still name the original's head.
		 * Walking the copy then reads that head as if it were a
		 * struct virtqueue -- a type confusion, on an address that is
		 * still valid, which is worse than a fault because nothing
		 * says so. Re-linking the copy to itself makes it an empty
		 * list: walking it does nothing, which is the truth for a
		 * snapshot taken before any queue exists.
		 */
		INIT_LIST_HEAD(&_virtio_devices[_num_devices - 1].vqs);
	}

	for (int i = 0; i < _num_drivers; i++) {
		struct virtio_driver* drv = _registered_drivers[i];
		if (!virtio_id_match(drv, virtio_device_type(au_dev)))
			continue;
		UARTDebugOut("[dcl-virtio]: matched, calling probe\r\n");
		vdev->priv = drv;
		int err = drv->probe ? drv->probe(vdev) : -1;
		if (err) {
			UARTDebugOut("[dcl-virtio]: probe failed\r\n");
		} else {
			UARTDebugOut("[dcl-virtio]: probe done\r\n");
			if (!(vdev->config->get_status(vdev) & VIRTIO_CONFIG_S_DRIVER_OK))
				vdev->config->set_status(
					vdev, vdev->config->get_status(vdev) | VIRTIO_CONFIG_S_DRIVER_OK);
			if (drv->scan) {
				UARTDebugOut("[dcl-virtio]: calling scan\r\n");
				drv->scan(vdev);
			}
#ifdef __XENEVA_BYPASS_VIRTGPU__
			/*
			 * Bypass: the vendored virtio-gpu owns 1AF4:1050 and the
			 * internal virtgpu.dll is out of the image, so nothing has
			 * claimed the display for the console yet. Hand it over
			 * now that the driver is live (DCL/virtgpu_console.c).
			 */
			if (vdev->id.device == VIRTIO_ID_GPU)
				dcl_virtgpu_console_attach(vdev);
#endif
		}
		return;
	}
	UARTDebugOut("[dcl-virtio]: no driver matched\r\n");
}

/* virtio_scan_and_register() results. */
#define VIRTIO_SCAN_NO_DEVICE  0 /* the bus has no function with this id */
#define VIRTIO_SCAN_INIT_FAIL  (-1) /* matched, but AuVirtioPCIInit() said no */
#define VIRTIO_SCAN_REGISTERED 1 /* matched, registered, probe ran */

/*
 * virtio_scan_and_register() -- walk the bus for one virtio id and bring the
 * first match up. It is a scan at all for the reason virtio_rng_detect()'s
 * has to be one: at initcall time there is no device yet, and a driver that
 * registers and never sees a device looks exactly like one that was never
 * registered. The two scans skip each other's device by id, so whichever
 * runs first leaves the other's for it -- and virtio_device_register() only
 * probes drivers whose id_table matches, so registering a console device
 * against a driver that is not yet on the list would fail loudly at probe
 * rather than silently, which is why the initcall runs first.
 *
 * vdev and au_dev belong to the caller rather than being static here:
 * register() keeps their addresses, so one shared pair would be memset() by
 * the second scan while the first device is still driving it.
 *
 * The only things the two detect paths disagreed on were the id, the name in
 * the two log lines, and the index handed to register(); folding those into
 * arguments is what stops the next id -- a virtio-blk, say -- from being a
 * 30-line copy with one hex digit changed.
 */
static int virtio_scan_and_register(uint16_t pci_id, const char* name, int index,
				    struct virtio_device* vdev,
				    struct VirtioPCIDevice* au_dev) {
	UARTDebugOut("[dcl-virtio]: scanning for %s\r\n", name);
	for (int b = 0; b < 256; b++) {
		for (int d = 0; d < 32; d++) {
			for (int f = 0; f < 8; f++) {
				uint64_t addr = AuPCIEGetDevice(0, b, d, f);
				if (!addr || addr == 0xFFFFFFFF)
					continue;
				if (!virtio_pci_matches(addr, b, d, f, pci_id))
					continue;

				if (!AuVirtioPCIInit(addr, b, d, f, 0, au_dev)) {
					UARTDebugOut("[dcl-virtio]: %s init failed\r\n", name);
					return VIRTIO_SCAN_INIT_FAIL;
				}
				virtio_device_register(vdev, au_dev, index);
				UARTDebugOut("[dcl-virtio]: %s device registered\r\n", name);
				return VIRTIO_SCAN_REGISTERED;
			}
		}
	}
	return VIRTIO_SCAN_NO_DEVICE;
}

/*
 * virtio-input is the one virtio id that appears more than once: QEMU adds a
 * keyboard and a tablet as two separate PCI functions, both 1AF4:1052.  The
 * generic scan above stops at the first match by design, so this one walks
 * the bus to completion and gives every function its own vdev/au_dev pair.
 * Storage is static because virtio_device_register() keeps both addresses.
 */
#define VIRTIO_PCI_DEV_INPUT 0x1052
#define MAX_DCL_INPUT_DEVICES 4

int virtio_input_detect(void) {
	static struct virtio_device vdevs[MAX_DCL_INPUT_DEVICES];
	static struct VirtioPCIDevice au_devs[MAX_DCL_INPUT_DEVICES];
	int found = 0;

	UARTDebugOut("[dcl-virtio]: scanning for virtio-input\r\n");
	for (int b = 0; b < 256 && found < MAX_DCL_INPUT_DEVICES; b++) {
		for (int d = 0; d < 32 && found < MAX_DCL_INPUT_DEVICES; d++) {
			for (int f = 0; f < 8 && found < MAX_DCL_INPUT_DEVICES; f++) {
				uint64_t addr = AuPCIEGetDevice(0, b, d, f);
				if (!addr || addr == 0xFFFFFFFF)
					continue;
				if (!virtio_pci_matches(addr, b, d, f,
								VIRTIO_PCI_DEV_INPUT))
					continue;

				if (!AuVirtioPCIInit(addr, b, d, f, 0,
									 &au_devs[found])) {
					UARTDebugOut("[dcl-virtio]: virtio-input init failed at %d:%d.%d\r\n",
								 b, d, f);
					continue;
				}
				virtio_device_register(&vdevs[found], &au_devs[found],
									   _num_devices);
				found++;
			}
		}
	}

	if (!found)
		UARTDebugOut("[dcl-virtio]: no virtio-input device on the bus\r\n");
	return found;
}

void virtio_rng_detect(void) {
	static struct virtio_device vdev;
	static struct VirtioPCIDevice au_dev;

	virtio_scan_and_register(VIRTIO_PCI_DEV_ENTROPY, "virtio-rng", 0, &vdev, &au_dev);
}

/*
 * The index is _num_devices -- the slot this device takes in DCL's own list
 * -- rather than a hardcoded 0 like the rng's. virtio_device.index is only
 * read for the /dev/vport%up%u name (virtio_console.c:1371), and two devices
 * both claiming 0 would name their ports identically.
 *
 * Returns 1 when a device was found and registered, 0 when the bus had none
 * or init failed. The caller needs to tell those apart because only the
 * first case will ever produce a /dev/vport* node, and waiting for one that
 * no device can create would be a fixed dead time on every boot with
 * --no-virtio-serial. The "absent" line is printed here rather than by the
 * scan so an init failure reports only its own message.
 */
int virtio_console_detect(void) {
	static struct virtio_device vdev;
	static struct VirtioPCIDevice au_dev;
	int rc = virtio_scan_and_register(VIRTIO_PCI_DEV_CONSOLE, "virtio-console",
					  _num_devices, &vdev, &au_dev);

	if (rc == VIRTIO_SCAN_NO_DEVICE)
		UARTDebugOut("[dcl-virtio]: no virtio-console device on the bus\r\n");
	return rc == VIRTIO_SCAN_REGISTERED;
}

/*
 * virtio_gpu_detect() -- the DRM twin of the console scan: same walk, same
 * skip-the-others-by-id contract, and _num_devices for the index because
 * this is the third device DCL owns and its slot is wherever the first two
 * left it.
 *
 * The driver it binds to is the vendored virtio-gpu, registered by its
 * initcall in DclRunInitcalls() like the two character drivers'. What
 * differs is who calls this: only a __XENEVA_BYPASS_VIRTGPU__ build does
 * (DCL/modload_test.c), because in a default image 1AF4:1050 belongs to the
 * internal virtgpu.dll. The vendored driver is registered there too -- that
 * is harmless, since nothing registers a DCL virtio device with this id
 * outside this function -- but the scan must never run.
 *
 * Returns 1 when the device was found and registered, 0 when the bus had
 * none or init failed, matching virtio_console_detect()'s contract.
 */
int virtio_gpu_detect(void) {
	static struct virtio_device vdev;
	static struct VirtioPCIDevice au_dev;
	int rc = virtio_scan_and_register(VIRTIO_PCI_DEV_GPU, "virtio-gpu",
					  _num_devices, &vdev, &au_dev);

	if (rc == VIRTIO_SCAN_NO_DEVICE)
		UARTDebugOut("[dcl-virtio]: no virtio-gpu device on the bus\r\n");
	return rc == VIRTIO_SCAN_REGISTERED;
}

/* ─── entry points virtio_console.c calls that DCL had no body for ──────── */

/*
 * The four below are the whole of the difference between a driver that
 * compiled and one that linked. Every one is mainline's shape, taken from
 * the declaration in <linux/virtio.h> rather than from how the call sites
 * happen to use them -- C links by name alone, so a signature that merely
 * "works" would call the right symbol with the wrong register contract and
 * show up as a wrong answer somewhere else entirely.
 */

/*
 * virtqueue_add_outbuf() -- the transmit-side add: `num` device-bound
 * buffers, no receive ones, which is virtqueue_add_flat() with the write
 * flag the other way from virtqueue_add_inbuf() directly above.
 *
 * The gfp is accepted and dropped for the reason slab.h drops its own
 * flags argument -- Xeneva's allocator makes no GFP_KERNEL/GFP_ATOMIC
 * distinction and a queue buffer is never allocated from an interrupt
 * context that could not have waited.
 */
int virtqueue_add_outbuf(struct virtqueue* vq, struct scatterlist sg[],
			 unsigned int num, void* data, gfp_t gfp)
{
	(void)gfp;
	return virtqueue_add_flat(vq, sg, num, false, data);
}

/*
 * virtqueue_is_broken() -- has the device gone away? Reads the flag
 * virtio_break_device() sets; false until then, which is what every
 * `while (!virtqueue_get_buf(vq, &len) && !virtqueue_is_broken(vq)) cpu_relax();`
 * spin in the driver wants before a removal has happened.
 */
bool virtqueue_is_broken(const struct virtqueue* vq)
{
	return vq ? vq->broken : false;
}

/*
 * virtqueue_detach_unused_buf() -- take a buffer off the queue and hand it
 * back so the caller can free it; NULL when none is left, which is what
 * ends remove_vqs()'s `while ((buf = virtqueue_detach_unused_buf(vq)))`.
 *
 * The descriptor is returned to the free list along with the cookie, because
 * the cookie is only half the slot's ownership and nothing after this
 * allocates: del_vqs() -- the only call left in remove_vqs() -- is a TODO in
 * this file that frees nothing, so a chain left allocated here stays
 * allocated for good. `cookies[i]` being set means get_buf() has not run for
 * this slot, and get_buf() is the only other thing that clears it, so the
 * chain cannot be one the free list already holds.
 */
void* virtqueue_detach_unused_buf(struct virtqueue* vq)
{
	if (!vq)
		return NULL;

	for (uint16_t i = 0; i < vq->qsize; i++) {
		void* cookie = vq->cookies[i];

		if (!cookie)
			continue;

		vq->cookies[i] = NULL;
		free_desc_chain(vq, i);
		return cookie;
	}
	return NULL;
}

/*
 * virtio_break_device(dev) -- mark every queue on a device broken, so the
 * flush loops that follow stop waiting on hardware that has already left.
 * virtcons_remove() calls it first (virtio_console.c:1920), before the
 * reset and the flushes, and mainline's body is the same one-line walk.
 *
 * The walk needs `vdev->vqs` to be a real empty-or-not list, which is what
 * the two INIT_LIST_HEAD() calls in virtio_device_register() are for; a
 * head that was only zeroed would have this reading NULL as a struct
 * virtqueue on its very first step.
 */
void virtio_break_device(struct virtio_device* dev)
{
	struct virtqueue* vq;

	if (!dev)
		return;

	list_for_each_entry(vq, &dev->vqs, list)
		vq->broken = true;
}
