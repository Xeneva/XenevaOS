#ifdef __XENEVA_OPENXR__
#include "xr_present.h"
#include "dirty.h"
#include "../../Libs/OpenXR/openxr.h"
#include <string.h>
#include <stdlib.h>
#include <sys/_keproc.h>

static XrInstance g_instance;
static XrSession g_session;
static XrSpace g_space;
static XrSwapchain g_eye[2];
static XrSwapchainImageCpuXeneva g_imgs[2][2];
static int g_ready;
static int g_w, g_h;

static void output_damage_span(int src_x, int src_width, int shift, int* x0, int* x1) {
	int left = (src_x - shift) / 2 - 1;
	int right = (src_x + src_width - shift + 1) / 2 + 1;
	int count = g_w / 2;
	if (src_x <= 0)
		left = 0;
	if (src_x + src_width >= g_w)
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

void XrPresentInit(ChCanvas* canv) {
	if (!canv || !canv->buffer)
		return;
	g_w = (int)canv->canvasWidth;
	g_h = (int)canv->canvasHeight;
	if (g_w < 8 || g_h < 8)
		return;

	uint32_t* scan = canv->framebuff ? canv->framebuff : canv->buffer;
	int stride = canv->pitch ? (int)(canv->pitch / 4) : g_w;
	if (stride < g_w)
		stride = g_w;
	xrQemuSetScanout(scan, g_w, g_h, stride);

	XrInstanceCreateInfo ici;
	memset(&ici, 0, sizeof(ici));
	ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
	ici.applicationInfo.applicationName[0] = 'D';
	ici.applicationInfo.applicationName[1] = 'e';
	ici.applicationInfo.applicationName[2] = 'o';
	ici.applicationInfo.applicationName[3] = 'd';
	ici.applicationInfo.applicationName[4] = 'h';
	ici.applicationInfo.applicationName[5] = 'a';
	ici.applicationInfo.applicationName[6] = 'i';
	ici.applicationInfo.applicationName[7] = 'X';
	ici.applicationInfo.applicationName[8] = 'R';
	ici.applicationInfo.applicationName[9] = 0;
	ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	if (XR_FAILED(xrCreateInstance(&ici, &g_instance)))
		return;

	XrSystemGetInfo sysi;
	memset(&sysi, 0, sizeof(sysi));
	sysi.type = XR_TYPE_SYSTEM_GET_INFO;
	sysi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemId sys = 0;
	if (XR_FAILED(xrGetSystem(g_instance, &sysi, &sys)))
		return;

	XrSessionCreateInfo sci;
	memset(&sci, 0, sizeof(sci));
	sci.type = XR_TYPE_SESSION_CREATE_INFO;
	sci.systemId = sys;
	if (XR_FAILED(xrCreateSession(g_instance, &sci, &g_session)))
		return;

	XrSessionBeginInfo begin;
	memset(&begin, 0, sizeof(begin));
	begin.type = XR_TYPE_SESSION_BEGIN_INFO;
	begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	if (XR_FAILED(xrBeginSession(g_session, &begin)))
		return;

	XrReferenceSpaceCreateInfo spc;
	memset(&spc, 0, sizeof(spc));
	spc.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	spc.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spc.poseInReferenceSpace.orientation.w = 1.f;
	xrCreateReferenceSpace(g_session, &spc, &g_space);

	for (int e = 0; e < 2; e++) {
		XrSwapchainCreateInfo ci;
		memset(&ci, 0, sizeof(ci));
		ci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
		ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		ci.format = 0;
		ci.sampleCount = 1;
		ci.width = (uint32_t)g_w;
		ci.height = (uint32_t)g_h;
		ci.faceCount = 1;
		ci.arraySize = 1;
		ci.mipCount = 1;
		if (XR_FAILED(xrCreateSwapchain(g_session, &ci, &g_eye[e])))
			return;
		uint32_t n = 0;
		xrEnumerateSwapchainImages(g_eye[e], 0, &n, NULL);
		if (n > 2)
			n = 2;
		xrEnumerateSwapchainImages(g_eye[e], n, &n, (XrSwapchainImageBaseHeader*)g_imgs[e]);
	}
	g_ready = 1;
	_KePrint("DeodhaiXR OpenXR qemu runtime ready %d x %d\r\n", g_w, g_h);
}

void XrPresentFrame(ChCanvas* canv) {
	if (!g_ready || !canv || !canv->buffer)
		return;

	XrFrameWaitInfo wi;
	memset(&wi, 0, sizeof(wi));
	wi.type = XR_TYPE_FRAME_WAIT_INFO;
	XrFrameState fs;
	memset(&fs, 0, sizeof(fs));
	fs.type = XR_TYPE_FRAME_STATE;
	if (XR_FAILED(xrWaitFrame(g_session, &wi, &fs)))
		return;
	XrFrameBeginInfo bi;
	memset(&bi, 0, sizeof(bi));
	bi.type = XR_TYPE_FRAME_BEGIN_INFO;
	xrBeginFrame(g_session, &bi);

	XrViewLocateInfo vli;
	memset(&vli, 0, sizeof(vli));
	vli.type = XR_TYPE_VIEW_LOCATE_INFO;
	vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	vli.displayTime = fs.predictedDisplayTime;
	vli.space = g_space;
	XrViewState vs;
	memset(&vs, 0, sizeof(vs));
	vs.type = XR_TYPE_VIEW_STATE;
	XrView views[2];
	memset(views, 0, sizeof(views));
	uint32_t nview = 0;
	xrLocateViews(g_session, &vli, &vs, 2, &nview, views);

	int stride = canv->pitch ? (int)(canv->pitch / 4) : (int)canv->canvasWidth;
	const uint32_t* src = canv->buffer;
	XrCompositionLayerQuad quads[2];
	memset(quads, 0, sizeof(quads));
	const XrCompositionLayerBaseHeader* layers[2];
	int shifts[2] = {0, 0};
	XrRect2Di damage[100];
	uint32_t damage_count = 0;
	static bool first_frame = true;
	if (first_frame) {
		damage[0].offset.x = 0;
		damage[0].offset.y = 0;
		damage[0].extent.width = g_w;
		damage[0].extent.height = g_h;
		damage_count = 1;
		first_frame = false;
	} else {
		/* The guest compositor can restore obscured windows from backing surfaces
		 * outside the immediate dirty rectangle. Publishing only clipped damage
		 * here lets QEMU retain stale/cleared pixels when windows intersect. XR
		 * therefore uses conservative full-frame damage; internal dirty tracking
		 * still avoids unnecessary composition work. --axiss */
		damage[0].offset.x = 0;
		damage[0].offset.y = 0;
		damage[0].extent.width = g_w;
		damage[0].extent.height = g_h;
		damage_count = 1;
	}

	for (int e = 0; e < 2; e++) {
		uint32_t idx = 0;
		XrSwapchainImageAcquireInfo acq;
		memset(&acq, 0, sizeof(acq));
		acq.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
		xrAcquireSwapchainImage(g_eye[e], &acq, &idx);
		XrSwapchainImageWaitInfo wait;
		memset(&wait, 0, sizeof(wait));
		wait.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
		wait.timeout = XR_INFINITE_DURATION;
		xrWaitSwapchainImage(g_eye[e], &wait);
		if (idx > 1)
			idx = 0;
		int shift = (int)(views[e].pose.position.x * 250.f);
		shifts[e] = shift;
		/* EndFrame consumes the canvas directly; the enumerated eye images
		 * remain valid for the OpenXR acquire/release contract. --axiss */
		XrSwapchainImageReleaseInfo rel;
		memset(&rel, 0, sizeof(rel));
		rel.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
		xrReleaseSwapchainImage(g_eye[e], &rel);

		quads[e].type = XR_TYPE_COMPOSITION_LAYER_QUAD;
		quads[e].space = g_space;
		quads[e].eyeVisibility =
			(e == 0) ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
		quads[e].subImage.swapchain = g_eye[e];
		quads[e].subImage.imageRect.extent.width = g_w;
		quads[e].subImage.imageRect.extent.height = g_h;
		quads[e].pose.orientation.w = 1.f;
		quads[e].pose.position.z = -2.f;
		quads[e].size.width = 1.6f;
		quads[e].size.height = 0.9f;
		layers[e] = (const XrCompositionLayerBaseHeader*)&quads[e];
	}

	XrFrameEndInfo ei;
	memset(&ei, 0, sizeof(ei));
	ei.type = XR_TYPE_FRAME_END_INFO;
	ei.displayTime = fs.predictedDisplayTime;
	ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	ei.layerCount = 2;
	ei.layers = layers;
	xrQemuSetCanvasSrc(src, stride, shifts[0], shifts[1]);
	xrQemuSetCanvasDamage(damage, damage_count);
	xrEndFrame(g_session, &ei);

	/* Flat-panel transport preserves source coordinates and full horizontal
	 * detail, so the GPU damage list is the original canvas damage. --axiss */
	InitialiseDirtyClipList();
	for (uint32_t i = 0; i < damage_count; i++) {
		AddDirtyClip(damage[i].offset.x, damage[i].offset.y,
					 damage[i].extent.width, damage[i].extent.height);
	}
}

void XrPresentShutdown(void) {
	if (!g_ready)
		return;
	xrDestroySwapchain(g_eye[0]);
	xrDestroySwapchain(g_eye[1]);
	xrDestroySpace(g_space);
	xrEndSession(g_session);
	xrDestroySession(g_session);
	xrDestroyInstance(g_instance);
	g_ready = 0;
}

int XrPresentEnabled(void) {
	return g_ready;
}
#endif
