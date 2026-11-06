/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VIRTIO_CONFIG_H
#define _LINUX_VIRTIO_CONFIG_H

/*
 * DCL <linux/virtio_config.h> -- a router, and four helpers mainline puts
 * here that <linux/virtio.h> does not.
 *
 * ### Why this is not mainline's 699-line header
 *
 * mainline's <linux/virtio_config.h> carries its own copies of the three
 * types DCL's <linux/virtio.h> already defines: `struct virtqueue_info`
 * (:29), `struct virtio_config_ops` (:112) and, through the helpers, an
 * opinion about `struct virtio_device` -- it reads `vdev->features_array`
 * (:228, :239, :250) and declares `virtio_find_vqs()` with an
 * `struct irq_affinity *` parameter (:293) that disagrees with the one in
 * virtio.h. Two definitions of one struct in one translation unit is a hard
 * redefinition error, and DCL's version is the one that is authoritative:
 * it is the layout the native AuVirtIO layer fills in and the layout the
 * module loader indexes, and `features_array` is not a member of it and
 * cannot be added without deciding where in `_linux_prefix[0x328]` the
 * feature bitmap mainline keeps there is supposed to live.
 *
 * So the header includes <linux/virtio.h> and stops. Everything mainline
 * defines below the type declarations that DCL does not need is not needed
 * here either: virtio_cread8/16/32/64, virtio_get_status/set_status,
 * virtio_device_ready, virtio_find_vqs, the endian wrappers -- all of those
 * are already in virtio.h, which is the file this one routes to.
 *
 * ### The four helpers that are actually missing
 *
 * __virtio_test_bit / virtio_has_feature / virtio_cread_feature are what a
 * mainline driver tests a feature bit with; virtio_device_for_each_vq is the
 * list walk. They cannot come from virtio.h because mainline does not put
 * them there, and a ported source is written against mainline's *layout of
 * the headers*, not against where DCL happens to prefer things.
 */

#include <linux/virtio.h>
#include <linux/virtio_byteorder.h>	/* the uapi endian wrappers */
#include <uapi/linux/virtio_config.h>	/* VIRTIO_F_* and the status bits */

/*
 * __virtio_test_bit(vdev, fbit) -- is feature `fbit` agreed with this device?
 *
 * mainline reads `vdev->features_array[fbit]`: a bitmap of the features that
 * survived negotiation, filled once by virtio_finalise_features(). DCL has no
 * negotiation to have survived -- see shim_finalize_features() in
 * DCL/virtio_shim.c, which writes the device's whole feature set back
 * unfiltered and sets FEATURES_OK (it reports failure only if the device
 * itself rejects). The guest accepts everything the device offers, so
 * "offered" and "agreed" name the same bits, and the bitmap mainline caches
 * is here a query instead:
 *
 *     vdev->config->get_features(vdev)
 *
 * which is shim_get_features() -- two reads of the device's DevFeature
 * registers, select 0 then select 1, reassembled lo||hi. Two MMIO-ish reads
 * per feature test, where mainline does one load from memory, on paths that
 * run a handful of times at probe and once per config change: not worth a
 * cache, and a cache would have to be invalidated by something that does not
 * exist yet.
 *
 * The NULL guard is not defensive padding. use_multiport() calls this at
 * virtio_console.c:343 during probe, and probe runs for a device whose config
 * ops were installed by virtio_device_register() -- but a ported source that
 * builds its own virtio_device would not have gone through that path, and a
 * NULL call there faults with no register to point at. FALSE is the answer
 * that keeps the caller on the single-port path, which is the safe one.
 */
static inline bool __virtio_test_bit(const struct virtio_device* vdev,
				     unsigned int fbit)
{
	u64 features;

	if (!vdev || !vdev->config || !vdev->config->get_features)
		return false;

	features = vdev->config->get_features((struct virtio_device*)vdev);
	return (features >> fbit) & 1ULL;
}

