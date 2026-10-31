/*
 * Host OpenXR viewer: put a live QEMU (or PPM) frame on a world-locked
 * quad. Talks to whatever runtime XR_RUNTIME_JSON points at (WiVRn when
 * the Quest is connected). This is not a guest binary.
 */
#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#define GL_GLEXT_PROTOTYPES

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "pointer_filter.h"
#include "qemu_egl_capture.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <time.h>
#include <utility>
#include <vector>
#include <unistd.h>

static void die(const char* msg) {
	std::fprintf(stderr, "xeneva-xr-view: %s\n", msg);
	std::exit(1);
}

static uint64_t host_monotonic_ns() {
	struct timespec ts{};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void xr_check(XrResult r, const char* what) {
	if (XR_FAILED(r)) {
		char buf[128];
		std::snprintf(buf, sizeof(buf), "%s failed (%d)", what, (int)r);
		die(buf);
	}
}

static bool xr_has_extension(const char* wanted) {
	uint32_t count = 0;
	if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr)))
		return false;
	std::vector<XrExtensionProperties> props(count, {XR_TYPE_EXTENSION_PROPERTIES});
	if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, props.data())))
		return false;
	for (const auto& prop : props) {
		if (!std::strcmp(prop.extensionName, wanted))
			return true;
	}
	return false;
}

struct Capture {
	enum Mode { Pattern, X11, Ppm, Egl } mode = Pattern;
	std::string dbus_addr;
	std::string window_substr = "QEMU";
	std::string ppm_path;
	Display* dpy = nullptr;
	Window win = 0;
	Window exclude = 0;
	/* OPENXR guest (DeodhaiXR --openxr) emits a side-by-side scanout: left
	 * eye in the left half, right eye in the right half. The HMD path shows
	 * each eye only its own half (ocular); --mono shows the full frame to
	 * both eyes (for non-OPENXR 2D guests). */
	bool stereo = true;
	/* --hands: drive the guest cursor from right-hand tracking (ray +
	 * pinch), injected over dbus like VNC input. */
	bool hands = false;
	/* Standard XR_EXT_hand_tracking joint mesh. --hands enables it by
	 * default; --hand-mesh can also display hands without pointer input. */
	bool hand_mesh = false;
	/* --controllers: same via the right controller (aim + trigger);
	 * wins over hands while valid. */
	bool controllers = false;
	/* Relative pointer gain, guest pixels per meter of hand travel. */
	float hands_gain = 20000.f;
	/* Native-eye upscale policy: nearest keeps UI glyphs crisp; linear is
	 * available for image-heavy guests. */
	bool linear_filter = false;
	int w = 1024;
	int h = 768;
	std::vector<uint8_t> rgba;
};

static std::string ascii_lower(const char* s) {
	std::string o;
	if (!s)
		return o;
	for (; *s; s++)
		o.push_back((char)std::tolower((unsigned char)*s));
	return o;
}

static std::string window_label(Display* dpy, Window w) {
	std::string label;
	char* name = nullptr;
	if (XFetchName(dpy, w, &name) && name) {
		label += name;
		XFree(name);
	}
	XClassHint ch{};
	if (XGetClassHint(dpy, w, &ch)) {
		if (ch.res_name) {
			if (!label.empty())
				label += " ";
			label += ch.res_name;
			XFree(ch.res_name);
		}
		if (ch.res_class) {
			label += " ";
			label += ch.res_class;
			XFree(ch.res_class);
		}
	}
	return label;
}

static bool name_matches(Display* dpy, Window w, const std::string& needle) {
	std::string want = ascii_lower(needle.c_str());
	if (want.empty())
		return false;
	std::string label = ascii_lower(window_label(dpy, w).c_str());
	if (label.find(want) != std::string::npos)
		return true;
	char* name = nullptr;
	if (XFetchName(dpy, w, &name) && name) {
		bool ok = ascii_lower(name).find(want) != std::string::npos;
		XFree(name);
		if (ok)
			return true;
	}
	Atom net = XInternAtom(dpy, "_NET_WM_NAME", True);
	if (net == None)
		return false;
	Atom type;
	int fmt;
	unsigned long n = 0, extra = 0;
	unsigned char* prop = nullptr;
	if (XGetWindowProperty(dpy, w, net, 0, 256, False, AnyPropertyType, &type, &fmt, &n, &extra,
						   &prop) == Success &&
		prop) {
		bool ok = ascii_lower(reinterpret_cast<char*>(prop)).find(want) != std::string::npos;
		XFree(prop);
		return ok;
	}
	return false;
}

static bool find_window(Display* dpy, Window root, const std::string& needle, Window* out,
						Window exclude) {
	Window parent, *kids = nullptr;
	unsigned int n = 0;
	if (!XQueryTree(dpy, root, &root, &parent, &kids, &n) || !kids)
		return false;
	for (unsigned int i = 0; i < n; i++) {
		if (kids[i] == exclude)
			continue;
		if (name_matches(dpy, kids[i], needle)) {
			/* Skip helper/dummy windows (e.g. QEMU's 10x10 InputOnly
			 * window): only real mapped image windows are capturable
			 * with XGetImage. */
			XWindowAttributes wattr;
			if (!XGetWindowAttributes(dpy, kids[i], &wattr) || wattr.width < 16 ||
				wattr.height < 16 || wattr.c_class == InputOnly)
				continue;
			std::string nm = "";
			char* title = nullptr;
			if (XFetchName(dpy, kids[i], &title) && title) {
				nm = ascii_lower(title);
				XFree(title);
			}
			if (nm.find("xeneva-xr-view") != std::string::npos ||
				nm.find("ocular preview") != std::string::npos)
				continue;
			*out = kids[i];
			XFree(kids);
			return true;
		}
		if (find_window(dpy, kids[i], needle, out, exclude)) {
			XFree(kids);
			return true;
		}
	}
	XFree(kids);
	return false;
}

static void list_windows_rec(Display* dpy, Window root, int depth) {
	Window parent, *kids = nullptr;
	unsigned int n = 0;
	if (!XQueryTree(dpy, root, &root, &parent, &kids, &n) || !kids)
		return;
	for (unsigned int i = 0; i < n; i++) {
		XWindowAttributes attr;
		if (XGetWindowAttributes(dpy, kids[i], &attr) && attr.map_state == IsViewable &&
			attr.width > 16 && attr.height > 16) {
			std::string lab = window_label(dpy, kids[i]);
			if (!lab.empty())
				std::fprintf(stderr, "  0x%lx %dx%d %s\n", (unsigned long)kids[i], attr.width,
							 attr.height, lab.c_str());
		}
		list_windows_rec(dpy, kids[i], depth + 1);
	}
	XFree(kids);
}

static void capture_pattern(Capture* c) {
	c->rgba.resize((size_t)c->w * (size_t)c->h * 4);
	int cx = c->w / 2, cy = c->h / 2;
	for (int y = 0; y < c->h; y++) {
		for (int x = 0; x < c->w; x++) {
			uint8_t* p = &c->rgba[((size_t)y * c->w + x) * 4];
			int chk = ((x / 32) ^ (y / 32)) & 1;
			p[0] = chk ? 40 : 20;
			p[1] = chk ? 44 : 22;
			p[2] = chk ? 52 : 28;
			int dx = x - cx, dy = y - cy;
			if (dx * dx + dy * dy < 70 * 70) {
				p[0] = 240;
				p[1] = 220;
				p[2] = 40;
			}
			if (std::abs(dx) < 3 || std::abs(dy) < 3) {
				p[0] = 220;
				p[1] = 80;
				p[2] = 80;
			}
			p[3] = 255;
		}
	}
}

static bool capture_x11(Capture* c) {
	if (!c->dpy) {
		c->dpy = XOpenDisplay(nullptr);
		if (!c->dpy)
			return false;
	}
	if (!c->win) {
		Window root = DefaultRootWindow(c->dpy);
		if (c->win == c->exclude)
			c->win = 0;
		const char* tries[] = {c->window_substr.c_str(), "qemu-system", "qemu"};
		for (const char* t : tries) {
			if (find_window(c->dpy, root, t, &c->win, c->exclude)) {
				std::fprintf(stderr, "xeneva-xr-view: capturing window 0x%lx (matched '%s')\n",
							 (unsigned long)c->win, t);
				break;
			}
		}
		if (!c->win) {
			static bool listed;
			if (!listed) {
				std::fprintf(stderr,
							 "xeneva-xr-view: no X11 window matching qemu. "
							 "On Wayland, restart QEMU with GDK_BACKEND=x11.\n"
							 "Mapped X11 windows:\n");
				list_windows_rec(c->dpy, root, 0);
				listed = true;
			}
			return false;
		}
	}
	XWindowAttributes attr;
	if (!XGetWindowAttributes(c->dpy, c->win, &attr) || attr.width < 2 || attr.height < 2)
		return false;
	XImage* img = XGetImage(c->dpy, c->win, 0, 0, attr.width, attr.height, AllPlanes, ZPixmap);
	if (!img)
		return false;
	c->w = attr.width;
	c->h = attr.height;
	c->rgba.resize((size_t)c->w * (size_t)c->h * 4);
	for (int y = 0; y < c->h; y++) {
		for (int x = 0; x < c->w; x++) {
			unsigned long px = XGetPixel(img, x, y);
			uint8_t* d = &c->rgba[((size_t)y * c->w + x) * 4];
			d[0] = (uint8_t)((px >> 16) & 0xFF);
			d[1] = (uint8_t)((px >> 8) & 0xFF);
			d[2] = (uint8_t)(px & 0xFF);
			d[3] = 255;
		}
	}
	XDestroyImage(img);
	return true;
}

static bool capture_ppm(Capture* c) {
	FILE* f = std::fopen(c->ppm_path.c_str(), "rb");
	if (!f)
		return false;
	char magic[8];
	if (std::fscanf(f, "%7s", magic) != 1 || std::strcmp(magic, "P6") != 0) {
		std::fclose(f);
		return false;
	}
	int w = 0, h = 0, maxv = 0;
	if (std::fscanf(f, "%d %d %d", &w, &h, &maxv) != 3 || w <= 0 || h <= 0) {
		std::fclose(f);
		return false;
	}
	std::fgetc(f);
	std::vector<uint8_t> rgb((size_t)w * (size_t)h * 3);
	if (std::fread(rgb.data(), 1, rgb.size(), f) != rgb.size()) {
		std::fclose(f);
		return false;
	}
	std::fclose(f);
	c->w = w;
	c->h = h;
	c->rgba.resize((size_t)w * (size_t)h * 4);
	for (int i = 0; i < w * h; i++) {
		c->rgba[i * 4 + 0] = rgb[i * 3 + 0];
		c->rgba[i * 4 + 1] = rgb[i * 3 + 1];
		c->rgba[i * 4 + 2] = rgb[i * 3 + 2];
		c->rgba[i * 4 + 3] = 255;
	}
	return true;
}

