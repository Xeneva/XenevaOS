/**
 * devgpu.c -- thin kernel proxy for the user-space virtio-GPU server.
 *
 * The server owns the PCI function through /dev/hwdev.  This node keeps the
 * compositor-facing framebuffer stable and performs the one safety-critical
 * bridge operation: copying a bounded damage rectangle into DMA memory that
 * belongs to the server's exact GPU capability.  The kernel never submits a
 * virtio command and never waits for the server.
 */
#include <Fs/Dev/devgpu.h>
#include <Fs/Dev/devfs.h>
#include <Fs/Dev/devhw.h>
#include <Fs/vfs.h>
#include <Cred/group.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Drivers/uart.h>
#include <aucon.h>
#include <gpusrv.h>
#include <gpudamage.h>
#include <proctoken.h>
#include <string.h>

/* aucon.c owns this object and keeps its buffer mapping stable for /dev/graph. */
extern AuConsole* aucon;

static AuProcess* gpu_owner;
static void* gpu_hw_device;
static GpuSrvBacking gpu_backing[GPUSRV_MAX_BACKING];
static uint32_t gpu_backing_count;
static uint64_t gpu_backing_bytes;
static uint32_t gpu_width;
static uint32_t gpu_height;
static uint32_t gpu_pitch;
static uint32_t gpu_resource;
static uint64_t gpu_generation;
static uint64_t gpu_submitted;
static uint64_t gpu_completed;
static GpuDamage gpu_damage;
static bool gpu_attached;
static bool gpu_ready;

/* Local fixed-size uaccess, not DCL's raw memcpy copy_*_user. Callers keep
 * IRQs masked from lookup through copying/publication on this single-CPU
 * path. This does not pin pages against another CPU or recover bus faults.
 * Reject blocks, device memory, and hierarchical/leaf permission failures. */
#define GPU_PHYS_MASK 0x0000FFFFFFFFF000ULL
static void* gpu_user_page(AuProcess* proc, uint64_t address, bool write) {
	uint64_t* table = proc->cr3; /* process roots are direct-mapped */
	for (unsigned shift = 39; shift > 12; shift -= 9) {
		uint64_t desc = table[(address >> shift) & 511];
		if ((desc & (PTE_VALID | PTE_TABLE)) != (PTE_VALID | PTE_TABLE) ||
			(desc & (1ULL << 61)) || (write && (desc & (1ULL << 62))) ||
			!(desc & GPU_PHYS_MASK))
			return 0;
		table = (uint64_t*)P2V(desc & GPU_PHYS_MASK);
	}
	uint64_t desc = table[(address >> 12) & 511];
	uint64_t required = PTE_VALID | PTE_TABLE | PTE_AF | PTE_AP_RW_USER;
	uint64_t attr = desc & (7ULL << 2);
	if ((desc & required) != required || (write && (desc & (1ULL << 7))) ||
		(attr != PTE_NORMAL_MEM && attr != PTE_NORMAL_NON_CACHEABLE) ||
		!(desc & GPU_PHYS_MASK))
		return 0;
	return (void*)P2V((desc & GPU_PHYS_MASK) + (address & PAGE_MASK));
}

static bool gpu_user_copy(AuProcess* proc, void* user, void* local,
		size_t len, bool write) {
	uint64_t address = (uint64_t)user;
	const uint64_t limit = 0x0000800000000000ULL;
	/* Every ABI buffer is at most one page, hence at most two spans even
	 * when unaligned. Validate both before modifying either destination. */
	if (!proc || !proc->cr3 || !local || !len || len > PAGE_SIZE ||
		!address || address >= limit || len > limit - address)
		return false;
	void* pages[2];
	size_t sizes[2];
	size_t remaining = len;
	unsigned count = 0;
	while (remaining) {
		size_t n = PAGE_SIZE - (address & PAGE_MASK);
		if (n > remaining) n = remaining;
		pages[count] = gpu_user_page(proc, address, write);
		if (!pages[count])
			return false;
		sizes[count++] = n;
		remaining -= n;
		address += n;
	}
	uint8_t* bytes = (uint8_t*)local;
	for (unsigned i = 0; i < count; i++) {
		if (write) memcpy(pages[i], bytes, sizes[i]);
		else memcpy(bytes, pages[i], sizes[i]);
		bytes += sizes[i];
	}
	return true;
}

/* Check the last visible byte, not pitch * height: final-row padding need
 * not exist. Division/subtraction bounds every multiply/add before use. */