/*
 * virtio_has_feature() -- the spelling drivers reach for. mainline wraps
 * __virtio_test_bit() with a debug check that the feature is one the driver
 * actually offered (typecheck() against the driver's feature_table, via
 * virtio_check_driver_offered_feature()); that check is a CONFIG that DCL
 * does not set, and reproducing its *expression* would mean reproducing
 * typecheck() for a branch that is compiled out. The answer is the same.
 */
static inline bool virtio_has_feature(const struct virtio_device* vdev,
				      unsigned int fbit)
{
	return __virtio_test_bit(vdev, fbit);
}

/*
 * virtio_has_dma_quirk() -- mainline's reverse-polarity ACCESS_PLATFORM test
 * (linux/virtio_config.h:714 in linux-7.2). A device without the feature has
 * not announced that it takes bus DMA addresses, so drivers fall back to
 * their non-DMA-API branch -- sg_phys() addresses instead of
 * sg_dma_address().
 *
 * That fallback is the branch this port wants, not merely tolerates: the
 * shim never offers VIRTIO_F_ACCESS_PLATFORM, and DCL's scatterlist is
 * {buf, len} with no page for an IOMMU to translate (see that header).
 * virtgpu_object.c:166 is the reader that made this a must-have.
 */
static inline bool virtio_has_dma_quirk(const struct virtio_device* vdev)
{
	return !virtio_has_feature(vdev, VIRTIO_F_ACCESS_PLATFORM);
}

/*
 * struct virtio_shm_region -- a shared-memory window as reported through the
 * config ops' get_shm_region (linux/virtio.h:135). mainline keeps the struct
 * in this header; the members are mainline's.
 */
struct virtio_shm_region {
	u64 addr;
	u64 len;
};

/*
 * virtio_get_shm_region() -- checked routing to config->get_shm_region.
 * mainline calls the op unconditionally, on the contract that a device
 * advertising the region installed it. DCL's shim installs no op at all
 * (none of its devices has a window), and a NULL call would fault in the
 * middle of virtio_gpu_init(); answering false is the same "no region" that
 * mainline's has_host_visible=false arm gets, and it keeps virtio-gpu's
 * non-blob resource paths working.
 */
static inline bool virtio_get_shm_region(struct virtio_device* vdev,
					 struct virtio_shm_region* region_out,
					 u8 id)
{
	if (!vdev || !vdev->config || !vdev->config->get_shm_region)
		return false;
	return vdev->config->get_shm_region(vdev, region_out, id);
}

/*
 * virtio_cread_le() / virtio_cwrite() / virtio_cwrite_le() -- mainline's
 * endian-converting config accessors, reduced to what LE-on-LE makes them.
 *
 * virtio_cread_le() is DCL's virtio_cread() (<linux/virtio.h>) byte for
 * byte: on a little-endian target the conversion mainline performs is the
 * identity, and the copy already lands in the caller's member-typed object.
 *
 * The write stages the member-typed value from *ptr before handing
 * config->set() the span -- set's DCL contract is (offset, buf, length) of
 * raw bytes, and staging through the struct member is what guarantees the
 * width written is the config field's, not whatever type the caller happened
 * to hold. The one writer today is virtgpu_kms.c:59's virtio_cwrite_le() of
 * events_clear.
 */
#define virtio_cread_le(vdev, structname, member, ptr)			\
	virtio_cread((vdev), structname, member, ptr)

#define virtio_cwrite(vdev, structname, member, ptr)			\
	do {								\
		typeof(((structname*)0)->member) _dcl_cw =		\
			(typeof(((structname*)0)->member))(*(ptr));	\
		(vdev)->config->set((vdev), offsetof(structname, member), \
				    &_dcl_cw, sizeof(_dcl_cw));		\
	} while (0)

#define virtio_cwrite_le(vdev, structname, member, ptr)			\
	virtio_cwrite((vdev), structname, member, ptr)

/*
 * virtio_cread_feature(vdev, fbit, structname, member, ptr) -- read a config
 * field, but only if feature `fbit` is agreed; otherwise report -ENOENT.
 *
 * mainline's is a statement expression (linux/virtio_config.h:678) with the
 * same three-part shape, and this is that expression verbatim. The caller at
 * virtio_console.c:1999 compares the result against 0, so the two arms have
 * to be 0 and a *negative* errno -- 0 on the read, -ENOENT when the feature
 * is not there. virtio_cread() comes from <linux/virtio.h>.
 *
 * `-ENOENT` needs <linux/err.h>'s errno, which reaches <linux/kernel.h>,
 * which includes <linux/virtio.h> before this file: the chain is already
 * closed by the time this macro is expanded, and a macro expands where it is
 * used, not where it is written.
 */
