/**
 * sndproxy.c -- kernel half of a user-space sound driver.
 *
 * A server process (Process/sndsrv) that owns the sound hardware through
 * /dev/hwdev passes the kernel a shared ring (see BaseHdr/sndsrv.h). The
 * kernel then registers an ordinary sound card, "virtio-sound", whose write()
 * copies the mixer's PCM into that ring instead of touching hardware. The rest
 * of the sound stack (mixer, DSP clients) cannot tell the difference.
 *
 * Safety rules:
 *   - the ring must be one of the caller's own hwdev DMA buffers (AuHwdevOwnsDma)
 *   - write() never spins or waits on the server: it paces itself by sleeping,
 *     and if the server stops draining, data is dropped rather than hanging
 *   - when the server's device handle goes away (exit/crash) the card is
 *     removed and in-flight writers are drained BEFORE its memory is freed
 */
#include <Sound/sndproxy.h>
#include <Sound/sound.h>
#include <Fs/Dev/devhw.h>
#include <Fs/Dev/devfs.h>
#include <Fs/vfs.h>
#include <Cred/group.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/sched.h>
#include <Sync/spinlock.h>
#include <Serv/sysserv.h>
#include <sndsrv.h>
#include <string.h>
#include <_null.h>

extern uint64_t AuGetCurrentMS();

#define PX_DETACH_WAIT_MS   200

static struct {
	AuSound card;
	volatile bool attached;
	volatile int inflight;
	AuProcess* owner;
	SndRingHdr* hdr;
	uint8_t* data;
	uint64_t written, dropped, calls;
	/* audio that was still queued when the server went away; replayed into the
	 * next ring so a crash does not eat what the clients already wrote */
	uint8_t* carry;
	uint32_t carry_len;
	uint64_t carry_ms;           /* when it was taken; stale audio is not replayed */
} px;

static Spinlock px_lock;

#define PX_CARRY_MAX_AGE_MS 1500

static inline void px_barrier(void) {
	__asm__ volatile("dmb ish" ::: "memory");
}

static void px_cache_invalidate(void* address, size_t size) {
	uint64_t start = (uint64_t)address & ~63ULL;
	uint64_t end = (uint64_t)address + size;
	for (; start < end; start += 64)
		dc_ivac(start);
	dsb_sy_barrier();
}

static void px_cache_clean(void* address, size_t size) {
	if (size)
		aa64_data_cache_clean_range(address, size);
}

static void px_cache_clean_ring_bytes(uint8_t* data, uint32_t size,
	uint32_t offset, uint32_t length) {
	uint32_t first = length < size - offset ? length : size - offset;
	px_cache_clean(data + offset, first);
	if (first < length)
		px_cache_clean(data, length - first);
}

/* The mixer's per-period write: copy into the ring and, once a few periods are
 * queued, put the writer to sleep for about one period of playback time.
 *
 * The consumer is a user process on the same CPU, so this must NEVER spin
 * waiting for it: that starves the very process being waited on (and the
 * compositor). Waiting is done the way ProcessSleep does it in syscall context,
 * by sleeping through the scheduler. A full ring is the last resort: the
 * period is dropped and counted. */
#define PX_BYTES_PER_MS      192      /* 48 kHz * 2 channels * 2 bytes */
#define PX_TARGET_PERIODS    2        /* backlog the writer may keep queued */
#define PX_PACE_MAX_MS       80

static int px_write(uint8_t* buf, size_t len) {
	if (!buf || !len)
		return 1;
	AuAcquireSpinlock(&px_lock);
	if (!px.attached || !px.hdr || !px.data) {
		AuReleaseSpinlock(&px_lock);
		return 1;
	}
	px.inflight++;
	SndRingHdr* r = px.hdr;
	uint8_t* data = px.data;
	AuReleaseSpinlock(&px_lock);
	px_barrier();
	int rc = 1;
	uint32_t pace_ms = 0;
	px_cache_invalidate(r, sizeof(*r));
	uint32_t size = r->size;
	if (size >= 4096 && !(size & (size - 1))) {
		if (len > size)
			len = size;
		px.calls++;
		if ((uint32_t)(size - (r->head - r->tail)) < len) {
			r->dropped += (uint32_t)len;
			px.dropped += len;
			pace_ms = PX_PACE_MAX_MS;          /* ring full: we are far ahead of playback */
		} else {
			uint32_t head = r->head;
			uint32_t off = head & (size - 1);
			size_t first = len < (size - off) ? len : (size - off);
			memcpy(data + off, buf, first);
			if (first < len)
				memcpy(data, buf + first, len - first);
			px_cache_clean_ring_bytes(data, size, off, (uint32_t)len);
			px_barrier();
			r->head = head + (uint32_t)len;
			px_cache_clean(r, sizeof(*r));
			px.written += len;
			rc = 0;
			/* sleep in proportion to the backlog above the target, at the playback
			 * rate. Self-correcting: if sleeps come up short, the backlog grows
			 * and the next sleep gets longer. */
			uint32_t fill = r->head - r->tail;
			uint32_t target = PX_TARGET_PERIODS * r->period;
			if (fill > target) {
				pace_ms = (fill - target) / PX_BYTES_PER_MS;
				if (pace_ms < 1)
					pace_ms = 1;
				if (pace_ms > PX_PACE_MAX_MS)
					pace_ms = PX_PACE_MAX_MS;
			}
		}
	}
	px_barrier();
	AuAcquireSpinlock(&px_lock);
	px.inflight--;
	AuReleaseSpinlock(&px_lock);
	if (pace_ms) {
		AA64Thread* thr = AuGetCurrentThread();
		if (thr) {
			AuSleepThread(thr, pace_ms);
			AuScheduleThread(AA64GetCurrentRegCtx());
		}
	}
	return rc;
}