static bool gpu_extent(uint32_t pitch, uint32_t x, uint32_t y,
		uint32_t width, uint32_t height, uint64_t capacity, uint64_t* offset) {
	uint64_t row_bytes = (uint64_t)width * 4;
	uint64_t x_bytes = (uint64_t)x * 4;
	uint64_t last_row = (uint64_t)y + height - 1;
	if (!pitch || !width || !height || x_bytes > pitch ||
		row_bytes > pitch - x_bytes || last_row > capacity / pitch)
		return false;
	uint64_t last = last_row * pitch;
	if (x_bytes > capacity - last || row_bytes > capacity - last - x_bytes)
		return false;
	*offset = (uint64_t)y * pitch + x_bytes;
	return true;
}

static bool gpu_source(AuConsole* source) {
	if (!aucon)
		return false;
	*source = *aucon;
	uint64_t offset;
	return source->buffer && source->bpp == 32 && source->width && source->height &&
		source->width <= GPUSRV_MAX_DIM && source->height <= GPUSRV_MAX_DIM &&
		(uint64_t)source->buffer <= UINT64_MAX - source->size &&
		gpu_extent(source->pitch, 0, 0, source->width, source->height,
			source->size, &offset);
}

static bool gpu_daemon(AuProcess* proc) {
	return AuHwdevCallerAllowed(proc);
}

static bool gpu_display_owner(AuProcess* proc) {
	return proc && AuProcessTokenGetProc(PROCESS_TOKEN_DISPLAY) == proc;
}

static bool gpu_copy_into_backing(uint64_t offset, const void* src, uint64_t len) {
	if (!src || !len || offset > gpu_backing_bytes || len > gpu_backing_bytes - offset)
		return false;

	uint64_t cursor = 0;
	uint64_t remaining = len;
	const uint8_t* input = (const uint8_t*)src;
	for (uint32_t i = 0; i < gpu_backing_count && remaining; i++) {
		if (gpu_backing[i].length > gpu_backing_bytes - cursor)
			return false;
		uint64_t entry_end = cursor + gpu_backing[i].length;
		if (offset >= entry_end) {
			cursor = entry_end;
			continue;
		}
		uint64_t within = offset > cursor ? offset - cursor : 0;
		uint64_t available = gpu_backing[i].length - within;
		uint64_t take = available < remaining ? available : remaining;
		void* dst = (void*)P2V(gpu_backing[i].phys + within);
		memcpy(dst, input, (size_t)take);
		aa64_data_cache_clean_range(dst, (size_t)take);
		input += take;
		remaining -= take;
		offset += take;
		cursor = entry_end;
	}
	return remaining == 0;
}

static bool gpu_copy_rect(const GpuSrvFrame* frame) {
	AuConsole source;
	if (!frame || !gpu_source(&source) || source.width != gpu_width ||
		source.height != gpu_height || !gpu_pitch)
		return false;
	if (!frame->width || !frame->height || frame->x >= gpu_width ||
		frame->y >= gpu_height || frame->width > gpu_width - frame->x ||
		frame->height > gpu_height - frame->y)
		return false;

	uint64_t src_offset, dst_offset;
	if (!gpu_extent(source.pitch, frame->x, frame->y, frame->width,
		frame->height, source.size, &src_offset) ||
		!gpu_extent(gpu_pitch, frame->x, frame->y, frame->width,
		frame->height, gpu_backing_bytes, &dst_offset))
		return false;
	uint64_t row_bytes = (uint64_t)frame->width * 4;
	for (uint32_t row = 0; row < frame->height; row++) {
		const uint8_t* src = (const uint8_t*)source.buffer +
			src_offset + (uint64_t)row * source.pitch;
		if (!gpu_copy_into_backing(dst_offset + (uint64_t)row * gpu_pitch, src, row_bytes))
			return false;
	}
	return true;
}

