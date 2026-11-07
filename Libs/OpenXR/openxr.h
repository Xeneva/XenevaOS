#ifndef XENEVA_OPENXR_H_
#define XENEVA_OPENXR_H_
/*
 * Minimal OpenXR 1.0 types + the calls DeodhaiXR emits.
 * Hardware: swap this include for the vendor SDK and link a loader.
 * QEMU demo: XrRuntimeQemu defines the symbols.
 */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XR_MAKE_VERSION(major, minor, patch) \
	((((uint64_t)(major)&0xffffULL) << 48) | (((uint64_t)(minor)&0xffffULL) << 32) | \
	 ((uint64_t)(patch)&0xffffffffULL))
#define XR_CURRENT_API_VERSION XR_MAKE_VERSION(1, 0, 34)
#define XR_SUCCEEDED(r) ((r) >= 0)
#define XR_FAILED(r) ((r) < 0)
#define XR_NULL_HANDLE 0
#define XR_NULL_SYSTEM_ID 0
#define XR_INFINITE_DURATION 0x7fffffffffffffffLL

typedef uint64_t XrInstance;
typedef uint64_t XrSession;
typedef uint64_t XrSpace;
typedef uint64_t XrSwapchain;
typedef uint64_t XrSystemId;
typedef uint64_t XrTime;
typedef uint64_t XrDuration;
typedef uint32_t XrBool32;
typedef uint64_t XrFlags64;

typedef enum XrResult {
	XR_SUCCESS = 0,
	XR_TIMEOUT_EXPIRED = 1,
	XR_SESSION_LOSS_PENDING = 3,
	XR_ERROR_VALIDATION_FAILURE = -1,
	XR_ERROR_RUNTIME_FAILURE = -2,
	XR_ERROR_HANDLE_INVALID = -12,
	XR_ERROR_SESSION_NOT_RUNNING = -14,
	XR_ERROR_SWAPCHAIN_RECT_INVALID = -24,
	XR_ERROR_FEATURE_UNSUPPORTED = -41
} XrResult;

typedef enum XrStructureType {
	XR_TYPE_UNKNOWN = 0,
	XR_TYPE_INSTANCE_CREATE_INFO = 1,
	XR_TYPE_SYSTEM_GET_INFO = 4,
	XR_TYPE_SESSION_CREATE_INFO = 5,
	XR_TYPE_SWAPCHAIN_CREATE_INFO = 9,
	XR_TYPE_REFERENCE_SPACE_CREATE_INFO = 24,
	XR_TYPE_VIEW_LOCATE_INFO = 36,
	XR_TYPE_VIEW = 37,
	XR_TYPE_FRAME_WAIT_INFO = 44,
	XR_TYPE_FRAME_STATE = 45,
	XR_TYPE_FRAME_BEGIN_INFO = 46,
	XR_TYPE_FRAME_END_INFO = 47,
	XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO = 52,
	XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO = 53,
	XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO = 54,
	XR_TYPE_SESSION_BEGIN_INFO = 55,
	XR_TYPE_COMPOSITION_LAYER_QUAD = 62,
	XR_TYPE_VIEW_STATE = 89,
	XR_TYPE_SWAPCHAIN_IMAGE_CPU_XENEVA = 1000000000
} XrStructureType;

typedef enum XrFormFactor { XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY = 1 } XrFormFactor;
typedef enum XrViewConfigurationType {
	XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO = 2
} XrViewConfigurationType;
typedef enum XrReferenceSpaceType { XR_REFERENCE_SPACE_TYPE_LOCAL = 1 } XrReferenceSpaceType;
typedef enum XrSessionState {
	XR_SESSION_STATE_UNKNOWN = 0,
	XR_SESSION_STATE_IDLE = 1,
	XR_SESSION_STATE_READY = 2,
	XR_SESSION_STATE_SYNCHRONIZED = 3,
	XR_SESSION_STATE_VISIBLE = 4,
	XR_SESSION_STATE_FOCUSED = 5
} XrSessionState;
typedef enum XrEnvironmentBlendMode { XR_ENVIRONMENT_BLEND_MODE_OPAQUE = 1 } XrEnvironmentBlendMode;
typedef enum XrSwapchainUsageFlagBits {
	XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT = 0x00000001,
	XR_SWAPCHAIN_USAGE_SAMPLED_BIT = 0x00000020
} XrSwapchainUsageFlagBits;
typedef XrFlags64 XrSwapchainUsageFlags;
typedef enum XrCompositionLayerFlagBits {
	XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT = 0x00000001
} XrCompositionLayerFlagBits;
typedef XrFlags64 XrCompositionLayerFlags;
typedef enum XrEyeVisibility {
	XR_EYE_VISIBILITY_BOTH = 0,
	XR_EYE_VISIBILITY_LEFT = 1,
	XR_EYE_VISIBILITY_RIGHT = 2
} XrEyeVisibility;

