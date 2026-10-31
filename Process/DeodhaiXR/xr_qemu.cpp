#include "../../Libs/OpenXR/openxr.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arm_neon.h>

#define MAX_SC 4
#define SC_IMAGES 2

struct QemuSc {
	int used;
	uint32_t w, h;
	uint32_t* img[SC_IMAGES];
	uint32_t next;
	int acquired;
};

static XrInstance g_inst;
static XrSession g_sess;
static XrSpace g_space;
static int g_running;
static XrTime g_time = 1;
static QemuSc g_sc[MAX_SC];
static uint32_t* g_scan;
static int g_scan_w, g_scan_h, g_scan_stride;

void xrQemuSetScanout(uint32_t* pixels, int width, int height, int stride_pixels) {
	g_scan = pixels;
	g_scan_w = width;
	g_scan_h = height;
	g_scan_stride = stride_pixels;
}

/* EndFrame transports the live flat desktop at full scanout resolution. The
 * host OpenXR viewer presents that panel independently to both eyes; squeezing
 * it into SBS halves destroyed every other horizontal text sample. --axiss */
static const uint32_t* g_canvas;
static int g_canvas_stride;
static int g_shift_l;
static int g_shift_r;
#define MAX_DAMAGE_RECTS 100
static XrRect2Di g_damage[MAX_DAMAGE_RECTS];
static uint32_t g_damage_count;

void xrQemuSetCanvasSrc(const uint32_t* pixels, int stride_pixels, int shift_l, int shift_r) {
	g_canvas = pixels;
	g_canvas_stride = stride_pixels;
	g_shift_l = shift_l;
	g_shift_r = shift_r;
}

void xrQemuSetCanvasDamage(const XrRect2Di* rects, uint32_t count) {
	if (!rects)
		count = 0;
	if (count > MAX_DAMAGE_RECTS)
		count = MAX_DAMAGE_RECTS;
	for (uint32_t i = 0; i < count; i++)
		g_damage[i] = rects[i];
	g_damage_count = count;
}

static inline uint32_t avg_pair(uint32_t a, uint32_t b) {
	/* Average all four bytes independently. Masking the low bit before the
	 * shift prevents carries from crossing ARGB channel boundaries. */
	return (a & b) + (((a ^ b) & 0xFEFEFEFEu) >> 1);
}

/* Squeeze one canvas row into one scanout half-row (exact 2:1). shift is the
 * per-eye parallax in source pixels; out-of-range samples clamp. */
static void squeeze_half_row(uint32_t* dst, const uint32_t* srow, int src_w, int shift,
							 int x0, int x1) {
	int count = src_w / 2;
	if (x0 < 0)
		x0 = 0;
	if (x1 > count)
		x1 = count;
	int x = x0;
	/* The parallax shift only affects a handful of edge pixels. Clamp those
	 * scalarly, then average four pairs at once across the row. */
	for (; x < x1 && x * 2 + shift < 0; x++) {
		int sx0 = x * 2 + shift;
		int sx1 = sx0 + 1;
		if (sx0 < 0)
			sx0 = 0;
		if (sx1 < 0)
			sx1 = 0;
		if (sx1 >= src_w)
			sx1 = src_w - 1;
		if (sx0 >= src_w)
			sx0 = src_w - 1;
		if (sx1 == sx0)
			dst[x] = srow[sx0];
		else
			dst[x] = avg_pair(srow[sx0], srow[sx1]);
	}
	for (; x + 4 <= x1; x += 4) {
		int sx = x * 2 + shift;
		if (sx < 0 || sx + 7 >= src_w)
			break;
		uint32x4x2_t pairs = vld2q_u32(srow + sx);
		uint8x16_t avg = vhaddq_u8(vreinterpretq_u8_u32(pairs.val[0]),
									  vreinterpretq_u8_u32(pairs.val[1]));
		vst1q_u32(dst + x, vreinterpretq_u32_u8(avg));
	}
	for (; x < x1; x++) {
		int sx0 = x * 2 + shift;
		int sx1 = sx0 + 1;
		if (sx0 < 0)
			sx0 = 0;
		if (sx1 < 0)
			sx1 = 0;
		if (sx1 >= src_w)
			sx1 = src_w - 1;
		if (sx0 >= src_w)
			sx0 = src_w - 1;
		if (sx1 == sx0)
			dst[x] = srow[sx0];
		else
			dst[x] = avg_pair(srow[sx0], srow[sx1]);
	}
}