static int gpu_acquire(AuProcess* proc, GpuSrvAcquire* req, void* output) {
	if (!gpu_daemon(proc))
		return GPUSRV_E_PERM;
	if (gpu_attached)
		return GPUSRV_E_BUSY;
	if (!req || req->version != GPUSRV_VERSION || req->backing_count == 0 ||
		req->backing_count > GPUSRV_MAX_BACKING)
		return GPUSRV_E_INVAL;
	if (!AuHwdevHandleIsDevice(proc, req->hw_handle, GPUSRV_VENDOR, GPUSRV_DEVICE))
		return GPUSRV_E_PERM;

	uint32_t width = req->width;
	uint32_t height = req->height;
	AuConsole source;
	if (!gpu_source(&source) || width != source.width || height != source.height)
		return GPUSRV_E_INVAL;
	uint64_t bytes = (uint64_t)width * height * 4;
	if (!bytes || bytes > (uint64_t)GPUSRV_MAX_BACKING * GPUSRV_MAX_BACKING_BYTES)
		return GPUSRV_E_INVAL;

	GpuSrvBacking local[GPUSRV_MAX_BACKING] = {0};
	uint64_t total = 0;
	for (uint32_t i = 0; i < req->backing_count; i++) {
		local[i] = req->backing[i];
		if (!local[i].phys || !local[i].length ||
			local[i].length > GPUSRV_MAX_BACKING_BYTES ||
			(local[i].phys & 4095) ||
			local[i].phys > UINT64_MAX - local[i].length ||
			!AuHwdevHandleOwnsDma(proc, req->hw_handle, local[i].phys,
				local[i].length) || local[i].length > UINT64_MAX - total)
			return GPUSRV_E_INVAL;
		total += local[i].length;
	}
	if (total < bytes)
		return GPUSRV_E_INVAL;

	void* hw_device = AuHwdevHandleObject(proc, req->hw_handle);
	if (!hw_device)
		return GPUSRV_E_PERM;
	uint64_t generation = gpu_generation + 1;
	if (!generation) generation = 1;
	req->pitch = width * 4;
	req->resource = GPUSRV_RESOURCE;
	req->backing_bytes = total;
	req->generation = generation;
	/* No attachment is visible until the complete reply has been copied. */
	if (!gpu_user_copy(proc, output, req, sizeof(*req), true))
		return GPUSRV_E_INVAL;

	gpu_owner = proc;
	gpu_hw_device = hw_device;
	gpu_backing_count = req->backing_count;
	memcpy(gpu_backing, local, sizeof(local));
	gpu_backing_bytes = total;
	gpu_width = width;
	gpu_height = height;
	gpu_pitch = width * 4;
	gpu_resource = GPUSRV_RESOURCE;
	gpu_generation = generation;
	gpu_submitted = 0;
	gpu_completed = 0;
	memset(&gpu_damage, 0, sizeof gpu_damage);
	gpu_damage.width = width;
	gpu_damage.height = height;
	GpuDamageReset(&gpu_damage);
	gpu_attached = true;
	gpu_ready = true;

	UARTDebugOut("[gpuproxy]: gpu acquired by pid %d (%ux%u, %u backing pages)\r\n",
		proc->proc_id, gpu_width, gpu_height, gpu_backing_count);
	return GPUSRV_OK;
}

static int gpu_take(AuProcess* proc, GpuSrvFrame* frame) {
	if (proc != gpu_owner || !gpu_attached || !gpu_ready)
		return GPUSRV_E_PERM;
	GpuSrvFrame local;
	int rc = GpuDamageTake(&gpu_damage, &local);
	if (rc != GPUSRV_OK)
		return rc;
	if (!gpu_copy_rect(&local))
		rc = GPUSRV_E_UNAVAILABLE;
	else if (!gpu_user_copy(proc, frame, &local, sizeof(local), true))
		rc = GPUSRV_E_INVAL;
	if (rc != GPUSRV_OK) {
		/* The server never received a usable frame. Release the slot and
		 * preserve its damage for a retry; this is not a completed frame. */
		(void)GpuDamageAck(&gpu_damage, local.sequence);
		(void)GpuDamageAdd(&gpu_damage, local.x, local.y, local.width, local.height);
		return rc;
	}
	gpu_submitted++;
	return GPUSRV_OK;
}