static int px_read(uint8_t* buf, size_t len) {
	(void)buf; (void)len;
	return 0;
}

static int px_start(void) {
	AuAcquireSpinlock(&px_lock);
	if (px.attached && px.hdr) {
		px.hdr->running = 1;
		px_cache_clean(px.hdr, sizeof(*px.hdr));
	}
	AuReleaseSpinlock(&px_lock);
	return 0;
}

static int px_stop(void) {
	AuAcquireSpinlock(&px_lock);
	if (px.attached && px.hdr) {
		px.hdr->running = 0;
		px_cache_clean(px.hdr, sizeof(*px.hdr));
	}
	AuReleaseSpinlock(&px_lock);
	return 0;
}

static int px_vol(uint8_t vol) {
	(void)vol;
	return 0;
}

static void px_detach(void) {
	AuAcquireSpinlock(&px_lock);
	if (!px.attached) {
		AuReleaseSpinlock(&px_lock);
		return;
	}
	px.attached = false;                 /* writers see this and bail out */
	AuReleaseSpinlock(&px_lock);
	px_barrier();
	AuSoundUnregisterCard(&px.card);
	/* drain writers that were mid-copy before the ring can be freed */
	uint64_t t0 = AuGetCurrentMS();
	bool warned = false;
	for (;;) {
		AuAcquireSpinlock(&px_lock);
		int inflight = px.inflight;
		AuReleaseSpinlock(&px_lock);
		if (inflight == 0)
			break;
		if (!warned && AuGetCurrentMS() - t0 >= PX_DETACH_WAIT_MS) {
			UARTDebugOut("[sndproxy]: %d writer(s) still inside after %d ms; waiting safely\r\n",
				inflight, PX_DETACH_WAIT_MS);
			warned = true;
		}
		__asm__ volatile("yield");
	}
	/* the ring's memory is still valid here (it is freed after this returns):
	 * copy out what the server had not yet consumed */
	if (px.hdr)
		px_cache_invalidate(px.hdr, sizeof(*px.hdr));
	if (px.carry) {
		kfree(px.carry);
		px.carry = NULL;
		px.carry_len = 0;
	}
	if (px.hdr && px.data) {
		uint32_t size = px.hdr->size;
		uint32_t pending = px.hdr->head - px.hdr->tail;
		if (pending > size)
			pending = size;
		if (pending) {
			uint8_t* c = (uint8_t*)kmalloc(pending);
			if (c) {
				uint32_t off = px.hdr->tail & (size - 1);
				uint32_t first = pending < (size - off) ? pending : (size - off);
				memcpy(c, px.data + off, first);
				if (first < pending)
					memcpy(c + first, px.data, pending - first);
				px.carry = c;
				px.carry_len = pending;
				px.carry_ms = AuGetCurrentMS();
			}
		}
	}
	AuAcquireSpinlock(&px_lock);
	px.hdr = NULL;
	px.data = NULL;
	px.owner = NULL;
	AuReleaseSpinlock(&px_lock);
	UARTDebugOut("[sndproxy]: detached (written %x bytes, dropped %x)\r\n", (uint32_t)px.written, (uint32_t)px.dropped);
}

void AuSndProxyOwnerGone(AuProcess* proc) {
	AuAcquireSpinlock(&px_lock);
	bool owned = px.attached && px.owner == proc;
	AuReleaseSpinlock(&px_lock);
	if (owned)
		px_detach();
}