#define virtio_cread_feature(vdev, fbit, structname, member, ptr)	\
	({								\
		int _r = 0;						\
		if (!virtio_has_feature(vdev, fbit))			\
			_r = -ENOENT;					\
		else							\
			virtio_cread((vdev), structname, member, ptr);	\
		_r;							\
	})

/*
 * virtio_device_for_each_vq(vdev, vq) -- walk every queue on a device.
 *
 * mainline's is one line (linux/virtio.h:221):
 *
 *     list_for_each_entry(vq, &(vdev)->vqs, list)
 *
 * and this is the same line. It needs `vdev->vqs` (a list head, appended to
 * DCL's struct virtio_device) and `vq->list` (appended to struct virtqueue);
 * both were added for this macro, because the only caller -- remove_vqs(),
 * virtio_console.c:1895 -- is the loop that flushes and frees every in-flight
 * receive buffer, and a macro that quietly walked nothing would leave that
 * loop silently empty rather than obviously wrong.
 *
 * The head is initialised by virtio_device_register() and the queues are
 * linked in by shim_find_vqs(); _virtio_devices[]'s snapshot copy gets its
 * own empty head so that walking *it* terminates instead of treating the
 * original's head as if it were a struct virtqueue.
 */
#define virtio_device_for_each_vq(vdev, vq)	\
	list_for_each_entry(vq, &(vdev)->vqs, list)

/*
 * virtio_is_little_endian(vdev) -- byte order of this device's config fields
 * and vring entries.
 *
 * mainline (linux/virtio_config.h:399) forwards to
 * virtio_legacy_is_little_endian(), which is in <linux/virtio_byteorder.h>
 * -- the header included above, and DCL's copy answers it with
 * `#ifdef __LITTLE_ENDIAN` (line 9), which is true because types.h defines
 * __LITTLE_ENDIAN as 1234 and clang predefines no such macro for
 * aarch64-unknown-windows. The `vdev` parameter is accepted for mainline's
 * signature and ignored: the legacy/modern split it exists to query has no
 * second arm in a tree with one byte order.
 */
static inline bool virtio_is_little_endian(struct virtio_device* vdev)
{
	(void)vdev;
	return virtio_legacy_is_little_endian();
}

/*
 * The four converters a ported source names, in mainline's order and with
 * mainline's parameter types -- `struct virtio_device *`, non-const, because
 * mainline's is and a const mismatch would be a diagnostic on a call site
 * that has nothing to do with endianness.
 *
 * virtio_console.c:553/:554 use cpu_to_virtio32/16 to write a control
 * message into the out-queue; :1534/:1536/:1543 read the device's own
 * control word back. They route through virtio_is_little_endian() above, so
 * all four are the identity on this target -- which is the correct answer,
 * not a shortcut: QEMU's virtio-serial and the aarch64 ports are both
 * little-endian, and the bswap that would not happen here is the one that
 * must not.
 */
static inline u16 virtio16_to_cpu(struct virtio_device* vdev, __virtio16 val)
{
	return __virtio16_to_cpu(virtio_is_little_endian(vdev), val);
}

static inline __virtio16 cpu_to_virtio16(struct virtio_device* vdev, u16 val)
{
	return __cpu_to_virtio16(virtio_is_little_endian(vdev), val);
}

static inline u32 virtio32_to_cpu(struct virtio_device* vdev, __virtio32 val)
{
	return __virtio32_to_cpu(virtio_is_little_endian(vdev), val);
}

static inline __virtio32 cpu_to_virtio32(struct virtio_device* vdev, u32 val)
{
	return __cpu_to_virtio32(virtio_is_little_endian(vdev), val);
}

#endif /* _LINUX_VIRTIO_CONFIG_H */