static void source_damage_span(int src_x, int src_width, int shift, int* x0, int* x1) {
	/* Expand by one output pixel on either side. This is conservative around
	 * odd parallax shifts and keeps the transform arithmetic branch-free. */
	int left = (src_x - shift) / 2 - 1;
	int right = (src_x + src_width - shift + 1) / 2 + 1;
	int count = g_scan_w / 2;
	if (src_x <= 0)
		left = 0;
	if (src_x + src_width >= g_scan_w)
		right = count;
	if (left < 0)
		left = 0;
	if (left > count)
		left = count;
	if (right < 0)
		right = 0;
	if (right > count)
		right = count;
	if (right < left)
		right = left;
	*x0 = left;
	*x1 = right;
}

static QemuSc* sc_from(XrSwapchain h) {
	uint32_t i = (uint32_t)h;
	if (i == 0 || i > MAX_SC)
		return NULL;
	if (!g_sc[i - 1].used)
		return NULL;
	return &g_sc[i - 1];
}

XrResult xrCreateInstance(const XrInstanceCreateInfo* info, XrInstance* instance) {
	if (!info || !instance)
		return XR_ERROR_VALIDATION_FAILURE;
	g_inst = 1;
	*instance = g_inst;
	return XR_SUCCESS;
}

XrResult xrDestroyInstance(XrInstance instance) {
	(void)instance;
	g_inst = 0;
	return XR_SUCCESS;
}

XrResult xrGetSystem(XrInstance instance, const XrSystemGetInfo* info, XrSystemId* systemId) {
	if (!g_inst || instance != g_inst || !info || !systemId)
		return XR_ERROR_HANDLE_INVALID;
	*systemId = 1;
	return XR_SUCCESS;
}

XrResult xrCreateSession(XrInstance instance, const XrSessionCreateInfo* info, XrSession* session) {
	if (!info || !session || instance != g_inst)
		return XR_ERROR_HANDLE_INVALID;
	g_sess = 1;
	*session = g_sess;
	return XR_SUCCESS;
}

XrResult xrDestroySession(XrSession session) {
	(void)session;
	g_sess = 0;
	g_running = 0;
	return XR_SUCCESS;
}

XrResult xrBeginSession(XrSession session, const XrSessionBeginInfo* info) {
	(void)info;
	if (session != g_sess)
		return XR_ERROR_HANDLE_INVALID;
	g_running = 1;
	return XR_SUCCESS;
}

XrResult xrEndSession(XrSession session) {
	if (session != g_sess)
		return XR_ERROR_HANDLE_INVALID;
	g_running = 0;
	return XR_SUCCESS;
}

XrResult xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* info,
								XrSpace* space) {
	(void)info;
	if (session != g_sess || !space)
		return XR_ERROR_HANDLE_INVALID;
	g_space = 1;
	*space = g_space;
	return XR_SUCCESS;
}

XrResult xrDestroySpace(XrSpace space) {
	(void)space;
	g_space = 0;
	return XR_SUCCESS;
}

XrResult xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* info,
						   XrSwapchain* swapchain) {
	if (session != g_sess || !info || !swapchain || info->width == 0 || info->height == 0)
		return XR_ERROR_VALIDATION_FAILURE;
	int slot = -1;
	for (int i = 0; i < MAX_SC; i++) {
		if (!g_sc[i].used) {
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return XR_ERROR_RUNTIME_FAILURE;
	QemuSc* s = &g_sc[slot];
	memset(s, 0, sizeof(*s));
	s->used = 1;
	s->w = info->width;
	s->h = info->height;
	size_t bytes = (size_t)s->w * (size_t)s->h * 4;
	for (int i = 0; i < SC_IMAGES; i++) {
		s->img[i] = (uint32_t*)malloc(bytes);
		if (!s->img[i])
			return XR_ERROR_RUNTIME_FAILURE;
		memset(s->img[i], 0, bytes);
	}
	*swapchain = (XrSwapchain)(slot + 1);
	return XR_SUCCESS;
}

XrResult xrDestroySwapchain(XrSwapchain swapchain) {
	QemuSc* s = sc_from(swapchain);
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	for (int i = 0; i < SC_IMAGES; i++) {
		free(s->img[i]);
		s->img[i] = NULL;
	}
	s->used = 0;
	return XR_SUCCESS;
}

XrResult xrEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t count, uint32_t* written,
									XrSwapchainImageBaseHeader* images) {
	QemuSc* s = sc_from(swapchain);
	if (!s || !written)
		return XR_ERROR_HANDLE_INVALID;
	*written = SC_IMAGES;
	if (count == 0)
		return XR_SUCCESS;
	if (count < SC_IMAGES)
		return XR_ERROR_VALIDATION_FAILURE;
	XrSwapchainImageCpuXeneva* cpu = (XrSwapchainImageCpuXeneva*)images;
	for (uint32_t i = 0; i < SC_IMAGES; i++) {
		cpu[i].type = XR_TYPE_SWAPCHAIN_IMAGE_CPU_XENEVA;
		cpu[i].next = NULL;
		cpu[i].rgba = s->img[i];
		cpu[i].width = s->w;
		cpu[i].height = s->h;
	}
	return XR_SUCCESS;
}

XrResult xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo* info,
								 uint32_t* index) {
	(void)info;
	QemuSc* s = sc_from(swapchain);
	if (!s || !index)
		return XR_ERROR_HANDLE_INVALID;
	uint32_t acquired = s->next;
	s->next = (s->next + 1) % SC_IMAGES;
	s->acquired = 1;
	*index = acquired;
	return XR_SUCCESS;
}