static int px_ioctl(AuVFSNode* file, int code, void* arg) {
	(void)file;
	AuProcess* proc = AuHwdevCurrentProcess();
	if (!AuHwdevCallerAllowed(proc))
		return SNDSRV_E_PERM;

	switch (code) {
	case SNDSRV_IOC_ATTACH: {
		uint64_t a = (uint64_t)arg;
		if (!a || a + sizeof(SndsrvAttach) >= 0x0000800000000000ULL)
			return SNDSRV_E_INVAL;
		SndsrvAttach* req = (SndsrvAttach*)arg;
		AuAcquireSpinlock(&px_lock);
		bool busy = px.attached;
		AuReleaseSpinlock(&px_lock);
		if (busy)
			return SNDSRV_E_BUSY;
		if (req->ring_len < SNDSRV_DATA_OFFSET + 4096 || (req->ring_phys & 0xFFF))
			return SNDSRV_E_INVAL;
		/* never trust a user-supplied physical address: it must be the
		 * caller's own DMA buffer */
		if (!AuHwdevOwnsDma(proc, req->ring_phys, req->ring_len))
			return SNDSRV_E_PERM;
		SndRingHdr* hdr = (SndRingHdr*)P2V(req->ring_phys);
		px_cache_invalidate(hdr, sizeof(*hdr));
		uint32_t size = hdr->size;
		if (hdr->magic != SNDSRV_RING_MAGIC || size < 4096 || (size & (size - 1)) ||
			(uint64_t)size + SNDSRV_DATA_OFFSET > req->ring_len)
			return SNDSRV_E_INVAL;

		hdr->head = 0;
		hdr->tail = 0;
		hdr->dropped = 0;
		hdr->running = 0;
		px_cache_clean(hdr, sizeof(*hdr));
		/* replay audio left over from a server that died */
		if (px.carry && AuGetCurrentMS() - px.carry_ms > PX_CARRY_MAX_AGE_MS) {
			UARTDebugOut("[sndproxy]: discarded %d bytes of stale carried audio\r\n", (int)px.carry_len);
			kfree(px.carry);
			px.carry = NULL;
			px.carry_len = 0;
		}
		if (px.carry) {
			uint32_t n = px.carry_len < size ? px.carry_len : size;
			memcpy((uint8_t*)hdr + SNDSRV_DATA_OFFSET, px.carry, n);
			px_cache_clean((uint8_t*)hdr + SNDSRV_DATA_OFFSET, n);
			hdr->head = n;
			px_cache_clean(hdr, sizeof(*hdr));
			UARTDebugOut("[sndproxy]: carried %d bytes over from the previous server\r\n", (int)n);
			kfree(px.carry);
			px.carry = NULL;
			px.carry_len = 0;
		}
		memset(&px.card, 0, sizeof(px.card));
		strcpy(px.card.name, "virtio-sound");     /* same name the in-kernel driver used */
		px.card.write = px_write;
		px.card.read = px_read;
		px.card.start_output = px_start;
		px.card.stop_output = px_stop;
		px.card.set_vol = px_vol;
		px.card._force_write = 1;
		AuAcquireSpinlock(&px_lock);
		if (px.attached) {
			AuReleaseSpinlock(&px_lock);
			return SNDSRV_E_BUSY;
		}
		px.hdr = hdr;
		px.data = (uint8_t*)hdr + SNDSRV_DATA_OFFSET;
		px.owner = proc;
		px.written = px.dropped = px.calls = 0;
		px.inflight = 0;
		px.attached = true;
		AuReleaseSpinlock(&px_lock);
		px_barrier();
		if (AuSoundRegisterCard(&px.card) != 0) {
			AuAcquireSpinlock(&px_lock);
			px.attached = false;
			px.hdr = NULL;
			px.data = NULL;
			px.owner = NULL;
			AuReleaseSpinlock(&px_lock);
			return SNDSRV_E_INVAL;
		}
		UARTDebugOut("[sndproxy]: attached ring %x size %x for pid %d, card registered\r\n",
					 (uint32_t)req->ring_phys, size, proc->proc_id);
		return SNDSRV_OK;
	}
	case SNDSRV_IOC_DETACH:
		AuAcquireSpinlock(&px_lock);
		bool owned = px.attached && px.owner == proc;
		AuReleaseSpinlock(&px_lock);
		if (owned) {
			px_detach();
			return SNDSRV_OK;
		}
		return SNDSRV_E_INVAL;
	case SNDSRV_IOC_STATS: {
		uint64_t a = (uint64_t)arg;
		if (!a || a + sizeof(SndsrvStats) >= 0x0000800000000000ULL)
			return SNDSRV_E_INVAL;
		SndsrvStats* st = (SndsrvStats*)arg;
		st->written = px.written;
		st->dropped = px.dropped;
		st->calls = px.calls;
		return SNDSRV_OK;
	}
	}
	return SNDSRV_E_INVAL;
}

void AuSndProxyInitialize(void) {
	AuVFSNode* fsys = AuVFSFind("/dev");
	if (!fsys)
		return;
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "sndsrv");
	node->flags = FS_FLAG_DEVICE;
	node->uid = 0;
	node->gid = AuCredGetGroupID(AURORA_GID_MISC_DAEMONS);
	node->device = fsys;
	node->iocontrol = px_ioctl;
	AuDevFSAddFile(fsys, "/", node);
	UARTDebugOut("[sndproxy]: /dev/sndsrv registered\r\n");
}