static void capture_frame(Capture* c) {
	bool ok = false;
	if (c->mode == Capture::X11)
		ok = capture_x11(c);
	else if (c->mode == Capture::Ppm)
		ok = capture_ppm(c);
	else if (c->mode == Capture::Egl) {
		uint32_t* px = nullptr;
		int w = 0, h = 0;
		ok = qemu_egl_poll(&px, &w, &h);
		if (ok && px) {
			c->w = w;
			c->h = h;
			c->rgba.assign((uint8_t*)px, (uint8_t*)px + (size_t)w * h * 4);
		}
	}
	if (!ok) {
		static bool warned;
		if (!warned && c->mode != Capture::Pattern) {
			std::fprintf(stderr,
						 "xeneva-xr-view: no QEMU window yet (need a mapped GTK window). "
						 "Showing stereo test chart. Pass --pattern to hide this warning.\n");
			warned = true;
		}
		capture_pattern(c);
	}
}

struct GlxHeadless {
	Display* dpy = nullptr;
	Window win = 0;
	GLXContext ctx = nullptr;
	GLXFBConfig fb_config = nullptr;
	uint32_t visual_id = 0;
};

static GlxHeadless make_glx() {
	GlxHeadless g;
	g.dpy = XOpenDisplay(nullptr);
	if (!g.dpy)
		die("XOpenDisplay failed (need a display for GLX + OpenXR)");
	static int fb_attr[] = {GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
							GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_DOUBLEBUFFER, True,
							GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
							GLX_DEPTH_SIZE, 16, None};
	int ncfg = 0;
	GLXFBConfig* configs =
		glXChooseFBConfig(g.dpy, DefaultScreen(g.dpy), fb_attr, &ncfg);
	if (!configs || ncfg < 1)
		die("glXChooseFBConfig failed");
	g.fb_config = configs[0];
	XVisualInfo* vis = glXGetVisualFromFBConfig(g.dpy, g.fb_config);
	if (!vis)
		die("glXGetVisualFromFBConfig failed");
	g.visual_id = (uint32_t)vis->visualid;
	Colormap cmap = XCreateColormap(g.dpy, RootWindow(g.dpy, vis->screen), vis->visual, AllocNone);
	XSetWindowAttributes swa;
	swa.colormap = cmap;
	swa.event_mask = 0;
	g.win = XCreateWindow(g.dpy, RootWindow(g.dpy, vis->screen), 0, 0, 64, 64, 0, vis->depth,
						  InputOutput, vis->visual, CWColormap | CWEventMask, &swa);
	g.ctx = glXCreateNewContext(g.dpy, g.fb_config, GLX_RGBA_TYPE, nullptr, True);
	if (!g.ctx)
		die("glXCreateNewContext failed");
	if (!glXMakeCurrent(g.dpy, g.win, g.ctx))
		die("glXMakeCurrent failed");
	XFree(vis);
	XFree(configs);
	return g;
}

struct Swapchain {
	XrSwapchain handle = XR_NULL_HANDLE;
	int32_t w = 0, h = 0;
	int64_t format = GL_RGBA8;
	/* Placed guest rect inside the swapchain image. The swapchain stays
	 * at HMD res; the guest is pasted native-size centered (letterbox)
	 * so no per-pixel rescale ever runs --axiss */
	int up_x = 0, up_y = 0, up_w = 0, up_h = 0;
	std::vector<XrSwapchainImageOpenGLKHR> images;
};

static Swapchain make_swapchain(XrSession session, int32_t w, int32_t h) {
	Swapchain sc;
	sc.w = w;
	sc.h = h;
	sc.up_x = 0;
	sc.up_y = 0;
	sc.up_w = w;
	sc.up_h = h;
	uint32_t nfmt = 0;
	xr_check(xrEnumerateSwapchainFormats(session, 0, &nfmt, nullptr), "enumerate formats");
	std::vector<int64_t> fmts(nfmt);
	xr_check(xrEnumerateSwapchainFormats(session, nfmt, &nfmt, fmts.data()), "enumerate formats");
	bool have_srgb = false, have_rgba = false;
	for (int64_t f : fmts) {
		have_srgb = have_srgb || f == GL_SRGB8_ALPHA8;
		have_rgba = have_rgba || f == GL_RGBA8;
	}
	/* The guest compositor has already produced display-encoded bytes. Keep
	 * them byte-for-byte in a linear RGBA swapchain; an sRGB render target
	 * applies another transfer function and crushes the glass gradients. */
	int64_t format = have_rgba ? GL_RGBA8 : have_srgb ? GL_SRGB8_ALPHA8 : fmts[0];
	sc.format = format;
	XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
	ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
		XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	ci.format = format;
	ci.sampleCount = 1;
	ci.width = (uint32_t)w;
	ci.height = (uint32_t)h;
	ci.faceCount = 1;
	ci.arraySize = 1;
	ci.mipCount = 1;
	xr_check(xrCreateSwapchain(session, &ci, &sc.handle), "xrCreateSwapchain");
	uint32_t nimg = 0;
	xr_check(xrEnumerateSwapchainImages(sc.handle, 0, &nimg, nullptr), "nimg");
	sc.images.resize(nimg, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
	xr_check(xrEnumerateSwapchainImages(
				 sc.handle,
				 nimg,
				 &nimg,
				 reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data())),
			 "swapchain images");
	return sc;
}

static void upload_rgba(Swapchain& sc, const Capture& cap) {
	XrSwapchainImageAcquireInfo acq{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	uint32_t idx = 0;
	xr_check(xrAcquireSwapchainImage(sc.handle, &acq, &idx), "acquire");
	XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	wait.timeout = XR_INFINITE_DURATION;
	xr_check(xrWaitSwapchainImage(sc.handle, &wait), "wait");
	GLuint tex = sc.images[idx].image;
	while (glGetError() != GL_NO_ERROR) {}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	if (!cap.rgba.empty()) {
		/* Capture buffers are top-row-first; GL/OpenXR swapchain textures
		 * expect row 0 at the bottom, so upload vertically flipped.
		 * Without this the HMD image is upside down. The guest is pasted
		 * native-size centered: row copies only, no resampling, and only
		 * the placed region is uploaded. Reuse one scratch buffer. */
		static std::vector<uint8_t> scratch;
		if (cap.w <= sc.w && cap.h <= sc.h) {
			/* Preserve the guest pixels and submit only their centered rectangle.
			 * A second FBO used for scaling crashes Mesa's WiVRn OpenGL path when
			 * QEMU capture is active, and is unnecessary for a smaller guest. */
			size_t need = (size_t)cap.w * (size_t)cap.h * 4;
			if (scratch.size() != need)
				scratch.resize(need);
			for (int y = 0; y < cap.h; y++)
				std::memcpy(&scratch[(size_t)y * cap.w * 4],
							&cap.rgba[(size_t)(cap.h - 1 - y) * cap.w * 4],
							(size_t)cap.w * 4);
			sc.up_x = (sc.w - cap.w) / 2;
			sc.up_y = (sc.h - cap.h) / 2;
			sc.up_w = cap.w;
			sc.up_h = cap.h;
			glBindTexture(GL_TEXTURE_2D, tex);
			GLint filter = cap.linear_filter ? GL_LINEAR : GL_NEAREST;
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexSubImage2D(GL_TEXTURE_2D, 0, sc.up_x, sc.up_y, cap.w, cap.h,
							GL_RGBA, GL_UNSIGNED_BYTE, scratch.data());
		} else {
			/* Guest bigger than the swapchain (shouldn't happen): scale
			 * down with plain assignments, no per-pixel memcpy calls. */
			glBindTexture(GL_TEXTURE_2D, tex);
			sc.up_x = 0;
			sc.up_y = 0;
			sc.up_w = sc.w;
			sc.up_h = sc.h;
			size_t need = (size_t)sc.w * (size_t)sc.h * 4;
			if (scratch.size() != need)
				scratch.resize(need);
			uint32_t* d = reinterpret_cast<uint32_t*>(scratch.data());
			const uint32_t* s = reinterpret_cast<const uint32_t*>(cap.rgba.data());
			for (int y = 0; y < sc.h; y++) {
				int sy = cap.h - 1 - (int)((int64_t)y * cap.h / sc.h);
				uint32_t* drow = d + (size_t)y * sc.w;
				const uint32_t* srow = s + (size_t)sy * cap.w;
				for (int x = 0; x < sc.w; x++)
					drow[x] = srow[(int)((int64_t)x * cap.w / sc.w)];
			}
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sc.w, sc.h, GL_RGBA, GL_UNSIGNED_BYTE,
							scratch.data());
		}
	}
	/* xrReleaseSwapchainImage hands the texture back to WiVRn. Submit all GL
	 * writes first; llvmpipe otherwise leaves the CPU upload queued and the
	 * compositor samples the untouched black swapchain image. */
	glFinish();
	static int verified_w = 0, verified_h = 0;
	if (verified_w != sc.up_w || verified_h != sc.up_h) {
		GLint bound = 0, tex_w = 0, tex_h = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tex_w);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &tex_h);
		GLenum upload_error = glGetError();
		std::vector<uint8_t> check((size_t)sc.w * sc.h * 4);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, check.data());
		size_t nonblack = 0;
		uint64_t checksum = 1469598103934665603ull;
		for (size_t i = 0; i < check.size(); i += 388) {
			nonblack += check[i] || check[i + 1] || check[i + 2];
			checksum = (checksum ^ check[i]) * 1099511628211ull;
		}
		std::fprintf(stderr,
				"xeneva-xr-view: swapchain upload verified %dx%d tex=%u bound=%d storage=%dx%d "
				"gl_error=0x%x samples_nonblack=%zu checksum=0x%llx\n",
				sc.up_w, sc.up_h, tex, bound, tex_w, tex_h, upload_error,
				nonblack, (unsigned long long)checksum);
		verified_w = sc.up_w;
		verified_h = sc.up_h;
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	XrSwapchainImageReleaseInfo rel{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	xr_check(xrReleaseSwapchainImage(sc.handle, &rel), "release");
}

struct PointerVisual {
	XrPosef pose{};
	bool valid = false;
};

struct VisualInputs {
	PointerVisual handAim;
	PointerVisual controllerAim[2];
};

