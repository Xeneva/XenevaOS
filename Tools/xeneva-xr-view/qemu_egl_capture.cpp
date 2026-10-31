#include "qemu_egl_capture.h"

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GL/glx.h>
#include <gbm.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

static const char* k_listener_xml =
	"<node>"
	"  <interface name='org.qemu.Display1.Listener'>"
	"    <method name='Scanout'>"
	"      <arg type='u' name='width' direction='in'/>"
	"      <arg type='u' name='height' direction='in'/>"
	"      <arg type='u' name='stride' direction='in'/>"
	"      <arg type='u' name='pixman_format' direction='in'/>"
	"      <arg type='ay' name='data' direction='in'/>"
	"    </method>"
	"    <method name='Update'>"
	"      <arg type='i' name='x' direction='in'/>"
	"      <arg type='i' name='y' direction='in'/>"
	"      <arg type='i' name='width' direction='in'/>"
	"      <arg type='i' name='height' direction='in'/>"
	"      <arg type='u' name='stride' direction='in'/>"
	"      <arg type='u' name='pixman_format' direction='in'/>"
	"      <arg type='ay' name='data' direction='in'/>"
	"    </method>"
	"    <method name='ScanoutDMABUF'>"
	"      <arg type='h' name='dmabuf' direction='in'/>"
	"      <arg type='u' name='width' direction='in'/>"
	"      <arg type='u' name='height' direction='in'/>"
	"      <arg type='u' name='stride' direction='in'/>"
	"      <arg type='u' name='fourcc' direction='in'/>"
	"      <arg type='t' name='modifier' direction='in'/>"
	"      <arg type='b' name='y0_top' direction='in'/>"
	"    </method>"
	"    <method name='UpdateDMABUF'>"
	"      <arg type='i' name='x' direction='in'/>"
	"      <arg type='i' name='y' direction='in'/>"
	"      <arg type='i' name='width' direction='in'/>"
	"      <arg type='i' name='height' direction='in'/>"
	"    </method>"
	"    <method name='Disable'/>"
	"    <method name='MouseSet'>"
	"      <arg type='i' name='x' direction='in'/>"
	"      <arg type='i' name='y' direction='in'/>"
	"      <arg type='i' name='on' direction='in'/>"
	"    </method>"
	"    <method name='CursorDefine'>"
	"      <arg type='i' name='width' direction='in'/>"
	"      <arg type='i' name='height' direction='in'/>"
	"      <arg type='i' name='hot_x' direction='in'/>"
	"      <arg type='i' name='hot_y' direction='in'/>"
	"      <arg type='ay' name='data' direction='in'/>"
	"    </method>"
	"  </interface>"
	"</node>";

static std::mutex g_mu;
static std::vector<uint8_t> g_rgba;
static int g_w, g_h;
static bool g_have;
static uint64_t g_last_capture_ns;
/* Reverse dbus connection to QEMU (we are the client) plus the console we
 * registered on (scanout path). Injection goes over the session bus instead:
 * QEMU only hosts our Listener object on the reverse connection, while the
 * Mouse/Keyboard interfaces live on the session bus under its well-known
 * name. */
static GDBusConnection* g_p2p;
static std::string g_console_path;
static GDBusConnection* g_sbus;
static std::string g_qemu_name;
/* Coalesced pointer state. QEMU's display path can corrupt frames under a
 * pointer flood, so retain the newest target and send at most 30 updates/s. */
static std::mutex g_mu_abs;
static uint32_t g_last_mx = 0xFFFFFFFFu, g_last_my = 0xFFFFFFFFu;
static uint32_t g_pending_mx, g_pending_my;
static bool g_mouse_pending;
static gint64 g_last_mouse_us;
static guint g_mouse_timer;
/* Last DMABUF scanout, kept (dup'd fd) so UpdateDMABUF damage notifies can
 * re-read the same buffer. QEMU only sends a fresh ScanoutDMABUF when the
 * buffer itself changes. */
