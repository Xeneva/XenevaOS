/* Pure bounded damage-state helpers, also compiled by the host regression test.
 * Callers serialize access (AArch64 device ioctls run with IRQs masked).
 */
#ifndef __GPU_DAMAGE_H__
#define __GPU_DAMAGE_H__
#include <stdint.h>
#include <gpusrv.h>

typedef struct {
	uint32_t width, height;
	uint32_t left, top, right, bottom, dirty, busy;
	uint64_t next, pending;
} GpuDamage;

static inline int GpuDamageAdd(GpuDamage* d, uint64_t x, uint64_t y,
		uint64_t w, uint64_t h) {
	if (!w || !h || x >= d->width || y >= d->height ||
		w > d->width - x || h > d->height - y)
		return GPUSRV_E_INVAL;
	uint32_t r = (uint32_t)(x + w), b = (uint32_t)(y + h);
	if (!d->dirty) {
		d->left = (uint32_t)x; d->top = (uint32_t)y;
		d->right = r; d->bottom = b; d->dirty = 1;
	} else {
		if (x < d->left) d->left = (uint32_t)x;
		if (y < d->top) d->top = (uint32_t)y;
		if (r > d->right) d->right = r;
		if (b > d->bottom) d->bottom = b;
	}
	return GPUSRV_OK;
}

static inline int GpuDamageTake(GpuDamage* d, GpuSrvFrame* f) {
	if (d->busy) return GPUSRV_E_BUSY;
	if (!d->dirty) return GPUSRV_E_EMPTY;
	if (++d->next == 0) ++d->next;
	d->pending = d->next; d->busy = 1; d->dirty = 0;
	f->sequence = d->pending;
	f->x = d->left; f->y = d->top;
	f->width = d->right - d->left; f->height = d->bottom - d->top;
	return GPUSRV_OK;
}

static inline int GpuDamageAck(GpuDamage* d, uint64_t sequence) {
	if (!d->busy || sequence != d->pending) return GPUSRV_E_INVAL;
	d->busy = 0;
	return GPUSRV_OK;
}

static inline void GpuDamageReset(GpuDamage* d) {
	/* Do not reset next: an old generation's ACK must never match a new frame. */
	d->busy = 0;
	(void)GpuDamageAdd(d, 0, 0, d->width, d->height);
}
#endif