#ifdef XR_EXT_HAND_TRACKING_EXTENSION_NAME
struct FilteredJoint {
	PointerFilter xy;
	PointerFilter z;
};

struct HandVisual {
	PFN_xrCreateHandTrackerEXT create = nullptr;
	PFN_xrDestroyHandTrackerEXT destroy = nullptr;
	PFN_xrLocateHandJointsEXT locate = nullptr;
	XrHandTrackerEXT tracker[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
	std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> joints[2];
	std::array<FilteredJoint, XR_HAND_JOINT_COUNT_EXT> filters[2];
	bool active[2] = {false, false};
	Swapchain eye[2];
	GLuint fbo[2] = {};
	GLuint depth[2] = {};
	bool ready = false;
};

struct Vec3 {
	float x, y, z;
};

static Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 cross(Vec3 a, Vec3 b) {
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x};
}
static Vec3 normalized(Vec3 v) {
	float len = std::sqrt(dot(v, v));
	return len > 1e-6f ? v * (1.f / len) : Vec3{0.f, 1.f, 0.f};
}
static Vec3 joint_pos(const XrHandJointLocationEXT& joint) {
	return {joint.pose.position.x, joint.pose.position.y, joint.pose.position.z};
}

static void draw_sphere(Vec3 center, float radius);
static void draw_bone(Vec3 a, Vec3 b, float ra, float rb);

static Vec3 pose_point(const XrPosef& pose, Vec3 point) {
	const auto& q = pose.orientation;
	Vec3 u{q.x, q.y, q.z};
	Vec3 rotated = point + cross(u, point) * (2.f * q.w) +
		cross(u, cross(u, point)) * 2.f;
	return rotated + Vec3{pose.position.x, pose.position.y, pose.position.z};
}

static void draw_pointer(const PointerVisual& pointer, bool controller, int side) {
	if (!pointer.valid)
		return;
	/* OpenXR aim points along the action space's negative Z axis. Keep every
	 * vertex in the same LOCAL space as the projection layer and eye views. */
	Vec3 origin = pose_point(pointer.pose, {0.f, 0.f, 0.f});
	Vec3 tip = pose_point(pointer.pose, {0.f, 0.f, -1.5f});
	if (controller) {
		glColor4f(side == 0 ? 0.12f : 0.93f, 0.66f, 0.85f, 1.f);
		Vec3 base = pose_point(pointer.pose, {0.f, -0.025f, 0.11f});
		Vec3 front = pose_point(pointer.pose, {0.f, 0.012f, -0.055f});
		draw_bone(base, front, 0.028f, 0.038f);
		draw_sphere(front, 0.038f);
		origin = pose_point(pointer.pose, {0.f, 0.012f, -0.09f});
	} else {
		glColor4f(0.95f, 0.75f, 0.19f, 1.f);
	}
	draw_bone(origin, tip, 0.004f, 0.0015f);
	draw_sphere(tip, 0.012f);
}

static void draw_sphere(Vec3 center, float radius) {
	constexpr int rings = 5;
	constexpr int sides = 8;
	constexpr float pi = 3.14159265358979323846f;
	for (int ring = 0; ring < rings; ++ring) {
		float a0 = -pi * 0.5f + pi * (float)ring / rings;
		float a1 = -pi * 0.5f + pi * (float)(ring + 1) / rings;
		glBegin(GL_TRIANGLE_STRIP);
		for (int side = 0; side <= sides; ++side) {
			float lon = 2.f * pi * (float)side / sides;
			for (float lat : {a0, a1}) {
				float c = std::cos(lat);
				glVertex3f(center.x + radius * c * std::cos(lon),
						   center.y + radius * std::sin(lat),
						   center.z + radius * c * std::sin(lon));
			}
		}
		glEnd();
	}
}

static void draw_bone(Vec3 a, Vec3 b, float ra, float rb) {
	Vec3 axis = b - a;
	if (dot(axis, axis) < 1e-8f)
		return;
	axis = normalized(axis);
	Vec3 helper = std::fabs(axis.z) < 0.8f ? Vec3{0.f, 0.f, 1.f} : Vec3{0.f, 1.f, 0.f};
	Vec3 u = normalized(cross(axis, helper));
	Vec3 v = cross(axis, u);
	constexpr int sides = 8;
	constexpr float pi = 3.14159265358979323846f;
	glBegin(GL_TRIANGLE_STRIP);
	for (int i = 0; i <= sides; ++i) {
		float angle = 2.f * pi * (float)i / sides;
		Vec3 radial = u * std::cos(angle) + v * std::sin(angle);
		Vec3 pa = a + radial * ra;
		Vec3 pb = b + radial * rb;
		glVertex3f(pa.x, pa.y, pa.z);
		glVertex3f(pb.x, pb.y, pb.z);
	}
	glEnd();
}

static void projection_matrix(const XrFovf& fov, float* m) {
	float near_z = 0.03f, far_z = 10.f;
	float l = std::tan(fov.angleLeft) * near_z;
	float r = std::tan(fov.angleRight) * near_z;
	float b = std::tan(fov.angleDown) * near_z;
	float t = std::tan(fov.angleUp) * near_z;
	std::fill(m, m + 16, 0.f);
	m[0] = 2.f * near_z / (r - l);
	m[5] = 2.f * near_z / (t - b);
	m[8] = (r + l) / (r - l);
	m[9] = (t + b) / (t - b);
	m[10] = -(far_z + near_z) / (far_z - near_z);
	m[11] = -1.f;
	m[14] = -(2.f * far_z * near_z) / (far_z - near_z);
}

static void view_matrix(const XrPosef& pose, float* m) {
	/* Invert the eye pose: conjugate quaternion rotation followed by the
	 * corresponding translated origin. */
	float x = -pose.orientation.x, y = -pose.orientation.y;
	float z = -pose.orientation.z, w = pose.orientation.w;
	float r00 = 1.f - 2.f * (y * y + z * z);
	float r01 = 2.f * (x * y - z * w);
	float r02 = 2.f * (x * z + y * w);
	float r10 = 2.f * (x * y + z * w);
	float r11 = 1.f - 2.f * (x * x + z * z);
	float r12 = 2.f * (y * z - x * w);
	float r20 = 2.f * (x * z - y * w);
	float r21 = 2.f * (y * z + x * w);
	float r22 = 1.f - 2.f * (x * x + y * y);
	float px = pose.position.x, py = pose.position.y, pz = pose.position.z;
	float tx = -(r00 * px + r01 * py + r02 * pz);
	float ty = -(r10 * px + r11 * py + r12 * pz);
	float tz = -(r20 * px + r21 * py + r22 * pz);
	float out[16] = {r00, r10, r20, 0.f, r01, r11, r21, 0.f,
					 r02, r12, r22, 0.f, tx, ty, tz, 1.f};
	std::copy(out, out + 16, m);
}