static int g_dmabuf_fd = -1;
static uint32_t g_dmabuf_w, g_dmabuf_h, g_dmabuf_stride, g_dmabuf_fourcc;
static uint64_t g_dmabuf_mod;
static bool g_dmabuf_y0_top;
static bool g_dmabuf_valid;

static EGLDisplay g_egl = EGL_NO_DISPLAY;
static EGLContext g_egl_ctx = EGL_NO_CONTEXT;
static struct gbm_device* g_gbm;
static int g_drm_fd = -1;
static PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC p_glEGLImageTargetTexture2DOES;

static bool egl_init() {
	if (g_egl != EGL_NO_DISPLAY)
		return true;
	g_drm_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	if (g_drm_fd < 0)
		g_drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (g_drm_fd < 0) {
		std::fprintf(stderr, "qemu-egl: no DRM render node\n");
		return false;
	}
	g_gbm = gbm_create_device(g_drm_fd);
	if (!g_gbm) {
		std::fprintf(stderr, "qemu-egl: gbm_create_device failed\n");
		return false;
	}
	g_egl = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, g_gbm, nullptr);
	if (g_egl == EGL_NO_DISPLAY)
		g_egl = eglGetDisplay((EGLNativeDisplayType)g_gbm);
	if (g_egl == EGL_NO_DISPLAY || !eglInitialize(g_egl, nullptr, nullptr)) {
		std::fprintf(stderr, "qemu-egl: egl display/init failed (err=0x%x)\n", eglGetError());
		return false;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint cfg_attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
	EGLConfig cfg;
	EGLint count = 0;
	if (!eglChooseConfig(g_egl, cfg_attrs, &cfg, 1, &count) || count < 1)
		return false;
	EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
	g_egl_ctx = eglCreateContext(g_egl, cfg, EGL_NO_CONTEXT, ctx_attrs);
	if (g_egl_ctx == EGL_NO_CONTEXT)
		return false;
	p_eglCreateImageKHR =
		(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
	p_glEGLImageTargetTexture2DOES =
		(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	std::fprintf(stderr, "qemu-egl: GBM/EGL image import ready (%s)\n",
				 (p_eglCreateImageKHR && p_glEGLImageTargetTexture2DOES) ? "yes" : "no");
	return true;
}

static void store_bgra(const uint8_t* src, int w, int h, int stride) {
	std::lock_guard<std::mutex> lock(g_mu);
	g_w = w;
	g_h = h;
	g_rgba.resize((size_t)w * (size_t)h * 4);
	for (int y = 0; y < h; y++) {
		const uint8_t* row = src + y * stride;
		uint8_t* dst = g_rgba.data() + (size_t)y * w * 4;
		for (int x = 0; x < w; x++) {
			dst[x * 4 + 0] = row[x * 4 + 2];
			dst[x * 4 + 1] = row[x * 4 + 1];
			dst[x * 4 + 2] = row[x * 4 + 0];
			dst[x * 4 + 3] = 255;
		}
	}
	g_have = true;
	static uint64_t scanout_count = 0;
	if ((scanout_count++ % 120) == 0) {
		size_t nonblack = 0;
		uint64_t checksum = 0;
		for (size_t i = 0; i < (size_t)w * h; i += 97) {
			const uint8_t* p = g_rgba.data() + i * 4;
			nonblack += (p[0] | p[1] | p[2]) != 0;
			checksum = checksum * 131 + p[0] + ((uint64_t)p[1] << 8) +
					   ((uint64_t)p[2] << 16);
		}
		std::fprintf(stderr,
					 "qemu-egl: CPU scanout %dx%d stride=%d samples_nonblack=%zu checksum=0x%llx\n",
					 w, h, stride, nonblack, (unsigned long long)checksum);
		std::fflush(stderr);
	}
	struct timespec ts{};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	g_last_capture_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t qemu_egl_last_capture_ns() {
	std::lock_guard<std::mutex> lock(g_mu);
	return g_last_capture_ns;
}

static bool map_linear_dmabuf(int fd, uint32_t w, uint32_t h, uint32_t stride,
							  uint64_t modifier) {
	/* QEMU's INVALID/zero modifier scanouts are linear on the host. This CPU
	 * path keeps EGL capture working on GBM drivers which can export a DMA-BUF
	 * but cannot create a GLES context (common with mixed GPU setups). */
	constexpr uint64_t k_drm_format_mod_invalid = 0x00ffffffffffffffULL;
	if ((modifier != 0 && modifier != UINT64_MAX && modifier != k_drm_format_mod_invalid) ||
		!w || !h || stride < w * 4)
		return false;
	size_t length = (size_t)stride * h;
	void* map = mmap(nullptr, length, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		std::fprintf(stderr, "qemu-egl: linear dmabuf mmap failed: %s\n", std::strerror(errno));
		return false;
	}
	store_bgra(static_cast<const uint8_t*>(map), (int)w, (int)h, (int)stride);
	munmap(map, length);
	std::fprintf(stderr, "qemu-egl: using mapped linear dmabuf fallback\n");
	return true;
}

static bool import_dmabuf(int fd, uint32_t w, uint32_t h, uint32_t stride, uint32_t fourcc,
						  uint64_t modifier, bool y0_top) {
	if (!egl_init() || !p_eglCreateImageKHR || !p_glEGLImageTargetTexture2DOES)
		return map_linear_dmabuf(fd, w, h, stride, modifier);
	Display* old_display = glXGetCurrentDisplay();
	GLXDrawable old_drawable = glXGetCurrentDrawable();
	GLXContext old_context = glXGetCurrentContext();
	if (old_context && !glXMakeCurrent(old_display, None, nullptr))
		return false;
	if (!eglMakeCurrent(g_egl, EGL_NO_SURFACE, EGL_NO_SURFACE, g_egl_ctx)) {
		if (old_context) glXMakeCurrent(old_display, old_drawable, old_context);
		return map_linear_dmabuf(fd, w, h, stride, modifier);
	}
	EGLint attrs[20];
	int i = 0;
	attrs[i++] = EGL_WIDTH;
	attrs[i++] = (EGLint)w;
	attrs[i++] = EGL_HEIGHT;
	attrs[i++] = (EGLint)h;
	attrs[i++] = EGL_LINUX_DRM_FOURCC_EXT;
	attrs[i++] = (EGLint)fourcc;
	attrs[i++] = EGL_DMA_BUF_PLANE0_FD_EXT;
	attrs[i++] = fd;
	attrs[i++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
	attrs[i++] = 0;
	attrs[i++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
	attrs[i++] = (EGLint)stride;
	/* QEMU uses DRM_FORMAT_MOD_INVALID (all bits set) to mean that no explicit
	 * modifier was supplied. Passing that sentinel as an actual modifier makes
	 * Mesa reject an otherwise importable linear scanout. */
	if (modifier && modifier != UINT64_MAX && modifier != 0x00ffffffffffffffULL) {
		attrs[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
		attrs[i++] = (EGLint)(modifier & 0xffffffffu);
		attrs[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
		attrs[i++] = (EGLint)(modifier >> 32);
	}
	attrs[i++] = EGL_NONE;
	EGLImage img =
		p_eglCreateImageKHR(g_egl, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attrs);
	if (img == EGL_NO_IMAGE_KHR) {
		std::fprintf(stderr, "qemu-egl: eglCreateImageKHR failed (fourcc=0x%x)\n", fourcc);
		eglMakeCurrent(g_egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (old_context) glXMakeCurrent(old_display, old_drawable, old_context);
		return false;
	}
	GLuint tex = 0, fbo = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	p_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, img);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	std::vector<uint8_t> tmp((size_t)w * h * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data());
	/* QEMU tells us which edge row zero represents. With a top-origin DMA-BUF,
	 * the imported GL image already compensates for GL's lower-left readback
	 * convention, so reversing here would create a second vertical flip when
	 * upload_rgba converts the top-first capture into an OpenXR texture. */
	{
		std::lock_guard<std::mutex> lock(g_mu);
		g_w = (int)w;
		g_h = (int)h;
		g_rgba.resize((size_t)w * h * 4);
		for (uint32_t y = 0; y < h; y++) {
			/* y0_top describes the DMA-BUF's memory origin. The EGL import keeps
			 * GL's lower-left framebuffer convention, so a top-origin source must
			 * be reversed at readback; a bottom-origin source is already ordered
			 * for upload_rgba's single texture conversion. */
			uint32_t src_y = y0_top ? (h - 1 - y) : y;
			std::memcpy(g_rgba.data() + (size_t)y * w * 4,
						tmp.data() + (size_t)src_y * w * 4, (size_t)w * 4);
		}
		g_have = true;
		struct timespec ts{};
		clock_gettime(CLOCK_MONOTONIC, &ts);
		g_last_capture_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	glDeleteTextures(1, &tex);
	eglDestroyImage(g_egl, img);
	eglMakeCurrent(g_egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (old_context) glXMakeCurrent(old_display, old_drawable, old_context);
	return true;
}

static void method_call(GDBusConnection*, const gchar*, const gchar*, const gchar*,
						const gchar* method, GVariant* params, GDBusMethodInvocation* inv,
						gpointer) {
	if (!std::strcmp(method, "Scanout")) {
		guint32 w, h, stride, fmt;
		GVariant* data_v = nullptr;
		g_variant_get(params, "(uuuu@ay)", &w, &h, &stride, &fmt, &data_v);
		gsize n = 0;
		const guint8* data = (const guint8*)g_variant_get_fixed_array(data_v, &n, 1);
		{
			int64_t need = (int64_t)(h > 0 ? h - 1 : 0) * stride + (int64_t)w * 4;
			if ((int64_t)n < need) {
				std::fprintf(stderr, "qemu-egl: Scanout w=%u h=%u stride=%u fmt=0x%x datalen=%zu need=%lld%s\n",
							 w, h, stride, fmt, n, (long long)need,
							 (int64_t)n < need ? " SHORT-DATA" : "");
				std::fflush(stderr);
			}
		}
		if (data && w && h)
			store_bgra(data, (int)w, (int)h, (int)stride);
		g_variant_unref(data_v);
		g_dbus_method_invocation_return_value(inv, nullptr);
		return;
	}
	if (!std::strcmp(method, "Update")) {
		gint32 x, y, w, h;
		guint32 stride, fmt;
		GVariant* data_v = nullptr;
		g_variant_get(params, "(iiiiuu@ay)", &x, &y, &w, &h, &stride, &fmt, &data_v);
		gsize n = 0;
		const guint8* data = (const guint8*)g_variant_get_fixed_array(data_v, &n, 1);
		/* Reject malformed geometry before it can index outside the scanout.
		 * The payload must cover (h-1)*stride + w*4 bytes. --axiss */
		{
			int64_t need = (int64_t)(h > 0 ? h - 1 : 0) * stride + (int64_t)w * 4;
			if (x < 0 || y < 0 || w <= 0 || h <= 0 || (int64_t)n < need) {
				std::fprintf(stderr,
							 "qemu-egl: Update x=%d y=%d w=%d h=%d stride=%u fmt=0x%x "
							 "datalen=%zu need=%lld%s\n",
							 x, y, w, h, stride, fmt, n, (long long)need,
							 (int64_t)n < need ? " SHORT-DATA" : "");
				std::fflush(stderr);
			}
		}
		if (data && x >= 0 && y >= 0 && w > 0 && h > 0) {
			std::lock_guard<std::mutex> lock(g_mu);
			if ((int)(x + w) <= g_w && (int)(y + h) <= g_h && !g_rgba.empty()) {
				for (int row = 0; row < h; row++) {
					const uint8_t* s = data + row * stride;
					uint8_t* d = g_rgba.data() + ((size_t)(y + row) * g_w + x) * 4;
					for (int col = 0; col < w; col++) {
						d[col * 4 + 0] = s[col * 4 + 2];
						d[col * 4 + 1] = s[col * 4 + 1];
						d[col * 4 + 2] = s[col * 4 + 0];
						d[col * 4 + 3] = 255;
					}
				}
				struct timespec ts{};
				clock_gettime(CLOCK_MONOTONIC, &ts);
				g_last_capture_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
			}
		}
		g_variant_unref(data_v);
		g_dbus_method_invocation_return_value(inv, nullptr);
		return;
	}
	if (!std::strcmp(method, "ScanoutDMABUF")) {
		GDBusMessage* msg = g_dbus_method_invocation_get_message(inv);
		GUnixFDList* fds = g_dbus_message_get_unix_fd_list(msg);
		gint32 hidx = 0;
		guint32 w = 0, ht = 0, stride = 0, fourcc = 0;
		guint64 mod = 0;
		gboolean y0 = TRUE;
		g_variant_get(params, "(huuuutb)", &hidx, &w, &ht, &stride, &fourcc, &mod, &y0);
		gint fd = -1;
		if (fds)
			fd = g_unix_fd_list_get(fds, hidx, nullptr);
		{
			static bool logged = false;
			static uint32_t last_fourcc = 0;
			if (!logged || fourcc != last_fourcc) {
				std::fprintf(stderr, "qemu-egl: scanout %ux%u stride=%u fourcc=0x%x mod=0x%llx\n",
							 w, ht, stride, fourcc, (unsigned long long)mod);
				std::fflush(stderr);
				logged = true;
				last_fourcc = fourcc;
			}
		}
		if (fd >= 0) {
			import_dmabuf(fd, w, ht, stride, fourcc, mod, y0 == TRUE);
			/* Keep our own reference so later UpdateDMABUF notifies
			 * (same buffer, new damage) can re-read it. */
			int kept = dup(fd);
			if (kept >= 0) {
				std::lock_guard<std::mutex> lock(g_mu);
				if (g_dmabuf_fd >= 0)
					close(g_dmabuf_fd);
				g_dmabuf_fd = kept;
				g_dmabuf_w = w;
				g_dmabuf_h = ht;
				g_dmabuf_stride = stride;
				g_dmabuf_fourcc = fourcc;
				g_dmabuf_mod = mod;
				g_dmabuf_y0_top = y0 == TRUE;
				g_dmabuf_valid = true;
			}
			close(fd);
		}
		g_dbus_method_invocation_return_value(inv, nullptr);
		return;
	}
	if (!std::strcmp(method, "UpdateDMABUF")) {
		gint32 x = 0, y = 0, w = 0, h = 0;
		g_variant_get(params, "(iiii)", &x, &y, &w, &h);
		(void)x;
		(void)y;
		(void)w;
		(void)h;
		int fd = -1;
		uint32_t cw = 0, ch = 0, cs = 0, cf = 0;
		uint64_t cm = 0;
		bool cy0_top = true;
		{
			std::lock_guard<std::mutex> lock(g_mu);
			if (g_dmabuf_valid && g_dmabuf_fd >= 0) {
				fd = dup(g_dmabuf_fd);
				cw = g_dmabuf_w;
				ch = g_dmabuf_h;
				cs = g_dmabuf_stride;
				cf = g_dmabuf_fourcc;
				cm = g_dmabuf_mod;
				cy0_top = g_dmabuf_y0_top;
			}
		}
		if (fd >= 0) {
			import_dmabuf(fd, cw, ch, cs, cf, cm, cy0_top);
			close(fd);
		}
		g_dbus_method_invocation_return_value(inv, nullptr);
		return;
	}
	g_dbus_method_invocation_return_value(inv, nullptr);
}

static const GDBusInterfaceVTable k_vt = {method_call, nullptr, nullptr, {}};

static gboolean on_new_p2p(GDBusServer*, GDBusConnection* conn, gpointer) {
	GError* err = nullptr;
	GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(k_listener_xml, &err);
	if (!info) {
		std::fprintf(stderr, "qemu-egl: xml: %s\n", err ? err->message : "");
		return TRUE;
	}
	g_dbus_connection_register_object(conn, "/org/qemu/Display1/Listener",
									  info->interfaces[0], &k_vt, nullptr, nullptr, &err);
	if (err)
		std::fprintf(stderr, "qemu-egl: register object: %s\n", err->message);
	g_dbus_node_info_unref(info);
	return TRUE;
}

bool qemu_egl_connect(const char* dbus_addr) {
	GError* err = nullptr;
	GDBusConnection* bus = nullptr;
	if (dbus_addr && dbus_addr[0]) {
		bus = g_dbus_connection_new_for_address_sync(
			dbus_addr, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT, nullptr, nullptr, &err);
	} else {
		bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
	}
	if (!bus) {
		std::fprintf(stderr, "qemu-egl: session/dbus connect: %s\n", err ? err->message : "?");
		return false;
	}

	gchar* qemu_name = g_strdup("org.qemu");
	GVariant* names = g_dbus_connection_call_sync(
		bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames",
		nullptr, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
	if (names) {
		GVariantIter* it = nullptr;
		const gchar* n = nullptr;
		g_variant_get(names, "(as)", &it);
		while (g_variant_iter_loop(it, "s", &n)) {
			if (n && std::strncmp(n, "org.qemu", 8) == 0) {
				g_free(qemu_name);
				qemu_name = g_strdup(n);
				break;
			}
		}
		g_variant_iter_free(it);
		g_variant_unref(names);
	}

	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) {
		std::fprintf(stderr, "qemu-egl: socketpair failed\n");
		return false;
	}

	/* Hand QEMU the peer fd first, then connect to it as a D-Bus CLIENT.
	 * QEMU serves the reverse connection (it answers our AUTH with OK). */
	/* gpu0 is registered first so VNC and XR consume the same console. Allow
	 * older launch layouts to select Console_1 explicitly. */
	const char* selected = std::getenv("XENEVA_QEMU_CONSOLE");
	std::string selected_path = "/org/qemu/Display1/Console_";
	selected_path += selected && *selected ? selected : "0";
	const char* consoles[] = {selected_path.c_str(), "/org/qemu/Display1/Console"};
	GUnixFDList* fds = g_unix_fd_list_new_from_array(&sv[1], 1);
	GVariant* ret = nullptr;
	const char* reg_console = nullptr;
	for (int console_index = 0; console_index < 2; ++console_index) {
		const char* path = consoles[console_index];
		err = nullptr;
		ret = g_dbus_connection_call_with_unix_fd_list_sync(
			bus, qemu_name, path, "org.qemu.Display1.Console", "RegisterListener",
			g_variant_new("(h)", 0), nullptr, G_DBUS_CALL_FLAGS_NONE, 8000, fds, nullptr, nullptr,
			&err);
		if (ret) {
			std::fprintf(stderr, "qemu-egl: RegisterListener ok on %s (%s)\n", path, qemu_name);
			reg_console = path;
			break;
		}
	}
	if (!ret) {
		std::fprintf(stderr,
					 "qemu-egl: RegisterListener failed: %s\n"
					 "          Is QEMU running with -display dbus,gl=on on the session bus?\n",
					 err ? err->message : "?");
		g_object_unref(fds);
		g_free(qemu_name);
		close(sv[0]);
		close(sv[1]);
		return false;
	}
	g_variant_unref(ret);
	g_object_unref(fds);

	GSocket* gs = g_socket_new_from_fd(sv[0], &err);
	if (!gs) {
		std::fprintf(stderr, "qemu-egl: g_socket_new_from_fd: %s\n", err ? err->message : "?");
		g_free(qemu_name);
		return false;
	}
	GSocketConnection* sc = G_SOCKET_CONNECTION(g_socket_connection_factory_create_connection(gs));
	gchar* guid = g_dbus_generate_guid();
	err = nullptr;
	/* QEMU is the SERVER on the reverse connection: it answers our client
	 * greeting with OK (verified against qemu 11.1.1). Connecting as
	 * SERVER here makes new_sync block forever waiting for QEMU to speak
	 * first, which it never does. Client connections take guid=NULL. */
	GDBusConnection* p2p = g_dbus_connection_new_sync(
		G_IO_STREAM(sc), nullptr, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT, nullptr,
		nullptr, &err);
	g_free(guid);
	if (!p2p) {
		std::fprintf(stderr, "qemu-egl: p2p conn: %s\n", err ? err->message : "?");
		g_free(qemu_name);
		return false;
	}
	on_new_p2p(nullptr, p2p, nullptr);
	g_dbus_connection_start_message_processing(p2p);
	/* Do not eglInitialize here: GBM/EGL can block, and OpenXR never starts.
	 * EGL is created lazily on ScanoutDMABUF. */
	{
		std::lock_guard<std::mutex> lock(g_mu);
		g_p2p = p2p;
		if (reg_console)
			g_console_path = reg_console;
		g_sbus = bus;
		g_object_ref(g_sbus);
		/* Copy before g_free below (use-after-free reads garbage). */
		g_qemu_name = qemu_name ? qemu_name : "";
	}
	g_free(qemu_name);
	return true;
}

/* Fire-and-forget input injection on the registered console's Mouse
 * interface (absolute pointer, same path VNC uses). Async: never blocks
 * the frame loop. */
static void mouse_call(const char* method, GVariant* params) {
	GDBusConnection* conn = nullptr;
	std::string name, path;
	{
		std::lock_guard<std::mutex> lock(g_mu);
		conn = g_sbus;
		name = g_qemu_name;
		path = g_console_path;
		if (conn)
			g_object_ref(conn);
	}
	if (!conn || name.empty() || path.empty()) {
		if (params)
			g_variant_unref(params);
		return;
	}
	g_dbus_connection_call(conn, name.c_str(), path.c_str(), "org.qemu.Display1.Mouse", method,
						   params, nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr,
						   nullptr);
	g_object_unref(conn);
}

void qemu_mouse_abs(uint32_t x, uint32_t y) {
	constexpr gint64 interval_us = 33333;
	std::lock_guard<std::mutex> lock(g_mu_abs);
	if (!g_mouse_pending && x == g_last_mx && y == g_last_my)
		return;
	g_pending_mx = x;
	g_pending_my = y;
	g_mouse_pending = true;
	if (g_mouse_timer)
		return;
	gint64 elapsed = g_get_monotonic_time() - g_last_mouse_us;
	guint delay_ms = elapsed >= interval_us ? 1u : (guint)((interval_us - elapsed + 999) / 1000);
	g_mouse_timer = g_timeout_add(delay_ms, [](gpointer) -> gboolean {
		uint32_t send_x = 0, send_y = 0;
		{
			std::lock_guard<std::mutex> pending_lock(g_mu_abs);
			g_mouse_timer = 0;
			if (!g_mouse_pending)
				return G_SOURCE_REMOVE;
			send_x = g_pending_mx;
			send_y = g_pending_my;
			g_mouse_pending = false;
			g_last_mx = send_x;
			g_last_my = send_y;
			g_last_mouse_us = g_get_monotonic_time();
		}
		mouse_call("SetAbsPosition", g_variant_new("(uu)", send_x, send_y));
		return G_SOURCE_REMOVE;
	}, nullptr);
}

void qemu_mouse_button(uint32_t button, bool down) {
	mouse_call(down ? "Press" : "Release", g_variant_new("(u)", button));
}

bool qemu_egl_poll(uint32_t** rgba, int* w, int* h) {
	while (g_main_context_pending(nullptr))
		g_main_context_iteration(nullptr, FALSE);
	std::lock_guard<std::mutex> lock(g_mu);
	if (!g_have || g_rgba.empty())
		return false;
	/* The D-Bus scanout thread may replace/resize g_rgba as soon as this
	 * mutex is released.  Return a per-calling-thread snapshot so the viewer
	 * can copy pixels without holding a dangling pointer. */
	static thread_local std::vector<uint32_t> snapshot;
	snapshot.resize(g_rgba.size() / sizeof(uint32_t));
	std::memcpy(snapshot.data(), g_rgba.data(), g_rgba.size());
	*w = g_w;
	*h = g_h;
	*rgba = snapshot.data();
	return true;
}