typedef struct XrVector3f {
	float x, y, z;
} XrVector3f;
typedef struct XrQuaternionf {
	float x, y, z, w;
} XrQuaternionf;
typedef struct XrPosef {
	XrQuaternionf orientation;
	XrVector3f position;
} XrPosef;
typedef struct XrFovf {
	float angleLeft, angleRight, angleUp, angleDown;
} XrFovf;
typedef struct XrExtent2Di {
	int32_t width, height;
} XrExtent2Di;
typedef struct XrExtent2Df {
	float width, height;
} XrExtent2Df;
typedef struct XrRect2Di {
	struct {
		int32_t x, y;
	} offset;
	XrExtent2Di extent;
} XrRect2Di;
typedef struct XrOffset2Di {
	int32_t x, y;
} XrOffset2Di;

typedef struct XrApplicationInfo {
	char applicationName[128];
	uint32_t applicationVersion;
	char engineName[128];
	uint32_t engineVersion;
	uint64_t apiVersion;
} XrApplicationInfo;

typedef struct XrInstanceCreateInfo {
	XrStructureType type;
	const void* next;
	uint64_t createFlags;
	XrApplicationInfo applicationInfo;
	uint32_t enabledApiLayerCount;
	const char* const* enabledApiLayerNames;
	uint32_t enabledExtensionCount;
	const char* const* enabledExtensionNames;
} XrInstanceCreateInfo;

typedef struct XrSystemGetInfo {
	XrStructureType type;
	const void* next;
	XrFormFactor formFactor;
} XrSystemGetInfo;

typedef struct XrSessionCreateInfo {
	XrStructureType type;
	const void* next;
	uint64_t createFlags;
	XrSystemId systemId;
} XrSessionCreateInfo;

typedef struct XrSessionBeginInfo {
	XrStructureType type;
	const void* next;
	XrViewConfigurationType primaryViewConfigurationType;
} XrSessionBeginInfo;

typedef struct XrSwapchainCreateInfo {
	XrStructureType type;
	const void* next;
	uint64_t createFlags;
	XrSwapchainUsageFlags usageFlags;
	int64_t format;
	uint32_t sampleCount;
	uint32_t width;
	uint32_t height;
	uint32_t faceCount;
	uint32_t arraySize;
	uint32_t mipCount;
} XrSwapchainCreateInfo;

typedef struct XrSwapchainImageBaseHeader {
	XrStructureType type;
	void* next;
} XrSwapchainImageBaseHeader;

typedef struct XrSwapchainImageCpuXeneva {
	XrStructureType type;
	void* next;
	uint32_t* rgba;
	uint32_t width;
	uint32_t height;
} XrSwapchainImageCpuXeneva;

typedef struct XrSwapchainImageAcquireInfo {
	XrStructureType type;
	const void* next;
} XrSwapchainImageAcquireInfo;
typedef struct XrSwapchainImageWaitInfo {
	XrStructureType type;
	const void* next;
	XrDuration timeout;
} XrSwapchainImageWaitInfo;
typedef struct XrSwapchainImageReleaseInfo {
	XrStructureType type;
	const void* next;
} XrSwapchainImageReleaseInfo;

typedef struct XrReferenceSpaceCreateInfo {
	XrStructureType type;
	const void* next;
	XrReferenceSpaceType referenceSpaceType;
	XrPosef poseInReferenceSpace;
} XrReferenceSpaceCreateInfo;