static void draw_hand(const std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT>& j,
					  int hand) {
	static constexpr std::pair<int, int> bones[] = {
		{XR_HAND_JOINT_WRIST_EXT, XR_HAND_JOINT_PALM_EXT},
		{XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_THUMB_METACARPAL_EXT},
		{XR_HAND_JOINT_THUMB_METACARPAL_EXT, XR_HAND_JOINT_THUMB_PROXIMAL_EXT},
		{XR_HAND_JOINT_THUMB_PROXIMAL_EXT, XR_HAND_JOINT_THUMB_DISTAL_EXT},
		{XR_HAND_JOINT_THUMB_DISTAL_EXT, XR_HAND_JOINT_THUMB_TIP_EXT},
		{XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_INDEX_METACARPAL_EXT},
		{XR_HAND_JOINT_INDEX_METACARPAL_EXT, XR_HAND_JOINT_INDEX_PROXIMAL_EXT},
		{XR_HAND_JOINT_INDEX_PROXIMAL_EXT, XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT},
		{XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, XR_HAND_JOINT_INDEX_DISTAL_EXT},
		{XR_HAND_JOINT_INDEX_DISTAL_EXT, XR_HAND_JOINT_INDEX_TIP_EXT},
		{XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_MIDDLE_METACARPAL_EXT},
		{XR_HAND_JOINT_MIDDLE_METACARPAL_EXT, XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT},
		{XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT, XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT},
		{XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, XR_HAND_JOINT_MIDDLE_DISTAL_EXT},
		{XR_HAND_JOINT_MIDDLE_DISTAL_EXT, XR_HAND_JOINT_MIDDLE_TIP_EXT},
		{XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_RING_METACARPAL_EXT},
		{XR_HAND_JOINT_RING_METACARPAL_EXT, XR_HAND_JOINT_RING_PROXIMAL_EXT},
		{XR_HAND_JOINT_RING_PROXIMAL_EXT, XR_HAND_JOINT_RING_INTERMEDIATE_EXT},
		{XR_HAND_JOINT_RING_INTERMEDIATE_EXT, XR_HAND_JOINT_RING_DISTAL_EXT},
		{XR_HAND_JOINT_RING_DISTAL_EXT, XR_HAND_JOINT_RING_TIP_EXT},
		{XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_LITTLE_METACARPAL_EXT},
		{XR_HAND_JOINT_LITTLE_METACARPAL_EXT, XR_HAND_JOINT_LITTLE_PROXIMAL_EXT},
		{XR_HAND_JOINT_LITTLE_PROXIMAL_EXT, XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT},
		{XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, XR_HAND_JOINT_LITTLE_DISTAL_EXT},
		{XR_HAND_JOINT_LITTLE_DISTAL_EXT, XR_HAND_JOINT_LITTLE_TIP_EXT},
	};
	if (hand == 0)
		glColor4f(0.10f, 0.78f, 0.92f, 1.f);
	else
		glColor4f(0.96f, 0.58f, 0.16f, 1.f);
	for (const auto& bone : bones) {
		const auto& a = j[(size_t)bone.first];
		const auto& b = j[(size_t)bone.second];
		if (!(a.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) ||
			!(b.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
			continue;
		float ra = std::clamp(a.radius * 0.72f, 0.0035f, 0.012f);
		float rb = std::clamp(b.radius * 0.72f, 0.003f, 0.011f);
		draw_bone(joint_pos(a), joint_pos(b), ra, rb);
	}
	for (const auto& joint : j) {
		if (!(joint.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
			continue;
		float radius = std::clamp(joint.radius * 0.78f, 0.0035f, 0.013f);
		draw_sphere(joint_pos(joint), radius);
	}
	const auto& palm = j[XR_HAND_JOINT_PALM_EXT];
	if (palm.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
		draw_sphere(joint_pos(palm), std::max(0.026f, palm.radius * 1.3f));
}

static bool hand_visual_setup(HandVisual* h, XrInstance instance, XrSystemId system,
						  XrSession session, int32_t w, int32_t height) {
	if (!h)
		return false;
	XrSystemHandTrackingPropertiesEXT hand_props{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
	XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};
	props.next = &hand_props;
	if (XR_FAILED(xrGetSystemProperties(instance, system, &props)) ||
		hand_props.supportsHandTracking != XR_TRUE) {
		std::fprintf(stderr, "xeneva-xr-view: system does not support hand joints\n");
		return false;
	}
	if (XR_FAILED(xrGetInstanceProcAddr(instance, "xrCreateHandTrackerEXT",
									   reinterpret_cast<PFN_xrVoidFunction*>(&h->create))) ||
		XR_FAILED(xrGetInstanceProcAddr(instance, "xrDestroyHandTrackerEXT",
									   reinterpret_cast<PFN_xrVoidFunction*>(&h->destroy))) ||
		XR_FAILED(xrGetInstanceProcAddr(instance, "xrLocateHandJointsEXT",
									   reinterpret_cast<PFN_xrVoidFunction*>(&h->locate))))
		return false;
	for (int hand = 0; hand < 2; ++hand) {
		XrHandTrackerCreateInfoEXT create{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
		create.hand = hand == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
		create.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
		if (XR_FAILED(h->create(session, &create, &h->tracker[hand])))
			h->tracker[hand] = XR_NULL_HANDLE;
	}
	if (h->tracker[0] == XR_NULL_HANDLE && h->tracker[1] == XR_NULL_HANDLE)
		return false;
	/* The joint mesh is simple geometry and does not need a pair of native
	 * HMD-resolution render targets.  On WiVRn/Mesa, allocating two more
	 * 2198x2418 swapchains after the desktop swapchain can exhaust the GL
	 * path and crash inside xrCreateSwapchain. */
	int32_t render_w = std::min(w, 1024);
	int32_t render_h = std::min(height, 1024);
	for (int eye = 0; eye < 2; ++eye) {
		h->eye[eye] = make_swapchain(session, render_w, render_h);
		glGenFramebuffers(1, &h->fbo[eye]);
		glGenRenderbuffers(1, &h->depth[eye]);
		glBindRenderbuffer(GL_RENDERBUFFER, h->depth[eye]);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, render_w, render_h);
	}
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	h->ready = true;
	std::fprintf(stderr, "xeneva-xr-view: standard joint hand mesh on (left + right)\n");
	return true;
}

static bool hand_visual_render(HandVisual* h, XrSession session, XrSpace space, XrTime time,
						   const VisualInputs& pointers,
						   XrCompositionLayerProjection* layer,
						   std::array<XrCompositionLayerProjectionView, 2>* projection_views) {
	if (!h || !h->ready || !layer || !projection_views)
		return false;
	bool any = false;
	for (int hand = 0; hand < 2; ++hand) {
		h->active[hand] = false;
		if (h->tracker[hand] == XR_NULL_HANDLE)
			continue;
		XrHandJointsLocateInfoEXT locate_info{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
		locate_info.baseSpace = space;
		locate_info.time = time;
		XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
		locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
		locations.jointLocations = h->joints[hand].data();
		if (XR_FAILED(h->locate(h->tracker[hand], &locate_info, &locations)) ||
			locations.isActive != XR_TRUE) {
			for (auto& filter : h->filters[hand]) {
				filter.xy.reset();
				filter.z.reset();
			}
			continue;
		}
		h->active[hand] = true;
		any = true;
		for (size_t i = 0; i < h->joints[hand].size(); ++i) {
			auto& joint = h->joints[hand][i];
			if (!(joint.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
				h->filters[hand][i].xy.reset();
				h->filters[hand][i].z.reset();
				continue;
			}
			auto xy = h->filters[hand][i].xy.update(joint.pose.position.x,
												 joint.pose.position.y, (int64_t)time);
			auto z = h->filters[hand][i].z.update(joint.pose.position.z, 0.f, (int64_t)time);
			/* Reject implausible behind-head and beyond-panel samples instead of
			 * letting a transient tracking spike paint over the guest. */
			if (z.x > -0.05f || z.x < -2.1f) {
				joint.locationFlags &= ~XR_SPACE_LOCATION_POSITION_VALID_BIT;
				continue;
			}
			joint.pose.position.x = xy.x;
			joint.pose.position.y = xy.y;
			joint.pose.position.z = z.x;
		}
	}
	if (!any && !pointers.handAim.valid && !pointers.controllerAim[0].valid &&
		!pointers.controllerAim[1].valid)
		return false;

	std::array<XrView, 2> views = {XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
	XrViewLocateInfo view_info{XR_TYPE_VIEW_LOCATE_INFO};
	view_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	view_info.displayTime = time;
	view_info.space = space;
	XrViewState view_state{XR_TYPE_VIEW_STATE};
	uint32_t view_count = 0;
	if (XR_FAILED(xrLocateViews(session, &view_info, &view_state, 2, &view_count, views.data())) ||
		view_count != 2 ||
		(view_state.viewStateFlags & (XR_VIEW_STATE_POSITION_VALID_BIT |
									  XR_VIEW_STATE_ORIENTATION_VALID_BIT)) !=
			(XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT))
		return false;

	for (int eye = 0; eye < 2; ++eye) {
		XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
		uint32_t image = 0;
		xr_check(xrAcquireSwapchainImage(h->eye[eye].handle, &acquire, &image),
				 "acquire hand image");
		XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
		wait.timeout = XR_INFINITE_DURATION;
		xr_check(xrWaitSwapchainImage(h->eye[eye].handle, &wait), "wait hand image");
		glBindFramebuffer(GL_FRAMEBUFFER, h->fbo[eye]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
						   h->eye[eye].images[image].image, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
							h->depth[eye]);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			static bool warned = false;
			if (!warned) {
				std::fprintf(stderr, "xeneva-xr-view: hand framebuffer incomplete\n");
				warned = true;
			}
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
			xrReleaseSwapchainImage(h->eye[eye].handle, &release);
			return false;
		}
		glViewport(0, 0, h->eye[eye].w, h->eye[eye].h);
		glClearColor(0.f, 0.f, 0.f, 0.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDisable(GL_CULL_FACE);
		glDisable(GL_BLEND);
		glDisable(GL_TEXTURE_2D);
		float projection[16], view[16];
		projection_matrix(views[eye].fov, projection);
		view_matrix(views[eye].pose, view);
		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf(projection);
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf(view);
		for (int hand = 0; hand < 2; ++hand) {
			if (h->active[hand])
				draw_hand(h->joints[hand], hand);
		}
		draw_pointer(pointers.handAim, false, 1);
		for (int side = 0; side < 2; ++side)
			draw_pointer(pointers.controllerAim[side], true, side);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		xr_check(xrReleaseSwapchainImage(h->eye[eye].handle, &release), "release hand image");

		auto& pv = (*projection_views)[eye];
		pv = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
		pv.pose = views[eye].pose;
		pv.fov = views[eye].fov;
		pv.subImage.swapchain = h->eye[eye].handle;
		pv.subImage.imageRect.extent = {h->eye[eye].w, h->eye[eye].h};
	}
	*layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	layer->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
						XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
	layer->space = space;
	layer->viewCount = 2;
	layer->views = projection_views->data();
	return true;
}

static void hand_visual_shutdown(HandVisual* h) {
	if (!h)
		return;
	for (int hand = 0; hand < 2; ++hand) {
		if (h->tracker[hand] != XR_NULL_HANDLE && h->destroy)
			h->destroy(h->tracker[hand]);
		h->tracker[hand] = XR_NULL_HANDLE;
	}
	for (int eye = 0; eye < 2; ++eye) {
		if (h->fbo[eye])
			glDeleteFramebuffers(1, &h->fbo[eye]);
		if (h->depth[eye])
			glDeleteRenderbuffers(1, &h->depth[eye]);
		if (h->eye[eye].handle != XR_NULL_HANDLE)
			xrDestroySwapchain(h->eye[eye].handle);
	}
	h->ready = false;
}
#else
struct HandVisual {};
static bool hand_visual_setup(HandVisual*, XrInstance, XrSystemId, XrSession, int32_t, int32_t) {
	return false;
}
static bool hand_visual_render(HandVisual*, XrSession, XrSpace, XrTime,
						   const VisualInputs&,
						   XrCompositionLayerProjection*,
						   std::array<XrCompositionLayerProjectionView, 2>*) {
	return false;
}
static void hand_visual_shutdown(HandVisual*) {}
#endif

/* Right-hand pointer + pinch click (--hands) and controller pointer +
 * trigger (--controllers). Hands ride EXT_hand_interaction action input
 * (aim pose + pinch float); controllers ride the standard touch/simple
 * profiles (aim pose + trigger float). One shared relative integrator
 * drives the guest cursor; controllers win while valid. Injected over
 * dbus like VNC input. */
struct Hands {
	XrActionSet set = XR_NULL_HANDLE;
	XrAction aim = XR_NULL_HANDLE;
	XrAction pinch = XR_NULL_HANDLE;
	XrSpace aimSpace = XR_NULL_HANDLE;
	XrPath handPath = XR_NULL_PATH;
	XrAction ctlAim = XR_NULL_HANDLE;
	XrAction leftCtlAim = XR_NULL_HANDLE;
	XrAction ctlTrigger = XR_NULL_HANDLE;
	XrSpace ctlSpace = XR_NULL_HANDLE;
	XrSpace leftCtlSpace = XR_NULL_HANDLE;
	XrPath leftHandPath = XR_NULL_PATH;
	bool handOn = false;
	bool ctlOn = false;
	bool ready = false;
	/* Shared pointer state. */
	float cur_x = 0.f, cur_y = 0.f;
	bool homed = false;
	float hand_lx = 0.f, hand_ly = 0.f;
	float hand_rx = 0.f, hand_ry = 0.f;
	bool hand_have = false;
	float ctl_lx = 0.f, ctl_ly = 0.f;
	float ctl_rx = 0.f, ctl_ry = 0.f;
	bool ctl_have = false;
	PointerFilter hand_filter{1.7f, 2.f};
	PointerFilter ctl_filter{1.7f, 2.f};
	int pinch_hold = 0;
	int ctl_hold = 0; /* frames to keep preferring controllers after loss */
	enum Src { SRC_NONE, SRC_HAND, SRC_CTL };
	Src pressed_by = SRC_NONE;
	Src active_src = SRC_NONE;
	bool pressed = false;
};

static constexpr uint32_t QEMU_MOUSE_LEFT = 0;

static bool hands_setup(XrInstance instance, XrSession session, Hands* h, bool want_hands,
						bool want_ctl) {
	if (!h || (!want_hands && !want_ctl))
		return false;
	/* Soft-fail everything: missing input support must never kill a working
	 * view session. */
	if (XR_FAILED(xrStringToPath(instance, "/user/hand/right", &h->handPath))) {
		std::fprintf(stderr, "xeneva-xr-view: hand path unknown\n");
		return false;
	}
	xrStringToPath(instance, "/user/hand/left", &h->leftHandPath);
	XrActionSetCreateInfo sci{XR_TYPE_ACTION_SET_CREATE_INFO};
	std::strcpy(sci.actionSetName, "pointer");
	std::strcpy(sci.localizedActionSetName, "guest pointer");
	sci.priority = 0;
	if (XR_FAILED(xrCreateActionSet(instance, &sci, &h->set))) {
		std::fprintf(stderr, "xeneva-xr-view: xrCreateActionSet failed\n");
		return false;
	}
	XrActionCreateInfo aci{XR_TYPE_ACTION_CREATE_INFO};
	aci.countSubactionPaths = 1;
	aci.subactionPaths = &h->handPath;
	if (want_hands) {
		XrPath profile = XR_NULL_PATH, aimIn = XR_NULL_PATH, pinchIn = XR_NULL_PATH;
		if (XR_FAILED(xrStringToPath(instance, "/interaction_profiles/ext/hand_interaction_ext",
									 &profile)) ||
			XR_FAILED(xrStringToPath(instance, "/user/hand/right/input/aim/pose", &aimIn)) ||
			XR_FAILED(xrStringToPath(instance, "/user/hand/right/input/pinch_ext/value",
									  &pinchIn))) {
			std::fprintf(stderr, "xeneva-xr-view: hand interaction profile unknown\n");
		} else {
			std::strcpy(aci.actionName, "aim");
			std::strcpy(aci.localizedActionName, "hand aim");
			aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
			bool ok = !XR_FAILED(xrCreateAction(h->set, &aci, &h->aim));
			std::strcpy(aci.actionName, "pinch");
			std::strcpy(aci.localizedActionName, "pinch click");
			aci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
			ok = ok && !XR_FAILED(xrCreateAction(h->set, &aci, &h->pinch));
			XrActionSuggestedBinding bindings[2] = {{h->aim, aimIn}, {h->pinch, pinchIn}};
			XrInteractionProfileSuggestedBinding sug{
				XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
			sug.interactionProfile = profile;
			sug.countSuggestedBindings = 2;
			sug.suggestedBindings = bindings;
			ok = ok && !XR_FAILED(xrSuggestInteractionProfileBindings(instance, &sug));
			if (ok) {
				h->handOn = true;
				std::fprintf(stderr, "xeneva-xr-view: right-hand tracking on (aim + pinch)\n");
			} else {
				std::fprintf(stderr, "xeneva-xr-view: hand bindings rejected by runtime\n");
			}
		}
	}
	if (want_ctl) {
		/* Controllers: same aim/trigger inputs on the standard profiles.
		 * Best-effort per profile; either one working is enough. */
		const char* profiles[] = {
			"/interaction_profiles/oculus/touch_controller",
			"/interaction_profiles/khr/simple_controller",
		};
		XrPath aimIn = XR_NULL_PATH, trigIn = XR_NULL_PATH, leftAimIn = XR_NULL_PATH;
		bool paths = !XR_FAILED(xrStringToPath(instance, "/user/hand/right/input/aim/pose",
											   &aimIn)) &&
					 !XR_FAILED(xrStringToPath(instance, "/user/hand/right/input/trigger/value",
											   &trigIn));
		if (paths) {
			std::strcpy(aci.actionName, "ctl_aim");
			std::strcpy(aci.localizedActionName, "controller aim");
			aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
			bool ok = !XR_FAILED(xrCreateAction(h->set, &aci, &h->ctlAim));
			std::strcpy(aci.actionName, "ctl_trigger");
			std::strcpy(aci.localizedActionName, "controller trigger");
			aci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
			ok = ok && !XR_FAILED(xrCreateAction(h->set, &aci, &h->ctlTrigger));
			if (h->leftHandPath != XR_NULL_PATH &&
				XR_SUCCEEDED(xrStringToPath(instance, "/user/hand/left/input/aim/pose",
											&leftAimIn))) {
				aci.subactionPaths = &h->leftHandPath;
				std::strcpy(aci.actionName, "left_ctl_aim");
				std::strcpy(aci.localizedActionName, "left controller aim");
				aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
				if (XR_FAILED(xrCreateAction(h->set, &aci, &h->leftCtlAim)))
					h->leftCtlAim = XR_NULL_HANDLE;
				aci.subactionPaths = &h->handPath;
			}
			XrActionSuggestedBinding bindings[3] = {{h->ctlAim, aimIn},
											{h->ctlTrigger, trigIn},
											{h->leftCtlAim, leftAimIn}};
			bool bound = false;
			for (const char* p : profiles) {
				XrPath prof = XR_NULL_PATH;
				if (XR_FAILED(xrStringToPath(instance, p, &prof)))
					continue;
				XrInteractionProfileSuggestedBinding sug{
					XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
				sug.interactionProfile = prof;
				sug.countSuggestedBindings = h->leftCtlAim != XR_NULL_HANDLE ? 3 : 2;
				sug.suggestedBindings = bindings;
				if (!XR_FAILED(xrSuggestInteractionProfileBindings(instance, &sug))) {
					bound = true;
					std::fprintf(stderr, "xeneva-xr-view: controller bindings on %s\n", p);
					break;
				}
			}
			h->ctlOn = ok && bound;
			if (!h->ctlOn)
				std::fprintf(stderr, "xeneva-xr-view: controller bindings rejected\n");
		}
	}
	if (!h->handOn && !h->ctlOn)
		return false;
	XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
	attach.countActionSets = 1;
	attach.actionSets = &h->set;
	if (XR_FAILED(xrAttachSessionActionSets(session, &attach))) {
		std::fprintf(stderr, "xeneva-xr-view: xrAttachSessionActionSets failed\n");
		return false;
	}
	XrActionSpaceCreateInfo sci2{XR_TYPE_ACTION_SPACE_CREATE_INFO};
	sci2.subactionPath = h->handPath;
	sci2.poseInActionSpace.orientation.w = 1.f;
	if (h->handOn) {
		sci2.action = h->aim;
		if (XR_FAILED(xrCreateActionSpace(session, &sci2, &h->aimSpace))) {
			std::fprintf(stderr, "xeneva-xr-view: aim space failed\n");
			h->handOn = false;
		}
	}
	if (h->ctlOn) {
		sci2.action = h->ctlAim;
		if (XR_FAILED(xrCreateActionSpace(session, &sci2, &h->ctlSpace))) {
			std::fprintf(stderr, "xeneva-xr-view: controller aim space failed\n");
			h->ctlOn = false;
		}
		if (h->leftCtlAim != XR_NULL_HANDLE) {
			sci2.action = h->leftCtlAim;
			sci2.subactionPath = h->leftHandPath;
			if (XR_FAILED(xrCreateActionSpace(session, &sci2, &h->leftCtlSpace)))
				h->leftCtlSpace = XR_NULL_HANDLE;
		}
	}
	if (!h->handOn && !h->ctlOn)
		return false;
	h->ready = true;
	return true;
}

static void hands_shutdown(Hands* h) {
	if (h && h->ready) {
		if (h->pressed)
			qemu_mouse_button(QEMU_MOUSE_LEFT, false);
		if (h->aimSpace != XR_NULL_HANDLE)
			xrDestroySpace(h->aimSpace);
		if (h->ctlSpace != XR_NULL_HANDLE)
			xrDestroySpace(h->ctlSpace);
		if (h->leftCtlSpace != XR_NULL_HANDLE)
			xrDestroySpace(h->leftCtlSpace);
		h->aimSpace = XR_NULL_HANDLE;
		h->ctlSpace = XR_NULL_HANDLE;
		h->ready = false;
		h->pressed = false;
	}
}

static PointerVisual locate_pointer(XrSpace action_space, XrSpace render_space, XrTime time) {
	PointerVisual result;
	if (action_space == XR_NULL_HANDLE)
		return result;
	XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
	if (XR_FAILED(xrLocateSpace(action_space, render_space, time, &location)))
		return result;
	constexpr XrSpaceLocationFlags required = XR_SPACE_LOCATION_POSITION_VALID_BIT |
		XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	if ((location.locationFlags & required) != required)
		return result;
	result.pose = location.pose;
	result.valid = true;
	return result;
}

static VisualInputs hands_visual_inputs(const Hands* h, XrSpace render_space, XrTime time) {
	VisualInputs visuals;
	if (!h || !h->ready)
		return visuals;
	if (h->handOn)
		visuals.handAim = locate_pointer(h->aimSpace, render_space, time);
	if (h->ctlOn) {
		visuals.controllerAim[0] = locate_pointer(h->leftCtlSpace, render_space, time);
		visuals.controllerAim[1] = locate_pointer(h->ctlSpace, render_space, time);
	}
	return visuals;
}

/* One frame of hand input. eye_w/img_h describe the per-eye texture region
 * (matches the quad composition below); cap carries guest pixel size. */
static void hands_update(Hands* h, const Capture& cap, XrSession session, XrSpace view,
						 XrTime time, int eye_w, int img_h) {
	if (!h || !h->ready)
		return;
	XrActiveActionSet active{};
	active.actionSet = h->set;
	XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
	sync.countActiveActionSets = 1;
	sync.activeActionSets = &active;
	XrResult syncrc = xrSyncActions(session, &sync);
	if (XR_FAILED(syncrc))
		return;
	/* Sample the hand: pinch float (sustained press rejects motion spikes)
	 * plus aim position. */
	bool hand_ok = false;
	float hand_px = 0.f, hand_py = 0.f;
	bool hand_click = h->pressed && h->pressed_by == Hands::SRC_HAND;
	XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
	get.subactionPath = h->handPath;
	get.action = h->pinch;
	XrActionStateFloat pinch{XR_TYPE_ACTION_STATE_FLOAT};
	if (h->handOn && XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &pinch)) &&
		pinch.isActive) {
		/* Pinch with hysteresis plus sustain: deliberate pinches hold for
		 * many frames, motion artifacts spike for one or two. */
		if (pinch.currentState > 0.06f) {
			if (++h->pinch_hold >= 4)
				hand_click = true;
		} else if (pinch.currentState < 0.02f) {
			hand_click = false;
			h->pinch_hold = 0;
		}
	} else {
		hand_click = false;
		h->pinch_hold = 0;
	}
	/* Aim pose gives the ray directly in VIEW space. */
	if (h->handOn) {
		XrSpaceLocation aim{XR_TYPE_SPACE_LOCATION};
		XrResult locrc = xrLocateSpace(h->aimSpace, view, time, &aim);
		if (XR_SUCCEEDED(locrc) &&
			(aim.locationFlags & XrSpaceLocationFlags(XR_SPACE_LOCATION_POSITION_VALID_BIT))) {
			hand_ok = true;
			hand_px = aim.pose.position.x;
			hand_py = aim.pose.position.y;
		}
	}
	/* Position map, not ray: this runtime's aim -Z sits ~40-60 deg above
	 * where the user points, so rays always overshoot the panel. The aim
	 * POSITION tracks 1:1, so the hand works like a floating touchpad:
	 * move right/left/up/down in front of you to move the cursor. */
	/* Sample the controller: trigger float plus aim position. */
	bool ctl_ok = false;
	float ctl_px = 0.f, ctl_py = 0.f;
	bool ctl_click = h->pressed && h->pressed_by == Hands::SRC_CTL;
	if (h->ctlOn) {
		XrActionStateGetInfo getc{XR_TYPE_ACTION_STATE_GET_INFO};
		getc.subactionPath = h->handPath;
		getc.action = h->ctlTrigger;
		XrActionStateFloat trig{XR_TYPE_ACTION_STATE_FLOAT};
		if (XR_SUCCEEDED(xrGetActionStateFloat(session, &getc, &trig)) && trig.isActive) {
			if (trig.currentState > 0.5f)
				ctl_click = true;
			else if (trig.currentState < 0.25f)
				ctl_click = false;
		} else {
			ctl_click = false;
		}
		XrSpaceLocation caim{XR_TYPE_SPACE_LOCATION};
		XrResult clocrc = xrLocateSpace(h->ctlSpace, view, time, &caim);
		if (XR_SUCCEEDED(clocrc) &&
			(caim.locationFlags & XrSpaceLocationFlags(XR_SPACE_LOCATION_POSITION_VALID_BIT))) {
			ctl_ok = true;
			ctl_px = caim.pose.position.x;
			ctl_py = caim.pose.position.y;
		}
	}
	/* Arbitrate: controllers win while valid, with a short hold so a
	 * flickering pose doesn't flap the cursor between sources. */
	if (ctl_ok)
		h->ctl_hold = 10;
	else {
		h->ctl_filter.reset();
		h->ctl_have = false;
		if (h->ctl_hold > 0)
			h->ctl_hold--;
	}
	if (!hand_ok) {
		h->hand_filter.reset();
		h->hand_have = false;
	}
	bool use_ctl = h->ctlOn && (ctl_ok || h->ctl_hold > 0);
	bool use_hand = !use_ctl && h->handOn && hand_ok;
	if (!use_ctl && !use_hand) {
		if (h->pressed) {
			qemu_mouse_button(QEMU_MOUSE_LEFT, false);
			h->pressed = false;
			h->pressed_by = Hands::SRC_NONE;
		}
		h->active_src = Hands::SRC_NONE;
		return;
	}
	/* Click edges follow the active source; a source switch releases first. */
	bool want_click = use_ctl ? ctl_click : hand_click;
	Hands::Src want_src = use_ctl ? Hands::SRC_CTL : Hands::SRC_HAND;
	if (want_src != h->active_src) {
		/* Never carry a filtered velocity or stale origin across devices. */
		if (want_src == Hands::SRC_CTL) {
			h->ctl_filter.reset();
			h->ctl_have = false;
		} else {
			h->hand_filter.reset();
			h->hand_have = false;
		}
		h->active_src = want_src;
	}
	if (want_click && h->pressed && h->pressed_by != want_src) {
		qemu_mouse_button(QEMU_MOUSE_LEFT, false);
		h->pressed = false;
		h->pressed_by = Hands::SRC_NONE;
	}
	if (want_click && !h->pressed) {
		qemu_mouse_button(QEMU_MOUSE_LEFT, true);
		h->pressed = true;
		h->pressed_by = want_src;
	} else if (!want_click && h->pressed) {
		qemu_mouse_button(QEMU_MOUSE_LEFT, false);
		h->pressed = false;
		h->pressed_by = Hands::SRC_NONE;
	}
	/* Keep controller ownership briefly through a pose dropout, but do not
	 * integrate the default (0,0) pose or hand control will jump. */
	if ((use_ctl && !ctl_ok) || (use_hand && !hand_ok))
		return;
	/* Relative (trackpad-style) pointer: absolute boxes cannot fit unknown
	 * hand travel, so integrate scaled deltas instead. Fast flicks act as
	 * a clutch (reposition without moving the cursor); sub-pixel noise is
	 * dropped; deltas are smoothed. */
	(void)eye_w;
	(void)img_h;
	if (cap.w <= 0 || cap.h <= 0)
		return;
	if (!h->homed) {
		h->cur_x = (float)cap.w * 0.5f;
		h->cur_y = (float)cap.h * 0.5f;
		h->homed = true;
	}
	float px = use_ctl ? ctl_px : hand_px;
	float py = use_ctl ? ctl_py : hand_py;
	float& last_x = use_ctl ? h->ctl_lx : h->hand_lx;
	float& last_y = use_ctl ? h->ctl_ly : h->hand_ly;
	float& raw_x = use_ctl ? h->ctl_rx : h->hand_rx;
	float& raw_y = use_ctl ? h->ctl_ry : h->hand_ry;
	bool& have = use_ctl ? h->ctl_have : h->hand_have;
	PointerFilter& filter = use_ctl ? h->ctl_filter : h->hand_filter;
	PointerFilter::Point filtered = filter.update(px, py, (int64_t)time);
	if (!have) {
		last_x = filtered.x;
		last_y = filtered.y;
		raw_x = px;
		raw_y = py;
		have = true;
		return;
	}
	/* Pinching shifts hand aim enough to need the lower gain; controller
	 * motion needs more range while retaining the same jitter filtering. */
	float input_gain = cap.hands_gain * (use_ctl ? 0.5f : 0.25f);
	float raw_dx = (px - raw_x) * input_gain;
	float raw_dy = -(py - raw_y) * input_gain;
	raw_x = px;
	raw_y = py;
	float raw_step = std::sqrt(raw_dx * raw_dx + raw_dy * raw_dy);
	float dx = (filtered.x - last_x) * input_gain;
	float dy = -(filtered.y - last_y) * input_gain; /* XR y up, pixels y down */
	if (raw_step > 300.f) {
		filter.reset();
		have = false;
		return; /* flick = clutch, reposition silently */
	}
	float step = std::sqrt(dx * dx + dy * dy);
	constexpr float deadzone_px = 10.f;
	if (step <= deadzone_px)
		return; /* hold the cursor through small tracking fluctuations */
	/* Keep the deadzone as an elastic offset: a slow deliberate movement
	 * crosses it once, then advances smoothly instead of jumping 10 pixels. */
	float fraction = (step - deadzone_px) / step;
	dx *= fraction;
	dy *= fraction;
	last_x += (filtered.x - last_x) * fraction;
	last_y += (filtered.y - last_y) * fraction;
	h->cur_x += dx;
	h->cur_y += dy;
	if (h->cur_x < 0.f)
		h->cur_x = 0.f;
	if (h->cur_y < 0.f)
		h->cur_y = 0.f;
	if (h->cur_x > (float)(cap.w - 1))
		h->cur_x = (float)(cap.w - 1);
	if (h->cur_y > (float)(cap.h - 1))
		h->cur_y = (float)(cap.h - 1);
	qemu_mouse_abs((uint32_t)h->cur_x, (uint32_t)h->cur_y);
}

static void usage() {	std::fprintf(stderr,
				 "usage: xeneva-xr-view [capture] [output]\n"
				 "  capture:  --x11 [--window SUBSTR] | --ppm FILE | --pattern [--size WxH]\n"
				 "  output:   --desktop [--sbs|--anaglyph] [--ipd-px N]   (laptop, no OpenXR)\n"
				 "            (default) OpenXR quad; needs WiVRn + headset\n"
				 "  --stereo (default)  guest scanout is side-by-side L|R; each eye\n"
				 "                      gets its own half (ocular)\n"
				 "  --mono              guest scanout is 2D; show full frame to both eyes\n"
				 "  --filter MODE       native-eye upscale: nearest (default) or linear\n"
				 "  --hands             right-hand aim pointer + pinch click into the\n"
				 "                      guest; also shows both tracked hands\n"
				 "  --hand-mesh         show both tracked hands without hand pointer input\n"
				 "  --no-hand-mesh      disable the hand visual enabled by --hands\n"
				 "  --controllers       same via the right controller (aim + trigger),\n"
				 "                      takes over while valid\n"
				 "  desktop keys: [ ] IPD, s SBS/anaglyph, q quit\n"
				 "  --egl [--dbus ADDR]  steal QEMU scanout over dbus (true framebuffer)\n"
				 "  QEMU: Scripts/Linux/build_and_run_qemu.sh --xr-demo\n");
}

static void draw_textured_eye(float x0, float y0, float x1, float y1, float u_shift) {
	/* Desktop preview only (the HMD path uses OpenXR quads): capture
	 * buffers are top-row-first and upload unflipped, so v=1 (last
	 * uploaded row = screen bottom) belongs at the bottom vertex
	 * under the y-up orthographic projection. */
	glBegin(GL_QUADS);
	glTexCoord2f(u_shift, 1.f);
	glVertex2f(x0, y0);
	glTexCoord2f(1.f + u_shift, 1.f);
	glVertex2f(x1, y0);
	glTexCoord2f(1.f + u_shift, 0.f);
	glVertex2f(x1, y1);
	glTexCoord2f(u_shift, 0.f);
	glVertex2f(x0, y1);
	glEnd();
}

static int run_desktop(Capture* cap, bool anaglyph, int ipd_px) {
	Display* dpy = XOpenDisplay(nullptr);
	if (!dpy)
		die("XOpenDisplay failed");
	int screen = DefaultScreen(dpy);
	int vis_attr[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
					  GLX_BLUE_SIZE, 8, None};
	XVisualInfo* vis = glXChooseVisual(dpy, screen, vis_attr);
	if (!vis)
		die("glXChooseVisual failed");
	Colormap cmap = XCreateColormap(dpy, RootWindow(dpy, screen), vis->visual, AllocNone);
	XSetWindowAttributes swa;
	swa.colormap = cmap;
	swa.event_mask = KeyPressMask | StructureNotifyMask | ExposureMask;
	capture_frame(cap);
	int win_w = anaglyph ? cap->w : cap->w * 2;
	int win_h = cap->h;
	if (win_w < 320)
		win_w = 640;
	if (win_h < 240)
		win_h = 360;
	Window win = XCreateWindow(dpy, RootWindow(dpy, screen), 64, 64, win_w, win_h, 0, vis->depth,
							   InputOutput, vis->visual, CWColormap | CWEventMask, &swa);
	XStoreName(dpy, win, "xeneva-xr-view desktop (ocular preview)");
	XMapWindow(dpy, win);
	cap->exclude = win;
	if (cap->win == win)
		cap->win = 0;
	GLXContext ctx = glXCreateContext(dpy, vis, nullptr, True);
	if (!ctx || !glXMakeCurrent(dpy, win, ctx))
		die("GLX context failed");
	XFree(vis);

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	int tex_w = 0, tex_h = 0;

	Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);

	bool running = true;
	while (running) {
		while (XPending(dpy)) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == wm_delete)
				running = false;
			if (ev.type == ConfigureNotify) {
				win_w = ev.xconfigure.width;
				win_h = ev.xconfigure.height;
			}
			if (ev.type == KeyPress) {
				KeySym ks = XLookupKeysym(&ev.xkey, 0);
				if (ks == XK_q || ks == XK_Escape)
					running = false;
				else if (ks == XK_bracketleft)
					ipd_px = std::max(0, ipd_px - 1);
				else if (ks == XK_bracketright)
					ipd_px += 1;
				else if (ks == XK_s)
					anaglyph = !anaglyph;
			}
		}
		capture_frame(cap);
		if (cap->w != tex_w || cap->h != tex_h) {
			tex_w = cap->w;
			tex_h = cap->h;
			glBindTexture(GL_TEXTURE_2D, tex);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_w, tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
						 cap->rgba.empty() ? nullptr : cap->rgba.data());
		} else if (!cap->rgba.empty()) {
			glBindTexture(GL_TEXTURE_2D, tex);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_w, tex_h, GL_RGBA, GL_UNSIGNED_BYTE,
							cap->rgba.data());
		}
		float du = (tex_w > 0) ? (float)ipd_px / (float)tex_w : 0.f;
		glViewport(0, 0, win_w, win_h);
		glClearColor(0.05f, 0.05f, 0.07f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, win_w, 0, win_h, -1, 1);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, tex);
		if (anaglyph) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE);
			glColor3f(1.f, 0.f, 0.f);
			draw_textured_eye(0, 0, (float)win_w, (float)win_h, du);
			glColor3f(0.f, 1.f, 1.f);
			draw_textured_eye(0, 0, (float)win_w, (float)win_h, -du);
			glDisable(GL_BLEND);
			glColor3f(1.f, 1.f, 1.f);
		} else {
			glColor3f(1.f, 1.f, 1.f);
			int mid = win_w / 2;
			draw_textured_eye(0, 0, (float)mid, (float)win_h, du);
			draw_textured_eye((float)mid, 0, (float)win_w, (float)win_h, -du);
		glDisable(GL_TEXTURE_2D);
		glColor3f(1.f, 1.f, 1.f);
		}
		glXSwapBuffers(dpy, win);
	}
	glDeleteTextures(1, &tex);
	glXMakeCurrent(dpy, None, nullptr);
	glXDestroyContext(dpy, ctx);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}