XrResult xrWaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo* info) {
	(void)info;
	QemuSc* s = sc_from(swapchain);
	if (!s || !s->acquired)
		return XR_ERROR_HANDLE_INVALID;
	return XR_SUCCESS;
}

XrResult xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo* info) {
	(void)info;
	QemuSc* s = sc_from(swapchain);
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	s->acquired = 0;
	return XR_SUCCESS;
}

XrResult xrWaitFrame(XrSession session, const XrFrameWaitInfo* info, XrFrameState* state) {
	(void)info;
	if (session != g_sess || !g_running || !state)
		return XR_ERROR_SESSION_NOT_RUNNING;
	g_time += 11111111; /* ~90 Hz in ns */
	state->type = XR_TYPE_FRAME_STATE;
	state->next = NULL;
	state->predictedDisplayTime = g_time;
	state->predictedDisplayPeriod = 11111111;
	state->shouldRender = 1;
	return XR_SUCCESS;
}

XrResult xrBeginFrame(XrSession session, const XrFrameBeginInfo* info) {
	(void)info;
	if (session != g_sess || !g_running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	return XR_SUCCESS;
}

XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* info) {
	if (session != g_sess || !g_running || !info)
		return XR_ERROR_SESSION_NOT_RUNNING;
	if (!g_scan)
		return XR_SUCCESS;
	/* This runtime presents the compositor canvas, not the compatibility
	 * swapchain buffers returned to OpenXR callers. --axiss */
	if (g_canvas && g_scan_w > 0) {
		for (uint32_t i = 0; i < g_damage_count; i++) {
			int x0 = g_damage[i].offset.x;
			int x1 = x0 + g_damage[i].extent.width;
			int y0 = g_damage[i].offset.y;
			int y1 = y0 + g_damage[i].extent.height;
			if (x0 < 0)
				x0 = 0;
			if (x1 > g_scan_w)
				x1 = g_scan_w;
			if (y0 < 0)
				y0 = 0;
			if (y1 > g_scan_h)
				y1 = g_scan_h;
			if (x1 <= x0 || y1 <= y0)
				continue;
			for (int y = y0; y < y1; y++) {
				const uint32_t* srow = g_canvas + (size_t)y * g_canvas_stride;
				uint32_t* drow = g_scan + (size_t)y * g_scan_stride;
				for (int x = x0; x < x1; x++)
					drow[x] = srow[x];
			}
		}
	}
	return XR_SUCCESS;
}

XrResult xrLocateViews(XrSession session, const XrViewLocateInfo* info, XrViewState* state,
					   uint32_t viewCapacity, uint32_t* viewCount, XrView* views) {
	(void)info;
	if (session != g_sess || !viewCount)
		return XR_ERROR_HANDLE_INVALID;
	*viewCount = 2;
	if (state) {
		state->type = XR_TYPE_VIEW_STATE;
		state->viewStateFlags = 3;
	}
	if (viewCapacity < 2)
		return XR_SUCCESS;
	const float ipd = 0.063f;
	const XrFovf fov = {-0.8f, 0.8f, 0.8f, -0.8f};
	for (int e = 0; e < 2; e++) {
		views[e].type = XR_TYPE_VIEW;
		views[e].next = NULL;
		views[e].pose.orientation.x = 0;
		views[e].pose.orientation.y = 0;
		views[e].pose.orientation.z = 0;
		views[e].pose.orientation.w = 1;
		views[e].pose.position.x = (e == 0) ? -ipd * 0.5f : ipd * 0.5f;
		views[e].pose.position.y = 0;
		views[e].pose.position.z = 0;
		views[e].fov = fov;
	}
	return XR_SUCCESS;
}