static int gpu_ioctl_locked(AuProcess* proc, int code, void* arg) {
	switch (code) {
	case GPUSRV_IOC_ACQUIRE: {
		if (!gpu_daemon(proc))
			return GPUSRV_E_PERM;
		GpuSrvAcquire req;
		if (!gpu_user_copy(proc, arg, &req, sizeof(req), false))
			return GPUSRV_E_INVAL;
		return gpu_acquire(proc, &req, arg);
	}
	case GPUSRV_IOC_DAMAGE:
	case GPUSRV_IOC_FLUSH: {
		if (!gpu_display_owner(proc))
			return GPUSRV_E_PERM;
		GpuSrvDamage damage;
		if (!gpu_user_copy(proc, arg, &damage, sizeof(damage), false))
			return GPUSRV_E_INVAL;
		return GpuDamageAdd(&gpu_damage, damage.x, damage.y, damage.width, damage.height);
	}
	case GPUSRV_IOC_TAKE:
		return gpu_take(proc, (GpuSrvFrame*)arg);
	case GPUSRV_IOC_ACK: {
		if (!proc || proc != gpu_owner)
			return GPUSRV_E_PERM;
		GpuSrvAck ack;
		if (!gpu_user_copy(proc, arg, &ack, sizeof(ack), false))
			return GPUSRV_E_INVAL;
		int rc = GpuDamageAck(&gpu_damage, ack.sequence);
		if (rc == GPUSRV_OK)
			gpu_completed++;
		return rc;
	}
	case GPUSRV_IOC_STATUS: {
		if (!proc || (proc != gpu_owner && !gpu_display_owner(proc)))
			return GPUSRV_E_PERM;
		GpuSrvStatus status = {0};
		status.version = GPUSRV_VERSION;
		status.attached = gpu_attached ? 1 : 0;
		status.ready = gpu_ready ? 1 : 0;
		status.width = gpu_width;
		status.height = gpu_height;
		status.pitch = gpu_pitch;
		status.resource = gpu_resource;
		status.submitted = gpu_submitted;
		status.completed = gpu_completed;
		status.generation = gpu_generation;
		return gpu_user_copy(proc, arg, &status, sizeof(status), true) ?
			GPUSRV_OK : GPUSRV_E_INVAL;
	}
	case GPUSRV_IOC_RESOURCE:
		if (!proc || (proc != gpu_owner && !gpu_display_owner(proc)))
			return GPUSRV_E_PERM;
		return gpu_resource ? (int)gpu_resource : GPUSRV_E_UNAVAILABLE;
	default:
		return GPUSRV_E_INVAL;
	}
}

static int gpu_ioctl(AuVFSNode* file, int code, void* arg) {
	(void)file;
	uint64_t daif = read_daif();
	mask_irqs();
	/* Non-blocking single-CPU transaction, including source/DMA lifetimes,
	 * user-page validation, and copyout-before-publication. Not an SMP lock. */
	int rc = gpu_ioctl_locked(AuHwdevCurrentProcess(), code, arg);
	restore_daif(daif);
	return rc;
}

void AuGpuProxyDamage(int x, int y, int width, int height) {
	uint64_t daif = read_daif();
	mask_irqs();
	if (gpu_attached && gpu_ready && width > 0 && height > 0)
		(void)GpuDamageAdd(&gpu_damage, (uint64_t)(x < 0 ? 0 : x),
			(uint64_t)(y < 0 ? 0 : y), (uint64_t)width, (uint64_t)height);
	restore_daif(daif);
}

void AuGpuProxyOwnerGone(AuProcess* proc, void* hw_device) {
	uint64_t daif = read_daif();
	mask_irqs();
	if (proc != gpu_owner || hw_device != gpu_hw_device) {
		restore_daif(daif);
		return;
	}
	/* Called after bus mastering is stopped and before the hwdev DMA pages are
	 * released. No kernel consumer retains a pointer into the server buffers. */
	gpu_owner = 0;
	gpu_hw_device = 0;
	gpu_backing_count = 0;
	gpu_backing_bytes = 0;
	gpu_attached = false;
	gpu_ready = false;
	gpu_width = gpu_height = gpu_pitch = gpu_resource = 0;
	memset(gpu_backing, 0, sizeof gpu_backing);
	memset(&gpu_damage, 0, sizeof gpu_damage);
	restore_daif(daif);
	UARTDebugOut("[gpuproxy]: user GPU server detached\r\n");
}

void AuGpuProxyInitialize(void) {
	AuVFSNode* fsys = AuVFSFind("/dev");
	AuVFSNode* node;
	if (!fsys)
		return;

	node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	if (!node)
		return;
	memset(node, 0, sizeof(*node));
	strcpy(node->filename, "gpusrv");
	node->flags = FS_FLAG_DEVICE;
	node->uid = 0;
	/* The compositor submits bounded damage, while only the daemon group can
	 * acquire/submit hardware. The ioctl handler enforces that split. */
	node->gid = AuCredGetGroupID(AURORA_GID_MISC_WORLD);
	node->device = fsys;
	node->iocontrol = gpu_ioctl;
	AuDevFSAddFile(fsys, "/", node);
	UARTDebugOut("[gpuproxy]: /dev/gpusrv registered\r\n");
}