int main(int argc, char** argv) {
	Capture cap;
	cap.mode = Capture::X11;
	bool desktop = false;
	bool anaglyph = false;
	int ipd_px = 12;
	for (int i = 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
			usage();
			return 0;
		} else if (!std::strcmp(argv[i], "--x11")) {
			cap.mode = Capture::X11;
		} else if (!std::strcmp(argv[i], "--pattern")) {
			cap.mode = Capture::Pattern;
		} else if (!std::strcmp(argv[i], "--ppm") && i + 1 < argc) {
			cap.mode = Capture::Ppm;
			cap.ppm_path = argv[++i];
		} else if (!std::strcmp(argv[i], "--window") && i + 1 < argc) {
			cap.window_substr = argv[++i];
		} else if (!std::strcmp(argv[i], "--egl")) {
			cap.mode = Capture::Egl;
		} else if (!std::strcmp(argv[i], "--dbus") && i + 1 < argc) {
			cap.dbus_addr = argv[++i];
		} else if (!std::strcmp(argv[i], "--wid") && i + 1 < argc) {
			cap.mode = Capture::X11;
			cap.win = (Window)std::strtoul(argv[++i], nullptr, 0);
		} else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) {
			if (std::sscanf(argv[++i], "%dx%d", &cap.w, &cap.h) != 2)
				die("--size WxH");
		} else if (!std::strcmp(argv[i], "--desktop")) {
			desktop = true;
		} else if (!std::strcmp(argv[i], "--sbs")) {
			anaglyph = false;
		} else if (!std::strcmp(argv[i], "--anaglyph")) {
			anaglyph = true;
		} else if (!std::strcmp(argv[i], "--stereo")) {
			cap.stereo = true;
		} else if (!std::strcmp(argv[i], "--mono")) {
			cap.stereo = false;
		} else if (!std::strcmp(argv[i], "--filter") && i + 1 < argc) {
			const char* mode = argv[++i];
			if (!std::strcmp(mode, "nearest"))
				cap.linear_filter = false;
			else if (!std::strcmp(mode, "linear"))
				cap.linear_filter = true;
			else
				die("--filter nearest|linear");
		} else if (!std::strcmp(argv[i], "--hands")) {
			cap.hands = true;
			cap.hand_mesh = true;
		} else if (!std::strcmp(argv[i], "--hand-mesh")) {
			cap.hand_mesh = true;
		} else if (!std::strcmp(argv[i], "--no-hand-mesh")) {
			cap.hand_mesh = false;
		} else if (!std::strcmp(argv[i], "--controllers")) {
			cap.controllers = true;
		} else if (!std::strcmp(argv[i], "--gain") && i + 1 < argc) {
			cap.hands_gain = (float)std::atof(argv[++i]);
			if (!(cap.hands_gain > 0.f))
				die("--gain N (pixels per meter)");
		} else if (!std::strcmp(argv[i], "--ipd-px") && i + 1 < argc) {
			ipd_px = std::atoi(argv[++i]);
		} else {
			usage();
			return 1;
		}
	}
	if (cap.mode == Capture::Egl) {
		if (!qemu_egl_connect(cap.dbus_addr.empty() ? nullptr : cap.dbus_addr.c_str()))
			std::fprintf(stderr, "xeneva-xr-view: EGL/dbus capture not connected yet\n");
	}
	if (desktop)
		return run_desktop(&cap, anaglyph, ipd_px);

	std::vector<const char*> exts{XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
	XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
	/* Hand interaction and skeletal tracking are independent optional
	 * features. Negotiate each instead of making either one fatal. */
#ifndef XR_EXT_HAND_INTERACTION_EXTENSION_NAME
	if (cap.hands) {
		std::fprintf(stderr, "xeneva-xr-view: --hands needs hand-interaction headers; ignoring\n");
		cap.hands = false;
	}
#else
	if (cap.hands && xr_has_extension(XR_EXT_HAND_INTERACTION_EXTENSION_NAME))
		exts.push_back(XR_EXT_HAND_INTERACTION_EXTENSION_NAME);
	else if (cap.hands) {
		std::fprintf(stderr, "xeneva-xr-view: runtime has no XR_EXT_hand_interaction; "
						 "hand pointer disabled\n");
		cap.hands = false;
	}
#endif
#ifndef XR_EXT_HAND_TRACKING_EXTENSION_NAME
	if (cap.hand_mesh) {
		std::fprintf(stderr, "xeneva-xr-view: OpenXR headers have no hand tracking; "
						 "hand mesh disabled\n");
		cap.hand_mesh = false;
	}
#else
	if (cap.hand_mesh && xr_has_extension(XR_EXT_HAND_TRACKING_EXTENSION_NAME))
		exts.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
	else if (cap.hand_mesh) {
		std::fprintf(stderr, "xeneva-xr-view: runtime has no XR_EXT_hand_tracking; "
						 "hand mesh disabled\n");
		cap.hand_mesh = false;
	}
#endif
	ici.enabledExtensionCount = (uint32_t)exts.size();
	std::strcpy(ici.applicationInfo.applicationName, "xeneva-xr-view");
	ici.applicationInfo.applicationVersion = 1;
	std::strcpy(ici.applicationInfo.engineName, "XenevaOS");
	ici.applicationInfo.engineVersion = 1;
	ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	ici.enabledExtensionNames = exts.data();
	XrInstance instance = XR_NULL_HANDLE;
	const char* rt = std::getenv("XR_RUNTIME_JSON");
	std::fprintf(stderr, "xeneva-xr-view: XR_RUNTIME_JSON=%s\n", rt ? rt : "(default active_runtime)");
	std::fprintf(stderr, "xeneva-xr-view: xrCreateInstance...\n");
	std::fflush(stderr);
	xr_check(xrCreateInstance(&ici, &instance), "xrCreateInstance");
	std::fprintf(stderr, "xeneva-xr-view: instance ok, xrGetSystem (waiting for headset)...\n");
	std::fflush(stderr);

	XrSystemGetInfo sysi{XR_TYPE_SYSTEM_GET_INFO};
	sysi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemId sys = XR_NULL_SYSTEM_ID;
	xr_check(xrGetSystem(instance, &sysi, &sys), "xrGetSystem (is WiVRn connected?)");
	std::fprintf(stderr, "xeneva-xr-view: system ok, creating session\n");
	std::fflush(stderr);

	/* Quest 2 fit: render at the runtime's recommended per-eye size
	 * (e.g. 1832x1920) instead of shrinking the swapchain to the guest
	 * capture; the guest frame is scaled on upload --axiss */
	int32_t recW = 0, recH = 0;
	{
		XrViewConfigurationView views[4];
		for (uint32_t i = 0; i < 4; i++)
			views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		uint32_t nviews = 0;
		if (xrEnumerateViewConfigurationViews(instance, sys,
											  XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
											  &nviews, nullptr) == XR_SUCCESS &&
			nviews > 0) {
			uint32_t get = nviews < 4 ? nviews : 4;
			if (xrEnumerateViewConfigurationViews(
					instance, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, get, &get,
					views) == XR_SUCCESS &&
				get > 0) {
				recW = (int32_t)views[0].recommendedImageRectWidth;
				recH = (int32_t)views[0].recommendedImageRectHeight;
			}
		}
		std::fprintf(stderr, "xeneva-xr-view: recommended eye size %dx%d\n", recW, recH);
		std::fflush(stderr);
	}

	PFN_xrGetOpenGLGraphicsRequirementsKHR pfnReq = nullptr;
	xr_check(xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR",
								   reinterpret_cast<PFN_xrVoidFunction*>(&pfnReq)),
			 "proc addr");
	XrGraphicsRequirementsOpenGLKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
	xr_check(pfnReq(instance, sys, &req), "gl requirements");

	GlxHeadless glx = make_glx();
	XrGraphicsBindingOpenGLXlibKHR bind{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
	bind.xDisplay = glx.dpy;
	bind.glxFBConfig = glx.fb_config;
	bind.glxContext = glx.ctx;
	bind.glxDrawable = glx.win;
	bind.visualid = glx.visual_id;

	XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
	sci.next = &bind;
	sci.systemId = sys;
	XrSession session = XR_NULL_HANDLE;
	xr_check(xrCreateSession(instance, &sci, &session), "xrCreateSession");

	XrReferenceSpaceCreateInfo spc{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	/* --axiss: Keep the panel attached to the headset's eye frame; LOCAL puts
	 * it at a fixed floor-relative height and makes it appear above the eyes. */
	spc.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	spc.poseInReferenceSpace.orientation.w = 1.f;
	XrSpace space = XR_NULL_HANDLE;
	xr_check(xrCreateReferenceSpace(session, &spc, &space), "view space");
	XrReferenceSpaceCreateInfo hand_spc{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	hand_spc.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	hand_spc.poseInReferenceSpace.orientation.w = 1.f;
	XrSpace hand_space = XR_NULL_HANDLE;
	xr_check(xrCreateReferenceSpace(session, &hand_spc, &hand_space), "hand local space");

	Hands hands;
	if ((cap.hands || cap.controllers) &&
		!hands_setup(instance, session, &hands, cap.hands, cap.controllers))
		cap.hands = cap.controllers = false;

	/* Do not size the swapchain from the temporary test chart shown while
	 * gpu0 is still booting. Wait for the first real D-Bus scanout so the
	 * OpenXR image and submitted rectangle exactly match the guest. WiVRn's
	 * current OpenGL path presents an offset sub-rectangle as black. */
	if (cap.mode == Capture::Egl) {
		uint32_t* pixels = nullptr;
		int capture_w = 0, capture_h = 0;
		for (int attempt = 0; attempt < 500; ++attempt) {
			if (qemu_egl_poll(&pixels, &capture_w, &capture_h) && pixels &&
				capture_w > 0 && capture_h > 0) {
				cap.w = capture_w;
				cap.h = capture_h;
				cap.rgba.assign(reinterpret_cast<uint8_t*>(pixels),
								reinterpret_cast<uint8_t*>(pixels) +
									(size_t)capture_w * capture_h * 4);
				break;
			}
			usleep(10000);
		}
	}
	if (cap.rgba.empty())
		capture_frame(&cap);
	int32_t eyeW = std::max(cap.stereo ? cap.w / 2 : cap.w, 256);
	int32_t scW = std::max(cap.w, 256);
	int32_t scH = std::max(cap.h, 256);
	Swapchain sc = make_swapchain(session, scW, scH);
	std::fprintf(stderr, "xeneva-xr-view: swapchain %dx%d format=0x%llx filter=%s capture=%s\n",
				 sc.w, sc.h, (unsigned long long)sc.format,
				 cap.linear_filter ? "linear" : "nearest",
				 cap.mode == Capture::X11 ? "x11" : cap.mode == Capture::Ppm ? "ppm" :
				 cap.mode == Capture::Egl ? "egl" : "pattern");
	HandVisual hand_visual;
	if (cap.hand_mesh &&
		!hand_visual_setup(&hand_visual, instance, sys, session, eyeW, scH)) {
		std::fprintf(stderr, "xeneva-xr-view: hand mesh unavailable; continuing without it\n");
		cap.hand_mesh = false;
	}

	bool running = true;
	bool session_running = false;
	uint64_t timing_frames = 0;
	while (running) {
		XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
		while (xrPollEvent(instance, &ev) == XR_SUCCESS) {
			if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
				auto* s = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
				std::fprintf(stderr, "xeneva-xr-view: session state %d\n", (int)s->state);
				if (s->state == XR_SESSION_STATE_READY) {
					XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
					begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
					xr_check(xrBeginSession(session, &begin), "xrBeginSession");
					session_running = true;
				} else if (s->state == XR_SESSION_STATE_STOPPING) {
					xrEndSession(session);
					session_running = false;
				} else if (s->state == XR_SESSION_STATE_EXITING ||
						   s->state == XR_SESSION_STATE_LOSS_PENDING)
					running = false;
			}
			ev = {XR_TYPE_EVENT_DATA_BUFFER};
		}
		if (!running)
			break;
		if (!session_running)
			continue;

		XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
		XrFrameState fs{XR_TYPE_FRAME_STATE};
		xr_check(xrWaitFrame(session, &wi, &fs), "wait frame");
		XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
		xr_check(xrBeginFrame(session, &bi), "begin frame");

		capture_frame(&cap);
		/* Firmware starts on ramfb at 1280x800, then the guest switches to
		 * gpu0 at 1024x768. Recreate on that transition so WiVRn receives a
		 * full swapchain image with no offset sub-rectangle. */
		if (cap.mode == Capture::Egl && cap.w > 0 && cap.h > 0 &&
			(cap.w != sc.w || cap.h != sc.h)) {
			glXMakeCurrent(glx.dpy, glx.win, glx.ctx);
			xrDestroySwapchain(sc.handle);
			sc = make_swapchain(session, cap.w, cap.h);
			std::fprintf(stderr, "xeneva-xr-view: scanout changed; swapchain recreated %dx%d\n",
					sc.w, sc.h);
		}
		if (fs.shouldRender) {
			if (!glXMakeCurrent(glx.dpy, glx.win, glx.ctx))
				die("glXMakeCurrent before swapchain upload failed");
			upload_rgba(sc, cap);
		}
		uint64_t upload_done_ns = host_monotonic_ns();

		XrCompositionLayerQuad quads[2] = {
			{XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD}};
		/* Ocular: with a side-by-side guest scanout each eye samples only
		 * its own half of the PLACED guest rect; with --mono both eyes
		 * sample the full placed frame. Guest halves are anamorphic (full
		 * frame squeezed into each half), so size the quad to the FULL
		 * frame aspect to unstretch. */
		int place_w = sc.up_w > 0 ? sc.up_w : sc.w;
		int place_h = sc.up_h > 0 ? sc.up_h : sc.h;
		int eye_w = (cap.stereo && place_w >= 2) ? place_w / 2 : place_w;
		if (cap.hands || cap.controllers)
			hands_update(&hands, cap, session, space, fs.predictedDisplayTime, eye_w,
						 sc.h);
	/* Panel keeps the GUEST frame aspect (e.g. 16:9 full SBS frame);
		 * the swapchain is at HMD res and only carries scaled pixels.
		 * Using the swapchain aspect here made a square panel with a
		 * ~2x squished image. */
		float aspect = (cap.w > 0 && cap.h > 0) ? (float)cap.w / (float)cap.h : 16.f / 9.f;
		for (int eye = 0; eye < 2; ++eye) {
			auto& quad = quads[eye];
			/* The guest scanout is an opaque desktop (XRGB DMA-BUF). Its alpha
			 * byte is undefined and must not blend the whole panel against the
			 * black XR environment. Deodhai's glass is already baked into RGB. */
			quad.layerFlags = 0;
			quad.space = space;
			quad.eyeVisibility = eye == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
			quad.subImage.swapchain = sc.handle;
			int half_x = sc.up_x + ((cap.stereo && eye == 1) ? eye_w : 0);
			int half_w = (cap.stereo && eye == 1) ? (place_w - eye_w) : eye_w;
			quad.subImage.imageRect.offset.x = half_x;
			quad.subImage.imageRect.offset.y = sc.up_y;
			quad.subImage.imageRect.extent.width = half_w;
			quad.subImage.imageRect.extent.height = place_h;
			quad.pose.orientation.w = 1.f;
			quad.pose.position = {eye == 0 ? -0.032f : 0.032f, 0.f, -2.2f};
			quad.size = {1.8f * aspect, 1.8f};
		}

		XrCompositionLayerProjection hand_layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
		std::array<XrCompositionLayerProjectionView, 2> hand_views;
		VisualInputs visuals = hands_visual_inputs(&hands, hand_space, fs.predictedDisplayTime);
		bool show_hands = fs.shouldRender && cap.hand_mesh &&
			hand_visual_render(&hand_visual, session, hand_space, fs.predictedDisplayTime,
							   visuals, &hand_layer, &hand_views);
		const XrCompositionLayerBaseHeader* layers[] = {
			reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[0]),
			reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[1]),
			reinterpret_cast<const XrCompositionLayerBaseHeader*>(&hand_layer)};
		XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
		ei.displayTime = fs.predictedDisplayTime;
		ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		if (fs.shouldRender) {
			ei.layerCount = show_hands ? 3 : 2;
			ei.layers = layers;
		}
		xr_check(xrEndFrame(session, &ei), "end frame");
		if ((++timing_frames % 60) == 0) {
			uint64_t cap_ns = qemu_egl_last_capture_ns();
			uint64_t submit_ns = host_monotonic_ns();
			long long cap_upload_us = cap_ns && upload_done_ns >= cap_ns
				? (long long)((upload_done_ns - cap_ns) / 1000) : -1;
			long long cap_submit_us = cap_ns && submit_ns >= cap_ns
				? (long long)((submit_ns - cap_ns) / 1000) : -1;
			std::fprintf(stderr,
				"xeneva-xr-view: host_timing frame=%llu qemu_capture_to_upload_us=%lld "
				"qemu_capture_to_submit_us=%lld guest_to_qemu=unmeasured "
				"wivrn_to_display=unmeasured\n",
				(unsigned long long)timing_frames, cap_upload_us, cap_submit_us);
		}
	}

	hand_visual_shutdown(&hand_visual);
	hands_shutdown(&hands);
	xrDestroySwapchain(sc.handle);
	xrDestroySpace(hand_space);
	xrDestroySpace(space);
	xrDestroySession(session);
	xrDestroyInstance(instance);
	glXMakeCurrent(glx.dpy, None, nullptr);
	glXDestroyContext(glx.dpy, glx.ctx);
	XDestroyWindow(glx.dpy, glx.win);
	XCloseDisplay(glx.dpy);
	return 0;
}