typedef struct XrView {
	XrStructureType type;
	void* next;
	XrPosef pose;
	XrFovf fov;
} XrView;

typedef struct XrViewLocateInfo {
	XrStructureType type;
	const void* next;
	XrViewConfigurationType viewConfigurationType;
	XrTime displayTime;
	XrSpace space;
} XrViewLocateInfo;

typedef struct XrViewState {
	XrStructureType type;
	const void* next;
	uint64_t viewStateFlags;
} XrViewState;

typedef struct XrSwapchainSubImage {
	XrSwapchain swapchain;
	XrRect2Di imageRect;
	uint32_t imageArrayIndex;
} XrSwapchainSubImage;

typedef struct XrCompositionLayerBaseHeader {
	XrStructureType type;
	const void* next;
	XrCompositionLayerFlags layerFlags;
	XrSpace space;
} XrCompositionLayerBaseHeader;

typedef struct XrCompositionLayerQuad {
	XrStructureType type;
	const void* next;
	XrCompositionLayerFlags layerFlags;
	XrSpace space;
	XrEyeVisibility eyeVisibility;
	XrSwapchainSubImage subImage;
	XrPosef pose;
	XrExtent2Df size;
} XrCompositionLayerQuad;

typedef struct XrFrameWaitInfo {
	XrStructureType type;
	const void* next;
} XrFrameWaitInfo;
typedef struct XrFrameState {
	XrStructureType type;
	void* next;
	XrTime predictedDisplayTime;
	XrDuration predictedDisplayPeriod;
	XrBool32 shouldRender;
} XrFrameState;
typedef struct XrFrameBeginInfo {
	XrStructureType type;
	const void* next;
} XrFrameBeginInfo;
typedef struct XrFrameEndInfo {
	XrStructureType type;
	const void* next;
	XrTime displayTime;
	XrEnvironmentBlendMode environmentBlendMode;
	uint32_t layerCount;
	const XrCompositionLayerBaseHeader* const* layers;
} XrFrameEndInfo;

XrResult xrCreateInstance(const XrInstanceCreateInfo* info, XrInstance* instance);
XrResult xrDestroyInstance(XrInstance instance);
XrResult xrGetSystem(XrInstance instance, const XrSystemGetInfo* info, XrSystemId* systemId);
XrResult xrCreateSession(XrInstance instance, const XrSessionCreateInfo* info, XrSession* session);
XrResult xrDestroySession(XrSession session);
XrResult xrBeginSession(XrSession session, const XrSessionBeginInfo* info);
XrResult xrEndSession(XrSession session);
XrResult xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* info,
								XrSpace* space);
XrResult xrDestroySpace(XrSpace space);
XrResult xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* info,
						   XrSwapchain* swapchain);
XrResult xrDestroySwapchain(XrSwapchain swapchain);
XrResult xrEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t count, uint32_t* written,
									XrSwapchainImageBaseHeader* images);
XrResult xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo* info,
								 uint32_t* index);
XrResult xrWaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo* info);
XrResult xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo* info);
XrResult xrWaitFrame(XrSession session, const XrFrameWaitInfo* info, XrFrameState* state);
XrResult xrBeginFrame(XrSession session, const XrFrameBeginInfo* info);
XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* info);
XrResult xrLocateViews(XrSession session, const XrViewLocateInfo* info, XrViewState* state,
					   uint32_t viewCapacity, uint32_t* viewCount, XrView* views);

/* QEMU runtime: where EndFrame should SBS-blit. Not part of Khronos. */
void xrQemuSetScanout(uint32_t* pixels, int width, int height, int stride_pixels);
/* Fused present: source canvas + per-eye parallax shifts for EndFrame to
 * squeeze directly into the scanout halves (skips the eye-image middlemen). */
void xrQemuSetCanvasSrc(const uint32_t* pixels, int stride_pixels, int shift_l, int shift_r);
void xrQemuSetCanvasDamage(const XrRect2Di* rects, uint32_t count);

#ifdef __cplusplus
}
#endif
#endif
