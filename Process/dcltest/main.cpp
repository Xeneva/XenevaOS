/*
 * Process/dcltest/main.cpp -- userspace end-to-end test for the DCL ports.
 *
 * DCL/mem_test.c proves the chain from inside the kernel, but it cannot reach
 * the half that actually matters to users: a real process calling
 * _KeOpenFile() -> fileserv -> devfs -> mainline file_operations, carrying
 * its own credentials. That distinction is not academic -- the DCL nodes are
 * created uid0/gid-world (AURORA_GID_MISC_WORLD), init hands XEShell that
 * group as a supplementary group, and a bare normal-user process is refused.
 * So run this from the shell / Terminal:
 *
 *     /dcltest.exe
 *
 * Every result is emitted to stdout (so you can read it in the shell) and to
 * /dev/kmsg, whose write path is mainline-style fops ending in UARTDebugOut.
 * That makes the same transcript appear in the QEMU serial log, so CI can
 * assert on it without a display:
 *
 *     grep '\[dcltest\]' run.log
 *
 * Exit code is 0 when every check passed, otherwise the number of failures.
 */

#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>

static int g_ok;
static int g_bad;
static int g_kmsg = -1;

/* emit -- one line to both the caller's console and the kernel log. */
static void emit(const char* line) {
	printf("%s", line);
	if (g_kmsg < 0)
		g_kmsg = _KeOpenFile((char*)"/dev/kmsg", FILE_OPEN_WRITE);
	if (g_kmsg >= 0)
		_KeWriteFile(g_kmsg, (void*)line, strlen(line));
}

/* report -- assert got == want. */
static void report(const char* what, int got, int want) {
	char line[168];
	if (got == want) {
		g_ok++;
		sprintf(line, "[dcltest] PASS %s (%d)\n", what, got);
	} else {
		g_bad++;
		sprintf(line, "[dcltest] FAIL %s got=%d want=%d\n", what, got, want);
	}
	emit(line);
}

/* note -- record something without counting it as a check. */
static void note(const char* what, int value) {
	char line[168];
	sprintf(line, "[dcltest] info %s (%d)\n", what, value);
	emit(line);
}

/* opendev -- open a node through the real syscall path; the open itself is a
 * check, because that is where mainline's memory_open() per-minor fops swap
 * happens (fileserv calls node->open at Serv/fileserv.c:127). */
static int opendev(const char* path, int mode) {
	char line[168];
	int fd = _KeOpenFile((char*)path, mode);
	if (fd < 0) {
		g_bad++;
		sprintf(line, "[dcltest] FAIL open %s fd=%d\n", path, fd);
	} else {
		g_ok++;
		sprintf(line, "[dcltest] PASS open %s fd=%d\n", path, fd);
	}
	emit(line);
	return fd;
}

/* ── stage 2: KMS properties, the libdrm two-step ─────────────────────────
 *
 * OBJ_GETPROPERTIES for an object's property ids -- the count pass first,
 * then the arrays -- and GETPROPERTY for each name.  The struct layouts are
 * the uapi's, spelled like every other ioctl struct in this file; the ioctl
 * numbers carry the sizes (32 and 64). */
struct drm_mode_obj_getprops_arg {
	unsigned long long props_ptr, prop_values_ptr;
	unsigned int count_props, obj_id, obj_type;
};
struct drm_mode_getprop_arg {
	unsigned long long values_ptr, enum_blob_ptr;
	unsigned int prop_id, flags;
	char name[32];
	unsigned int count_values, count_enum_blobs;
};

#define DRM_IOCTL_MODE_OBJ_GETPROPERTIES 0xC02064B9u	/* _IOWR('d', 0xB9, 32) */
#define DRM_IOCTL_MODE_GETPROPERTY 0xC04064AAu		/* _IOWR('d', 0xAA, 64) */

/* DRM_MODE_OBJECT_* -- obj_type for kms_prop_id(), from drm_mode_object.h */
#define DRM_MODE_OBJECT_CONNECTOR 0xC0C0C0C0u
#define DRM_MODE_OBJECT_CRTC 0xCCCCCCCCu
#define DRM_MODE_OBJECT_PLANE 0xEEEEEEEEu

/* kms_prop_id -- the id of the property named `want` on obj_id of obj_type,
 * or 0 (never a property id) when the object or name is not there. */
static unsigned int kms_prop_id(int fd, unsigned int obj_id,
				unsigned int obj_type, const char* want) {
	static unsigned int ids[64];
	static unsigned long long vals[64];
	struct drm_mode_obj_getprops_arg op;
	unsigned int n, i;

	memset(&op, 0, sizeof op);
	op.obj_id = obj_id;
	op.obj_type = obj_type;
	if (_KeFileIoControl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op) < 0)
		return 0;
	n = op.count_props;
	if (n == 0 || n > 64)
		return 0;

	memset(&op, 0, sizeof op);
	op.obj_id = obj_id;
	op.obj_type = obj_type;
	op.props_ptr = (unsigned long long)(size_t)ids;
	op.prop_values_ptr = (unsigned long long)(size_t)vals;
	op.count_props = n;
	if (_KeFileIoControl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op) < 0)
		return 0;

	for (i = 0; i < n; i++) {
		struct drm_mode_getprop_arg gp;
		memset(&gp, 0, sizeof gp);
		gp.prop_id = ids[i];
		if (_KeFileIoControl(fd, DRM_IOCTL_MODE_GETPROPERTY, &gp) < 0)
			continue;
		if (strcmp(gp.name, want) == 0)
			return ids[i];
	}
	return 0;
}

/* drm_find_card -- find /dev/dri/cardN by the driver name VERSION reports.
 *
 * Card numbers are assigned in probe order, and QEMU decides which devices
 * exist: vkms is always card0, but whether bochs and virtio-gpu are there,
 * and in which order, is a property of the machine rather than of this
 * test.  Matching on the driver name keeps every stage independent of that
 * numbering.  Returns 0 and fills `path` on a match, -1 otherwise.
 */
static int drm_find_card(const char* want, char* path, size_t path_len) {
	static const char* const cards[] = {
		"/dev/dri/card0", "/dev/dri/card1",
		"/dev/dri/card2", "/dev/dri/card3",
	};
	struct drm_version_arg {
		int major, minor, patchlevel;
		size_t name_len;
		char* name;
		size_t date_len;
		char* date;
		size_t desc_len;
		char* desc;
	} ver;
	char name[32];
	size_t i, n, want_len = strlen(want);

	for (i = 0; i < sizeof(cards) / sizeof(cards[0]); i++) {
		int fd = _KeOpenFile((char*)cards[i],
				     FILE_OPEN_READ_ONLY | FILE_OPEN_WRITE);
		int rc;

		if (fd < 0)
			continue;

		memset(&ver, 0, sizeof ver);
		memset(name, 0, sizeof name);
		ver.name = name;
		ver.name_len = sizeof name;
		rc = _KeFileIoControl(fd, 0xC0406400, &ver);
		_KeCloseFile(fd);
		if (rc != 0 || ver.name_len < want_len)
			continue;
		if (memcmp(name, want, want_len) != 0)
			continue;

		n = strlen(cards[i]);
		if (n >= path_len)
			n = path_len - 1;
		memcpy(path, (void*)cards[i], n);
		path[n] = 0;
		return 0;
	}
	return -1;
}

/* ── the vendored virtio-gpu, driven by atomic commits ────────────────────
 *
 * The console's boot framebuffer is created by DCL/virtgpu_console.c and
 * committed through the driver's atomic path.  This test reads that live
 * state, flips the primary plane to a userspace dumb buffer with the same
 * mode, verifies the plane state changed, leaves the pattern on screen long
 * enough for the host's gpu0 screendump, then flips back to the console fb.
 * It deliberately does not change the mode: that would blank the console
 * while the test FB is up and is already covered on vkms and bochs.
 */
static void test_virtgpu_card(const char* path) {
	struct drm_version_arg {
		int major, minor, patchlevel;
		size_t name_len;
		char* name;
		size_t date_len;
		char* date;
		size_t desc_len;
		char* desc;
	} ver;
	struct drm_mode_card_res_arg {
		unsigned long long fb_id_ptr, crtc_id_ptr;
		unsigned long long connector_id_ptr;
		unsigned long long encoder_id_ptr;
		unsigned int count_fbs, count_crtcs;
		unsigned int count_connectors, count_encoders;
		unsigned int min_width, max_width;
		unsigned int min_height, max_height;
	} res;
	struct drm_mode_get_plane_res_arg {
		unsigned long long plane_id_ptr;
		unsigned int count_planes;
	} pres;
	struct drm_mode_get_plane_arg {
		unsigned int plane_id, crtc_id, fb_id;
		unsigned int possible_crtcs, gamma_size;
		unsigned int count_format_types;
		unsigned long long format_type_ptr;
	} pl;
	struct drm_mode_create_dumb_arg {
		unsigned int height, width, bpp, flags;
		unsigned int handle, pitch;
		unsigned long long size;
	} dumb;
	struct drm_mode_map_dumb_arg {
		unsigned int handle, pad;
		unsigned long long offset;
	} map;
	struct drm_mode_fb_cmd2_arg {
		unsigned int fb_id, width, height;
		unsigned int pixel_format, flags;
		unsigned int handles[4], pitches[4], offsets[4];
		unsigned long long modifier[4];
	} fb;
	struct drm_mode_atomic_arg {
		unsigned int flags, count_objs;
		unsigned long long objs_ptr, count_props_ptr;
		unsigned long long props_ptr, prop_values_ptr;
		unsigned long long reserved, user_data;
	} at;
	struct drm_mode_crtc_arg {
		unsigned long long set_connectors_ptr;
		unsigned int count_connectors;
		unsigned int crtc_id, fb_id, x, y;
		unsigned int gamma_size, mode_valid;
		struct {
			unsigned int clock;
			unsigned short hdisplay, hsync_start, hsync_end;
			unsigned short htotal, hskew;
			unsigned short vdisplay, vsync_start, vsync_end;
			unsigned short vtotal, vscan;
			unsigned int vrefresh, flags, type;
			char name[32];
		} mode;
	} crtc;
	static unsigned int crtc_ids[4], conn_ids[4], plane_ids[4];
	static unsigned int enc_ids[4];
	static unsigned int a_objs[2], a_counts[2];
	static unsigned int a_props[13];
	static unsigned long long a_vals[13];
	unsigned int conn_crtc_id, plane_fb_id, plane_crtc_id;
	unsigned int src_x, src_y, src_w, src_h;
	unsigned int crtc_x, crtc_y, crtc_w, crtc_h;
	unsigned int console_fb_id, width = 0, height = 0, p;
	char cname[32], cdesc[64];
	const unsigned int pattern = 0x0000BEEFu;
	int vfd = opendev(path, FILE_OPEN_READ_ONLY | FILE_OPEN_WRITE);

	if (vfd < 0)
		return;

	memset(&ver, 0, sizeof ver);
	memset(cname, 0, sizeof cname);
	memset(cdesc, 0, sizeof cdesc);
	ver.name = cname;
	ver.name_len = sizeof cname;
	ver.desc = cdesc;
	ver.desc_len = sizeof cdesc;
	report("drm virtio-gpu VERSION",
	       _KeFileIoControl(vfd, 0xC0406400, &ver), 0);
	report("drm virtio-gpu name is virtio_gpu",
	       (int)memcmp(cname, "virtio_gpu", 10), 0);

	/* universal planes + atomic, same caps the other KMS stages set */
	{
		struct {
			unsigned long long capability;
			unsigned long long value;
		} scc;
		scc.capability = 2;
		scc.value = 1;
		report("drm virtio-gpu universal planes cap",
		       _KeFileIoControl(vfd, 0x4010640D, &scc), 0);
		scc.capability = 3;
		report("drm virtio-gpu atomic cap",
		       _KeFileIoControl(vfd, 0x4010640D, &scc), 0);
	}

	memset(&res, 0, sizeof res);
	report("drm virtio-gpu GETRESOURCES",
	       _KeFileIoControl(vfd, 0xC04064A0, &res), 0);
	report("drm virtio-gpu crtcs", (int)res.count_crtcs, 1);
	report("drm virtio-gpu connectors", (int)res.count_connectors, 1);

	memset(&res, 0, sizeof res);
	res.crtc_id_ptr = (unsigned long long)(size_t)crtc_ids;
	res.count_crtcs = 4;
	res.connector_id_ptr = (unsigned long long)(size_t)conn_ids;
	res.count_connectors = 4;
	res.encoder_id_ptr = (unsigned long long)(size_t)enc_ids;
	res.count_encoders = 4;
	report("drm virtio-gpu GETRESOURCES (ids)",
	       _KeFileIoControl(vfd, 0xC04064A0, &res), 0);
	report("drm virtio-gpu crtc id", (int)(crtc_ids[0] != 0), 1);
	report("drm virtio-gpu connector id", (int)(conn_ids[0] != 0), 1);

	memset(&crtc, 0, sizeof crtc);
	crtc.crtc_id = crtc_ids[0];
	report("drm virtio-gpu GETCRTC",
	       _KeFileIoControl(vfd, 0xC06864A1, &crtc), 0);
	report("drm virtio-gpu crtc programmed", (int)crtc.mode_valid, 1);
	width = crtc.mode.hdisplay;
	height = crtc.mode.vdisplay;
	report("drm virtio-gpu console mode", (int)(width >= 640 && height >= 480), 1);

	pres.plane_id_ptr = (unsigned long long)(size_t)plane_ids;
	pres.count_planes = 4;
	report("drm virtio-gpu GETPLANERESOURCES",
	       _KeFileIoControl(vfd, 0xC01064B5, &pres), 0);
	report("drm virtio-gpu primary planes", (int)pres.count_planes, 1);

	memset(&pl, 0, sizeof pl);
	pl.plane_id = plane_ids[0];
	report("drm virtio-gpu GETPLANE",
	       _KeFileIoControl(vfd, 0xC02064B6, &pl), 0);
	report("drm virtio-gpu console fb bound", (int)(pl.fb_id != 0), 1);
	console_fb_id = pl.fb_id;

	conn_crtc_id = kms_prop_id(vfd, conn_ids[0], DRM_MODE_OBJECT_CONNECTOR,
				   "CRTC_ID");
	plane_fb_id = kms_prop_id(vfd, plane_ids[0], DRM_MODE_OBJECT_PLANE,
				  "FB_ID");
	plane_crtc_id = kms_prop_id(vfd, plane_ids[0], DRM_MODE_OBJECT_PLANE,
				    "CRTC_ID");
	src_x = kms_prop_id(vfd, plane_ids[0], DRM_MODE_OBJECT_PLANE, "SRC_X");
	src_y = kms_prop_id(vfd, plane_ids[0], DRM_MODE_OBJECT_PLANE, "SRC_Y");
	src_w = kms_prop_id(vfd, plane_ids[0], DRM_MODE_OBJECT_PLANE, "SRC_W");
	src_h = kms_prop_id(vfd, plane_ids[0], DRM_MODE_OBJECT_PLANE, "SRC_H");
	crtc_x = kms_prop_id(vfd, plane_ids[0], DRM_MODE_OBJECT_PLANE, "CRTC_X");
	crtc_y = kms_prop_id(vfd, plane_ids[0], DRM_MODE_OBJECT_PLANE, "CRTC_Y");
	crtc_w = kms_prop_id(vfd, plane_ids[0], DRM_MODE_OBJECT_PLANE, "CRTC_W");
	crtc_h = kms_prop_id(vfd, plane_ids[0], DRM_MODE_OBJECT_PLANE, "CRTC_H");
	report("drm virtio-gpu atomic property ids",
	       (int)(conn_crtc_id && plane_fb_id && plane_crtc_id && src_x &&
		     src_y && src_w && src_h && crtc_x && crtc_y && crtc_w &&
		     crtc_h), 1);

	memset(&dumb, 0, sizeof dumb);
	dumb.width = width;
	dumb.height = height;
	dumb.bpp = 32;
	report("drm virtio-gpu CREATE_DUMB",
	       _KeFileIoControl(vfd, 0xC02064B2, &dumb), 0);
	report("drm virtio-gpu dumb pitch", (int)dumb.pitch, (int)width * 4);

	memset(&map, 0, sizeof map);
	map.handle = dumb.handle;
	report("drm virtio-gpu MAP_DUMB",
	       _KeFileIoControl(vfd, 0xC01064B3, &map), 0);
	{
		volatile unsigned int* pix =
			(volatile unsigned int*)(size_t)map.offset;
		unsigned long long words = dumb.size >> 2;
		for (unsigned long long i = 0; i < words; i++)
			pix[i] = pattern;
		report("drm virtio-gpu pattern written",
		       (int)(pix[0] == pattern), 1);
	}

	memset(&fb, 0, sizeof fb);
	fb.width = width;
	fb.height = height;
	fb.pixel_format = 0x34325258; /* XRGB8888 */
	fb.handles[0] = dumb.handle;
	fb.pitches[0] = dumb.pitch;
	report("drm virtio-gpu ADDFB2",
	       _KeFileIoControl(vfd, 0xC06864B8, &fb), 0);
	report("drm virtio-gpu fb id", (int)(fb.fb_id != 0), 1);

	a_objs[0] = conn_ids[0];
	a_counts[0] = 1;
	a_objs[1] = plane_ids[0];
	a_counts[1] = 10;
	p = 0;
	a_props[p] = conn_crtc_id;
	a_vals[p++] = crtc_ids[0];
	a_props[p] = plane_fb_id;
	a_vals[p++] = fb.fb_id;
	a_props[p] = plane_crtc_id;
	a_vals[p++] = crtc_ids[0];
	a_props[p] = src_x;
	a_vals[p++] = 0;
	a_props[p] = src_y;
	a_vals[p++] = 0;
	a_props[p] = src_w;
	a_vals[p++] = (unsigned long long)width << 16;
	a_props[p] = src_h;
	a_vals[p++] = (unsigned long long)height << 16;
	a_props[p] = crtc_x;
	a_vals[p++] = 0;
	a_props[p] = crtc_y;
	a_vals[p++] = 0;
	a_props[p] = crtc_w;
	a_vals[p++] = width;
	a_props[p] = crtc_h;
	a_vals[p++] = height;

	memset(&at, 0, sizeof at);
	at.count_objs = 2;
	at.objs_ptr = (unsigned long long)(size_t)a_objs;
	at.count_props_ptr = (unsigned long long)(size_t)a_counts;
	at.props_ptr = (unsigned long long)(size_t)a_props;
	at.prop_values_ptr = (unsigned long long)(size_t)a_vals;
	at.flags = 0x100 | 0x400; /* TEST_ONLY | ALLOW_MODESET */
	report("drm virtio-gpu atomic TEST_ONLY",
	       _KeFileIoControl(vfd, 0xC03864BC, &at), 0);
	at.flags = 0x400;
	report("drm virtio-gpu atomic commit",
	       _KeFileIoControl(vfd, 0xC03864BC, &at), 0);

	memset(&pl, 0, sizeof pl);
	pl.plane_id = plane_ids[0];
	report("drm virtio-gpu GETPLANE (test fb)",
	       _KeFileIoControl(vfd, 0xC02064B6, &pl), 0);
	report("drm virtio-gpu test fb active", (int)(pl.fb_id == fb.fb_id), 1);

	/* The bochs watcher captures just before this stage; give its monitor
	 * connection time to close before the gpu0 watcher connects. */
	_KeProcessSleep(2000);
	/* let the host's gpu0 watcher take its screendump */
	note("virtio-gpu scanout committed", 1);
	_KeProcessSleep(4000);

	/* flip back to the console framebuffer, leaving the desktop intact */
	a_vals[1] = console_fb_id;
	memset(&at, 0, sizeof at);
	at.count_objs = 2;
	at.objs_ptr = (unsigned long long)(size_t)a_objs;
	at.count_props_ptr = (unsigned long long)(size_t)a_counts;
	at.props_ptr = (unsigned long long)(size_t)a_props;
	at.prop_values_ptr = (unsigned long long)(size_t)a_vals;
	at.flags = 0x400;
	report("drm virtio-gpu console fb restore",
	       _KeFileIoControl(vfd, 0xC03864BC, &at), 0);

	memset(&pl, 0, sizeof pl);
	pl.plane_id = plane_ids[0];
	report("drm virtio-gpu GETPLANE (restored)",
	       _KeFileIoControl(vfd, 0xC02064B6, &pl), 0);
	report("drm virtio-gpu console fb active",
	       (int)(pl.fb_id == console_fb_id), 1);

	{
		unsigned int rmfb_id = fb.fb_id;
		report("drm virtio-gpu RMFB",
		       _KeFileIoControl(vfd, 0xC00464AF, &rmfb_id), 0);
	}
	{
		struct {
			unsigned int handle;
		} dd;
		dd.handle = dumb.handle;
		report("drm virtio-gpu DESTROY_DUMB",
		       _KeFileIoControl(vfd, 0xC00864B4, &dd), 0);
	}

	_KeCloseFile(vfd);
}

/* The writeback pixel pattern, and what the capture buffer must hold for
 * it.  Every channel is derived from the coordinates -- B = x ^ y, G = y,
 * R = x, X = 0 -- so a frame read at the wrong offset, stride or line
 * order compares unequal somewhere.  The capture's top byte is 0xff: that
 * is not the pattern, it is what argb_u16_to_XRGB8888() in
 * Vendored/drivers/gpu/drm/vkms/vkms_formats.c writes there
 * unconditionally, so the compare masks it in. */
static unsigned int wb_pat(int x, int y) {
	return ((unsigned int)(x & 0xFF) << 16) |
	       ((unsigned int)(y & 0xFF) << 8) |
	       ((unsigned int)(x ^ y) & 0xFF);
}
#define WB_CAP_MASK 0xFF000000u

int main(int argc, char* argv[]) {
	unsigned char a[64], b[64];
	int fd, n, i, differs;

	(void)argc;
	(void)argv;
	memset(a, 0, sizeof a);
	memset(b, 0, sizeof b);
	emit("[dcltest] --- DCL userspace test start ---\n");

	/* /dev/null: writes are consumed, reads are EOF */
	fd = opendev("/dev/null", FILE_OPEN_WRITE);
	if (fd >= 0) {
		memset(a, 0xAA, sizeof a);
		report("null write", (int)_KeWriteFile(fd, a, 8), 8);
		_KeCloseFile(fd);
	}
	fd = opendev("/dev/null", FILE_OPEN_READ_ONLY);
	if (fd >= 0) {
		memset(a, 0xAA, sizeof a);
		report("null read (EOF)", (int)_KeReadFile(fd, a, 8), 0);
		_KeCloseFile(fd);
	}

	/* /dev/zero: writes consumed, reads yield zero-filled chunks */
	fd = opendev("/dev/zero", FILE_OPEN_READ_ONLY);
	if (fd >= 0) {
		memset(a, 0xAA, sizeof a);
		n = (int)_KeReadFile(fd, a, 32);
		report("zero read", n, 32);
		differs = 1;
		for (i = 0; i < n && i < (int)sizeof a; i++)
			if (a[i])
				differs = 0;
		report("zero bytes are 0", differs, 1);
		_KeCloseFile(fd);
	}
	fd = opendev("/dev/zero", FILE_OPEN_WRITE);
	if (fd >= 0) {
		memset(a, 0xAA, 8);
		report("zero write", (int)_KeWriteFile(fd, a, 8), 8);
		_KeCloseFile(fd);
	}

	/* /dev/full: reads EOF, writes report no space as a short count */
	fd = opendev("/dev/full", FILE_OPEN_WRITE);
	if (fd >= 0) {
		memset(a, 0xAA, 8);
		report("full write (ENOSPC)", (int)_KeWriteFile(fd, a, 8), 0);
		_KeCloseFile(fd);
	}
	fd = opendev("/dev/full", FILE_OPEN_READ_ONLY);
	if (fd >= 0) {
		report("full read (EOF)", (int)_KeReadFile(fd, a, 8), 0);
		_KeCloseFile(fd);
	}

	/* /dev/urandom: entropy from the hardware RNG, not a constant */
	fd = opendev("/dev/urandom", FILE_OPEN_READ_ONLY);
	if (fd >= 0) {
		memset(a, 0, sizeof a);
		memset(b, 0, sizeof b);
		n = (int)_KeReadFile(fd, a, 16);
		report("urandom read", n, 16);
		_KeReadFile(fd, b, 16);
		differs = 0;
		for (i = 0; i < 16; i++)
			if (a[i] != b[i])
				differs = 1;
		report("urandom samples differ", differs, 1);
		_KeCloseFile(fd);
	}

	/* /dev/random: same backing, blocking-free here because it is hwrng-backed */
	fd = opendev("/dev/random", FILE_OPEN_READ_ONLY);
	if (fd >= 0) {
		memset(a, 0, sizeof a);
		report("random read", (int)_KeReadFile(fd, a, 16), 16);
		_KeCloseFile(fd);
	}

	/* /dev/kmsg: a write reaches the kernel log, a read is EOF (never blocks,
	 * which is why this test writes rather than draining the log). */
	{
		const char* msg = "[dcltest] kmsg write from userspace reached UART\n";
		fd = opendev("/dev/kmsg", FILE_OPEN_WRITE);
		if (fd >= 0) {
			report("kmsg write", (int)_KeWriteFile(fd, (void*)msg, strlen(msg)),
				   (int)strlen(msg));
			_KeCloseFile(fd);
		}
		fd = opendev("/dev/kmsg", FILE_OPEN_READ_ONLY);
		if (fd >= 0) {
			report("kmsg read (EOF)", (int)_KeReadFile(fd, a, 8), 0);
			_KeCloseFile(fd);
		}
	}

	/* /dev/mem: open only. open_port() walks capable() ->
	 * security_locked_down() -> iomem_get_mapping() -> memory_open(), which is
	 * mainline's gate path; reading arbitrary physical addresses is left to an
	 * interactive session, exactly as DCL/mem_test.c does it. */
	fd = opendev("/dev/mem", FILE_OPEN_READ_ONLY);
	if (fd >= 0) {
		note("mem gate path reached (read left to a human)", 1);
		_KeCloseFile(fd);
	}

	/* /dev/hwrng: Xeneva's own node, and the entropy source behind random */
	fd = opendev("/dev/hwrng", FILE_OPEN_READ_ONLY);
	if (fd >= 0) {
		memset(a, 0, sizeof a);
		n = (int)_KeReadFile(fd, a, 16);
		report("hwrng read", n, 16);
		_KeCloseFile(fd);
	}

	/* /dev/dcl: DCL's status node (open + read only) */
	fd = opendev("/dev/dcl", FILE_OPEN_READ_ONLY);
	if (fd >= 0) {
		n = (int)_KeReadFile(fd, a, sizeof a);
		report("dcl status readable", n >= 0, 1);
		note("dcl status bytes", n);
		_KeCloseFile(fd);
	}

	/* /dev/clipboard: a write replaces the contents, a read reports them
	 * without consuming -- pasting twice has to paste twice. */
	fd = opendev("/dev/clipboard", FILE_OPEN_WRITE);
	if (fd >= 0) {
		report("clipboard write", (int)_KeWriteFile(fd, (void*)"hello, dcl", 10),
			   10);
		_KeCloseFile(fd);
	}
	fd = opendev("/dev/clipboard", FILE_OPEN_READ_ONLY);
	if (fd >= 0) {
		char cb[64];
		memset(cb, 0, sizeof cb);
		report("clipboard read", (int)_KeReadFile(fd, cb, sizeof cb), 10);
		report("clipboard bytes", (int)memcmp(cb, "hello, dcl", 10), 0);
		report("clipboard read again", (int)_KeReadFile(fd, cb, sizeof cb), 10);
		_KeCloseFile(fd);
	}

	/* /dev/dri/card0 -- the vkms DRM device. Two ioctls, and both go the
	 * whole way: devfs -> fileserv -> the DCL cdev bridge -> mainline's
	 * file_operations->unlocked_ioctl -> drm_ioctl() -> vkms.
	 *
	 * The ioctl numbers are spelled out because this file cannot include
	 * kernel headers. Both are _IOWR('d', nr, type): direction 3 in the
	 * top two bits, sizeof(type) in bits 16..29, 'd' = 0x64 below that and
	 * nr in the low byte. struct drm_version is 64 bytes (three ints, then
	 * three length/pointer pairs) and struct drm_get_cap is 16, which is
	 * where 0x40 and 0x10 come from.
	 *
	 * The lengths are size_t -- 8 bytes here -- because the kernel's are
	 * __kernel_size_t, which BaseHdr/linux/types.h defines as size_t.
	 * `unsigned long` would be 4 bytes (this app and the kernel are both
	 * aarch64-unknown-windows, which is LLP64), which shifts the three
	 * pointers to offsets 16, 32 and 48; the kernel then reads `desc` from
	 * past the end of this struct and copy_to_user()s the driver string
	 * into stack garbage.
	 */
	{
		struct drm_version_arg {
			int major, minor, patchlevel;
			size_t name_len;
			char* name;
			size_t date_len;
			char* date;
			size_t desc_len;
			char* desc;
		} ver;
		struct drm_get_cap_arg {
			unsigned long long capability;
			unsigned long long value;
		} cap;
		char vname[32], vdesc[64];
		int vfd = opendev("/dev/dri/card0",
				  FILE_OPEN_READ_ONLY | FILE_OPEN_WRITE);

		if (vfd >= 0) {
			memset(&ver, 0, sizeof ver);
			memset(vname, 0, sizeof vname);
			memset(vdesc, 0, sizeof vdesc);
			ver.name = vname;
			ver.name_len = sizeof vname;
			ver.desc = vdesc;
			ver.desc_len = sizeof vdesc;
			report("drm VERSION", _KeFileIoControl(vfd, 0xC0406400, &ver), 0);
			report("drm version major", ver.major, 1);
			report("drm name length", (int)ver.name_len, 4);
			report("drm name is vkms", (int)memcmp(vname, "vkms", 4), 0);
			report("drm desc length", (int)ver.desc_len,
				   (int)strlen("Virtual Kernel Mode Setting"));

			memset(&cap, 0, sizeof cap);
			cap.capability = 0x1;	/* DRM_CAP_DUMB_BUFFER */
			report("drm GET_CAP", _KeFileIoControl(vfd, 0xC010640C, &cap), 0);
			report("drm dumb-buffer cap", (int)cap.value, 1);

			/* What the kernel gates the probes below on: only
			 * overlay planes are listed by GETPLANERESOURCES
			 * until DRM_CLIENT_CAP_UNIVERSAL_PLANES (2), and
			 * the writeback connector is hidden from
			 * GETRESOURCES until DRM_CLIENT_CAP_WRITEBACK_
			 * CONNECTORS (5), which itself requires ATOMIC (3).
			 * _IOW('d', 0x0D, struct drm_set_client_cap): two
			 * __u64s, so 0x10 in the size bits.
			 */
			struct drm_set_client_cap_arg {
				unsigned long long capability, value;
			} scc;

			scc.capability = 2;
			scc.value = 1;
			report("drm kms universal planes cap",
				   _KeFileIoControl(vfd, 0x4010640D, &scc), 0);
			scc.capability = 3;
			report("drm kms atomic cap",
				   _KeFileIoControl(vfd, 0x4010640D, &scc), 0);
			scc.capability = 5;
			report("drm kms writeback connectors cap",
				   _KeFileIoControl(vfd, 0x4010640D, &scc), 0);

			/* The KMS half of the same device.  vkms's default
			 * configuration -- this tree's module params: cursor
			 * on, writeback on, overlay off -- is one CRTC, a
			 * primary and a cursor plane, two encoders (the
			 * config's VIRTUAL one plus the writeback pair's)
			 * and two connectors: the VIRTUAL one (type 15) and
			 * the writeback connector (type 18), sharing the
			 * CRTC.  The caps above are what makes all of them
			 * visible.  GETRESOURCES counts CRTCs, encoders and
			 * connectors; planes are enumerated only by
			 * GETPLANERESOURCES.
			 *
			 * Same _IOWR('d', nr, type) spelling as above:
			 * card_res 0xA0/0x40, mode_crtc 0xA1/0x68 (36 header
			 * bytes plus the 68-byte modeinfo), get_encoder
			 * 0xA6/0x14, get_connector 0xA7/0x50, plane_res
			 * 0xB5/0x10, plane 0xB6/0x20.
			 *
			 * The _ptr fields take arrays of __u32 object ids;
			 * the count fields are both input buffer size and
			 * output true count -- the libdrm two-pass pattern.
			 * GETCONNECTOR's count pass (count_modes 0) is what
			 * makes vkms build the mode list.  The arrays are
			 * static: 128 modes are 8704 bytes and this stack is
			 * small.
			 */
			struct drm_mode_card_res_arg {
				unsigned long long fb_id_ptr, crtc_id_ptr;
				unsigned long long connector_id_ptr;
				unsigned long long encoder_id_ptr;
				unsigned int count_fbs, count_crtcs;
				unsigned int count_connectors, count_encoders;
				unsigned int min_width, max_width;
				unsigned int min_height, max_height;
			} res;
			struct drm_mode_modeinfo_arg {
				unsigned int clock;
				unsigned short hdisplay, hsync_start, hsync_end;
				unsigned short htotal, hskew;
				unsigned short vdisplay, vsync_start, vsync_end;
				unsigned short vtotal, vscan;
				unsigned int vrefresh, flags, type;
				char name[32];
			};
			struct drm_mode_crtc_arg {
				unsigned long long set_connectors_ptr;
				unsigned int count_connectors;
				unsigned int crtc_id, fb_id, x, y;
				unsigned int gamma_size, mode_valid;
				struct drm_mode_modeinfo_arg mode;
			} crtc;
			struct drm_mode_get_connector_arg {
				unsigned long long encoders_ptr, modes_ptr;
				unsigned long long props_ptr, prop_values_ptr;
				unsigned int count_modes, count_props;
				unsigned int count_encoders;
				unsigned int encoder_id, connector_id;
				unsigned int connector_type, connector_type_id;
				unsigned int connection, mm_width, mm_height;
				unsigned int subpixel, pad;
			} conn;
			struct drm_mode_get_encoder_arg {
				unsigned int encoder_id, encoder_type;
				unsigned int crtc_id, possible_crtcs;
				unsigned int possible_clones;
			} enc;
			struct drm_mode_get_plane_res_arg {
				unsigned long long plane_id_ptr;
				unsigned int count_planes;
			} pres;
			struct drm_mode_get_plane_arg {
				unsigned int plane_id, crtc_id, fb_id;
				unsigned int possible_crtcs, gamma_size;
				unsigned int count_format_types;
				unsigned long long format_type_ptr;
			} pl;
			static unsigned int crtc_ids[4], conn_ids[4];
			static unsigned int enc_ids[4], conn_encs[4];
			static unsigned int plane_ids[4], formats[64];
			static struct drm_mode_modeinfo_arg modes[128];
			int virt = -1, wb = -1, prim_xrgb = 0;
			unsigned int virt_type = 0, wb_type = 0;

			memset(&res, 0, sizeof res);
			report("drm kms GETRESOURCES",
				   _KeFileIoControl(vfd, 0xC04064A0, &res), 0);
			report("drm kms crtcs", (int)res.count_crtcs, 1);
			report("drm kms connectors",
				   (int)res.count_connectors, 2);
			report("drm kms encoders", (int)res.count_encoders, 2);
			report("drm kms fbs", (int)res.count_fbs, 0);
			report("drm kms bounds 10..8192",
				   (int)(res.min_width == 10 &&
					 res.min_height == 10 &&
					 res.max_width == 8192 &&
					 res.max_height == 8192), 1);

			res.crtc_id_ptr = (unsigned long long)(size_t)crtc_ids;
			res.count_crtcs = 4;
			res.connector_id_ptr = (unsigned long long)(size_t)conn_ids;
			res.count_connectors = 4;
			res.encoder_id_ptr = (unsigned long long)(size_t)enc_ids;
			res.count_encoders = 4;
			report("drm kms GETRESOURCES (ids)",
				   _KeFileIoControl(vfd, 0xC04064A0, &res), 0);
			report("drm kms crtc id",
				   (int)(crtc_ids[0] != 0), 1);
			report("drm kms connector ids",
				   (int)(conn_ids[0] != 0 && conn_ids[1] != 0 &&
					 conn_ids[0] != conn_ids[1]), 1);
			report("drm kms encoder id",
				   (int)(enc_ids[0] != 0), 1);

			memset(&pres, 0, sizeof pres);
			report("drm kms GETPLANERESOURCES",
				   _KeFileIoControl(vfd, 0xC01064B5, &pres), 0);
			report("drm kms planes", (int)pres.count_planes, 2);
			pres.plane_id_ptr = (unsigned long long)(size_t)plane_ids;
			pres.count_planes = 4;
			report("drm kms GETPLANERESOURCES (ids)",
				   _KeFileIoControl(vfd, 0xC01064B5, &pres), 0);
			report("drm kms plane ids",
				   (int)(plane_ids[0] != 0 && plane_ids[1] != 0 &&
					 plane_ids[0] != plane_ids[1]), 1);

			for (int k = 0; k < 2; k++) {
				memset(&pl, 0, sizeof pl);
				memset(formats, 0, sizeof formats);
				pl.plane_id = plane_ids[k];
				report("drm kms GETPLANE",
					   _KeFileIoControl(vfd, 0xC02064B6, &pl), 0);
				report("drm kms plane formats",
					   (int)(pl.count_format_types > 0), 1);
				report("drm kms plane possible_crtcs",
					   (int)(pl.possible_crtcs & 1), 1);
				pl.format_type_ptr =
					(unsigned long long)(size_t)formats;
				pl.count_format_types = 64;
				_KeFileIoControl(vfd, 0xC02064B6, &pl);
				if (k == 0)
					for (unsigned int f = 0; f < 64; f++)
						/* XRGB8888, as fourcc_code
						 * packs it: 'X','R','2','4' */
						if (formats[f] == 0x34325258)
							prim_xrgb = 1;
			}
			report("drm kms primary takes XRGB8888", prim_xrgb, 1);

			memset(&crtc, 0, sizeof crtc);
			crtc.crtc_id = crtc_ids[0];
			report("drm kms GETCRTC",
				   _KeFileIoControl(vfd, 0xC06864A1, &crtc), 0);
			report("drm kms crtc id round-trip",
				   (int)(crtc.crtc_id == crtc_ids[0]), 1);
			report("drm kms crtc unprogrammed",
				   (int)crtc.mode_valid, 0);

			memset(&enc, 0, sizeof enc);
			enc.encoder_id = enc_ids[0];
			report("drm kms GETENCODER",
				   _KeFileIoControl(vfd, 0xC01464A6, &enc), 0);
			report("drm kms encoder is virtual",
				   (int)enc.encoder_type, 5);
			report("drm kms encoder possible_crtcs",
				   (int)(enc.possible_crtcs & 1), 1);

			memset(&enc, 0, sizeof enc);
			enc.encoder_id = enc_ids[1];
			report("drm kms GETENCODER (second)",
				   _KeFileIoControl(vfd, 0xC01464A6, &enc), 0);

			for (int k = 0; k < 2; k++) {
				memset(&conn, 0, sizeof conn);
				conn.connector_id = conn_ids[k];
				report("drm kms GETCONNECTOR",
					   _KeFileIoControl(vfd, 0xC05064A7, &conn),
					   0);
				/* the split is by type: both connectors
				 * report the DMT mode list, so the mode
				 * count does not tell them apart */
				if (conn.connector_type == 15) {
					virt = k;
					virt_type = conn.connector_type;
				} else {
					wb_type = conn.connector_type;
					wb = k;
				}
			}
			report("drm kms virtual connector found",
				   (int)(virt >= 0), 1);
			report("drm kms virtual connector type",
				   (int)virt_type, 15);
			report("drm kms writeback connector type",
				   (int)wb_type, 18);

			if (virt >= 0) {
				memset(&conn, 0, sizeof conn);
				memset(modes, 0, sizeof modes);
				conn.connector_id = conn_ids[virt];
				conn.modes_ptr =
					(unsigned long long)(size_t)modes;
				conn.count_modes = 128;
				conn.encoders_ptr =
					(unsigned long long)(size_t)conn_encs;
				conn.count_encoders = 4;
				report("drm kms GETCONNECTOR (modes)",
					   _KeFileIoControl(vfd, 0xC05064A7, &conn),
					   0);
				report("drm kms connector connected",
					   (int)conn.connection, 1);
				report("drm kms connector modes fit",
					   (int)(conn.count_modes > 0 &&
						 conn.count_modes <= 128), 1);
				int pref = 0;
				static struct drm_mode_modeinfo_arg mode1024;
				for (unsigned int k = 0;
					 k < conn.count_modes && k < 128; k++)
					if (modes[k].hdisplay == 1024 &&
					    modes[k].vdisplay == 768) {
						pref = 1;
						memcpy(&mode1024, &modes[k],
						       sizeof mode1024);
					}
				report("drm kms preferred mode 1024x768",
					   pref, 1);
				/* encoder_id is the legacy field and
				 * reads 0 here -- nothing has programmed
				 * a CRTC yet.  The connector's encoder is
				 * its possible-encoder list, just copied
				 * into conn_encs. */
				report("drm kms connector encoder id",
					   (int)(conn_encs[0] == enc_ids[0] ||
						 conn_encs[0] == enc_ids[1]), 1);
				report("drm kms connector props",
					   (int)(conn.count_props > 0), 1);

				/* ---- stage 2: dumb buffer -> ADDFB2 -> atomic ---- */
				struct drm_mode_create_dumb_arg {
					unsigned int height, width, bpp, flags;
					unsigned int handle, pitch;
					unsigned long long size;
				} dumb, dumb2;
				struct drm_mode_map_dumb_arg {
					unsigned int handle, pad;
					unsigned long long offset;
				} map, map2;
				struct drm_mode_destroy_dumb_arg {
					unsigned int handle, pad;
				} dd, dd2;
				struct drm_mode_fb_cmd2_arg {
					unsigned int fb_id, width, height;
					unsigned int pixel_format, flags;
					unsigned int handles[4], pitches[4], offsets[4];
					unsigned long long modifier[4];
				} fb, fb2;
				struct drm_mode_create_blob_arg {
					unsigned long long data;
					unsigned int length, blob_id;
				} blob;
				struct drm_mode_atomic_arg {
					unsigned int flags, count_objs;
					unsigned long long objs_ptr, count_props_ptr;
					unsigned long long props_ptr, prop_values_ptr;
					unsigned long long reserved, user_data;
				} at;
				unsigned int conn_crtc_id, crtc_mode_id, crtc_active;
				unsigned int plane_fb_id, plane_crtc_id;
				unsigned int src_x, src_y, src_w, src_h;
				unsigned int crtc_x, crtc_y, crtc_w, crtc_h;
				static unsigned int a_objs[4], a_counts[4];
				static unsigned int a_props[13];
				static unsigned long long a_vals[13];
				int rc;

				/* CREATE_DUMB, 1024x768 XRGB8888: the uapi
				 * promises pitch 4096 and a PAGE_SIZE-aligned
				 * size, 3 MiB. */
				memset(&dumb, 0, sizeof dumb);
				dumb.width = 1024;
				dumb.height = 768;
				dumb.bpp = 32;
				rc = _KeFileIoControl(vfd, 0xC02064B2, &dumb);
				report("drm kms CREATE_DUMB", rc, 0);
				report("drm kms dumb handle",
					   (int)(dumb.handle != 0), 1);
				report("drm kms dumb pitch", (int)dumb.pitch, 4096);
				report("drm kms dumb size",
					   (int)(dumb.size == 3145728ULL), 1);

				/* MAP_DUMB: mainline's handler produces the
				 * fake mmap offset the uapi promises, and DCL's
				 * ioctl tail (drm_stubs.c) replaces it with a
				 * user address this app dereferences directly --
				 * there is no mmap on this target to hand the
				 * offset to. */
				memset(&map, 0, sizeof map);
				map.handle = dumb.handle;
				rc = _KeFileIoControl(vfd, 0xC01064B3, &map);
				report("drm kms MAP_DUMB", rc, 0);
				report("drm kms MAP_DUMB offset",
					   (int)(map.offset != 0 &&
						 (map.offset & 0xFFFull) == 0), 1);

				/* both ends of the frame through the mapping,
				 * written and read back: the pages are really
				 * there and really the object's. */
				{
					volatile unsigned int* pix =
						(volatile unsigned int*)(size_t)map.offset;
					pix[0] = 0x00FF00FFu;
					pix[1024 * 768 - 1] = 0x000000FFu;
					report("drm kms dumb mapping write/read",
						   (int)(pix[0] == 0x00FF00FFu &&
							 pix[1024 * 768 - 1] ==
								 0x000000FFu), 1);
				}

				/* ADDFB2 over the handle, XRGB8888 linear. */
				memset(&fb, 0, sizeof fb);
				fb.width = 1024;
				fb.height = 768;
				fb.pixel_format = 0x34325258; /* 'X','R','2','4' */
				fb.handles[0] = dumb.handle;
				fb.pitches[0] = dumb.pitch;
				rc = _KeFileIoControl(vfd, 0xC06864B8, &fb);
				report("drm kms ADDFB2", rc, 0);
				report("drm kms fb id", (int)(fb.fb_id != 0), 1);

				/* the property ids the atomic request needs. */
				conn_crtc_id = kms_prop_id(vfd, conn_ids[virt],
							   DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
				crtc_mode_id = kms_prop_id(vfd, crtc_ids[0],
							   DRM_MODE_OBJECT_CRTC, "MODE_ID");
				crtc_active = kms_prop_id(vfd, crtc_ids[0],
							  DRM_MODE_OBJECT_CRTC, "ACTIVE");
				plane_fb_id = kms_prop_id(vfd, plane_ids[0],
							  DRM_MODE_OBJECT_PLANE, "FB_ID");
				plane_crtc_id = kms_prop_id(vfd, plane_ids[0],
							    DRM_MODE_OBJECT_PLANE, "CRTC_ID");
				src_x = kms_prop_id(vfd, plane_ids[0],
						    DRM_MODE_OBJECT_PLANE, "SRC_X");
				src_y = kms_prop_id(vfd, plane_ids[0],
						    DRM_MODE_OBJECT_PLANE, "SRC_Y");
				src_w = kms_prop_id(vfd, plane_ids[0],
						    DRM_MODE_OBJECT_PLANE, "SRC_W");
				src_h = kms_prop_id(vfd, plane_ids[0],
						    DRM_MODE_OBJECT_PLANE, "SRC_H");
				crtc_x = kms_prop_id(vfd, plane_ids[0],
						     DRM_MODE_OBJECT_PLANE, "CRTC_X");
				crtc_y = kms_prop_id(vfd, plane_ids[0],
						     DRM_MODE_OBJECT_PLANE, "CRTC_Y");
				crtc_w = kms_prop_id(vfd, plane_ids[0],
						     DRM_MODE_OBJECT_PLANE, "CRTC_W");
				crtc_h = kms_prop_id(vfd, plane_ids[0],
						     DRM_MODE_OBJECT_PLANE, "CRTC_H");
				report("drm kms atomic property ids",
					   (int)(conn_crtc_id && crtc_mode_id &&
						 crtc_active && plane_fb_id &&
						 plane_crtc_id && src_x && src_y &&
						 src_w && src_h && crtc_x && crtc_y &&
						 crtc_w && crtc_h), 1);

				/* the mode as a blob: crtc MODE_ID takes one,
				 * and the one mode the connector offers that
				 * the mm bounds and this app agree on. */
				memset(&blob, 0, sizeof blob);
				blob.data = (unsigned long long)(size_t)&mode1024;
				blob.length = sizeof mode1024;
				rc = _KeFileIoControl(vfd, 0xC01064BD, &blob);
				report("drm kms CREATEPROPBLOB", rc, 0);
				report("drm kms blob id",
					   (int)(blob.blob_id != 0), 1);

				/* the atomic request: connector -> crtc, crtc
				 * mode + active, plane carrying the fb over the
				 * full 1024x768 with a 16.16 source rectangle. */
				a_objs[0] = conn_ids[virt];
				a_counts[0] = 1;
				a_objs[1] = crtc_ids[0];
				a_counts[1] = 2;
				a_objs[2] = plane_ids[0];
				a_counts[2] = 10;
				{
					unsigned int p = 0;
					a_props[p] = conn_crtc_id;
					a_vals[p++] = crtc_ids[0];
					a_props[p] = crtc_mode_id;
					a_vals[p++] = blob.blob_id;
					a_props[p] = crtc_active;
					a_vals[p++] = 1;
					a_props[p] = plane_fb_id;
					a_vals[p++] = fb.fb_id;
					a_props[p] = plane_crtc_id;
					a_vals[p++] = crtc_ids[0];
					a_props[p] = src_x;
					a_vals[p++] = 0;
					a_props[p] = src_y;
					a_vals[p++] = 0;
					a_props[p] = src_w;
					a_vals[p++] = 1024ULL << 16;
					a_props[p] = src_h;
					a_vals[p++] = 768ULL << 16;
					a_props[p] = crtc_x;
					a_vals[p++] = 0;
					a_props[p] = crtc_y;
					a_vals[p++] = 0;
					a_props[p] = crtc_w;
					a_vals[p++] = 1024;
					a_props[p] = crtc_h;
					a_vals[p++] = 768;
				}
				memset(&at, 0, sizeof at);
				at.count_objs = 3;
				at.objs_ptr = (unsigned long long)(size_t)a_objs;
				at.count_props_ptr =
					(unsigned long long)(size_t)a_counts;
				at.props_ptr = (unsigned long long)(size_t)a_props;
				at.prop_values_ptr =
					(unsigned long long)(size_t)a_vals;

				/* TEST_ONLY|ALLOW_MODESET first: the whole
				 * request is vetted before anything is applied.
				 * TEST_ONLY is 0x100 -- 0x200 is NONBLOCK, which
				 * still lands the state for real. */
				at.flags = 0x100 | 0x400;
				report("drm kms atomic TEST_ONLY",
					   _KeFileIoControl(vfd, 0xC03864BC, &at), 0);

				/* and for real.  ALLOW_MODESET covers first-time
				 * mode programming; no PAGE_FLIP_EVENT, this
				 * app has no event queue. */
				at.flags = 0x400;
				report("drm kms atomic commit",
					   _KeFileIoControl(vfd, 0xC03864BC, &at), 0);

				/* GETCRTC now reports the programmed mode:
				 * mode_valid is the driver's enable flag. */
				memset(&crtc, 0, sizeof crtc);
				crtc.crtc_id = crtc_ids[0];
				report("drm kms GETCRTC (programmed)",
					   _KeFileIoControl(vfd, 0xC06864A1, &crtc), 0);
				report("drm kms crtc programmed",
					   (int)crtc.mode_valid, 1);
				report("drm kms crtc mode 1024x768",
					   (int)(crtc.mode.hdisplay == 1024 &&
						 crtc.mode.vdisplay == 768), 1);

				/* ---- stage 4: page flip -> vblank event ----
				 *
				 * A flip to a second framebuffer with
				 * DRM_MODE_PAGE_FLIP_EVENT asked for: the
				 * completion event must come back through
				 * the same fd that asked.  The commit
				 * tail's tick delivers it -- drm_stubs.c.
				 */
				struct drm_mode_crtc_page_flip_arg {
					unsigned int crtc_id, fb_id;
					unsigned int flags, reserved;
					unsigned long long user_data;
				} flip;
				struct drm_event_vblank_arg {
					unsigned int type, length;
					unsigned long long user_data;
					unsigned int tv_sec, tv_usec;
					unsigned int sequence, crtc_id;
				} ev;

				memset(&dumb2, 0, sizeof dumb2);
				dumb2.width = 1024;
				dumb2.height = 768;
				dumb2.bpp = 32;
				report("drm kms flip CREATE_DUMB",
					   _KeFileIoControl(vfd, 0xC02064B2,
							    &dumb2), 0);
				memset(&map2, 0, sizeof map2);
				map2.handle = dumb2.handle;
				report("drm kms flip MAP_DUMB",
					   _KeFileIoControl(vfd, 0xC01064B3,
							    &map2), 0);
				memset(&fb2, 0, sizeof fb2);
				fb2.width = 1024;
				fb2.height = 768;
				fb2.pixel_format = 0x34325258; /* 'X','R','2','4' */
				fb2.handles[0] = dumb2.handle;
				fb2.pitches[0] = dumb2.pitch;
				report("drm kms flip ADDFB2",
					   _KeFileIoControl(vfd, 0xC06864B8, &fb2), 0);

				/* the flip itself: 0xC01864B0 is
				 * _IOWR('d', 0xB0, 24), flags 0x01 is
				 * PAGE_FLIP_EVENT, and user_data rides
				 * through to the event unchanged. */
				memset(&flip, 0, sizeof flip);
				flip.crtc_id = crtc_ids[0];
				flip.fb_id = fb2.fb_id;
				flip.flags = 0x01;
				flip.user_data = 0x50494C46ULL; /* 'FLIP' */
				report("drm kms flip event request",
					   _KeFileIoControl(vfd, 0xC01864B0, &flip), 0);

				/* the commit tail's tick ran inside the
				 * ioctl, so the event is already queued:
				 * one 32-byte drm_event_vblank record --
				 * header, the user_data above, vblank
				 * timestamp and sequence. */
				memset(&ev, 0, sizeof ev);
				report("drm kms flip event read",
					   (int)_KeReadFile(vfd, &ev, sizeof ev),
					   (int)sizeof ev);
				report("drm kms flip event type",
					   (int)ev.type, 0x02); /* FLIP_COMPLETE */
				report("drm kms flip event length",
					   (int)ev.length, (int)sizeof ev);
				report("drm kms flip event user data",
					   (int)(ev.user_data == 0x50494C46ULL), 1);
				report("drm kms flip event crtc",
					   (int)ev.crtc_id, (int)crtc_ids[0]);
				report("drm kms flip event sequence",
					   (int)(ev.sequence != 0), 1);

				/* flip back without EVENT: nothing more to
				 * read, and the plane carries the stage-2
				 * fb again when the disable below runs. */
				flip.fb_id = fb.fb_id;
				flip.flags = 0;
				report("drm kms flip back",
					   _KeFileIoControl(vfd, 0xC01864B0, &flip), 0);

				/* fb2 is unbound again: RMFB then
				 * DESTROY_DUMB. */
				{
					unsigned int rmfb_id = fb2.fb_id;
					report("drm kms flip RMFB",
						   _KeFileIoControl(vfd, 0xC00464AF,
								    &rmfb_id), 0);
				}
				memset(&dd2, 0, sizeof dd2);
				dd2.handle = dumb2.handle;
				report("drm kms flip DESTROY_DUMB",
					   _KeFileIoControl(vfd, 0xC00864B4, &dd2), 0);

				/* disable: plane off, crtc inactive with its
				 * mode cleared, connector detached -- the same
				 * three objects again.  MODE_ID=0 is what turns
				 * enable off; a crtc left "enabled" with no
				 * connectors attached is rejected by the helper
				 * (enabled/connectors mismatch). */
				a_counts[0] = 1;
				a_props[0] = conn_crtc_id;
				a_vals[0] = 0;
				a_counts[1] = 2;
				a_props[1] = crtc_active;
				a_vals[1] = 0;
				a_props[2] = crtc_mode_id;
				a_vals[2] = 0;
				a_counts[2] = 2;
				a_props[3] = plane_fb_id;
				a_vals[3] = 0;
				a_props[4] = plane_crtc_id;
				a_vals[4] = 0;
				at.flags = 0x400;
				report("drm kms atomic disable",
					   _KeFileIoControl(vfd, 0xC03864BC, &at), 0);
				memset(&crtc, 0, sizeof crtc);
				crtc.crtc_id = crtc_ids[0];
				_KeFileIoControl(vfd, 0xC06864A1, &crtc);
				report("drm kms crtc unprogrammed again",
					   (int)crtc.mode_valid, 0);

				/* RMFB then DESTROY_DUMB, then the object is
				 * gone: a second MAP_DUMB fails the lookup. */
				{
					unsigned int rmfb_id = fb.fb_id;
					report("drm kms RMFB",
						   _KeFileIoControl(vfd, 0xC00464AF,
								    &rmfb_id), 0);
				}
				memset(&dd, 0, sizeof dd);
				dd.handle = dumb.handle;
				report("drm kms DESTROY_DUMB",
					   _KeFileIoControl(vfd, 0xC00864B4, &dd), 0);
				memset(&map, 0, sizeof map);
				map.handle = dumb.handle;
				report("drm kms MAP_DUMB after destroy fails",
					   (int)(_KeFileIoControl(vfd, 0xC01064B3,
								  &map) != 0), 1);

				/* ---- stage 3: writeback, the pixel proof ----
				 *
				 * The frame the app paints must come back
				 * composed through the writeback connector:
				 * source buffer -> (composer) -> capture
				 * buffer, both reached through this app's
				 * own mappings.  The pattern is what makes
				 * it a pixel proof and not a plumbing
				 * proof -- see wb_pat() above.
				 *
				 * The wb job is one-shot: WRITEBACK_FB_ID
				 * applies to this commit only and reads
				 * back 0.  No out-fence is asked for -- the
				 * completion half (drm_writeback_signal_
				 * completion) has already run when the
				 * ioctl returns, because DCL's vblank tick
				 * runs the composer inline; see
				 * DCL/drm_stubs.c.
				 */
				memset(&dumb, 0, sizeof dumb);
				dumb.width = 1024;
				dumb.height = 768;
				dumb.bpp = 32;
				report("drm kms wb src CREATE_DUMB",
					   _KeFileIoControl(vfd, 0xC02064B2,
							    &dumb), 0);
				memset(&dumb2, 0, sizeof dumb2);
				dumb2.width = 1024;
				dumb2.height = 768;
				dumb2.bpp = 32;
				report("drm kms wb capture CREATE_DUMB",
					   _KeFileIoControl(vfd, 0xC02064B2,
							    &dumb2), 0);

				memset(&map, 0, sizeof map);
				map.handle = dumb.handle;
				report("drm kms wb src MAP_DUMB",
					   _KeFileIoControl(vfd, 0xC01064B3,
							    &map), 0);
				memset(&map2, 0, sizeof map2);
				map2.handle = dumb2.handle;
				report("drm kms wb capture MAP_DUMB",
					   _KeFileIoControl(vfd, 0xC01064B3,
							    &map2), 0);

				/* paint the source through the mapping;
				 * the capture buffer is only ever read
				 * afterwards. */
				{
					volatile unsigned int* src =
						(volatile unsigned int*)(size_t)
							map.offset;
					for (int y = 0; y < 768; y++)
						for (int x = 0; x < 1024; x++)
							src[y * 1024 + x] =
								wb_pat(x, y);
				}

				memset(&fb, 0, sizeof fb);
				fb.width = 1024;
				fb.height = 768;
				fb.pixel_format = 0x34325258; /* 'X','R','2','4' */
				fb.handles[0] = dumb.handle;
				fb.pitches[0] = dumb.pitch;
				report("drm kms wb src ADDFB2",
					   _KeFileIoControl(vfd, 0xC06864B8, &fb), 0);
				memset(&fb2, 0, sizeof fb2);
				fb2.width = 1024;
				fb2.height = 768;
				fb2.pixel_format = 0x34325258;
				fb2.handles[0] = dumb2.handle;
				fb2.pitches[0] = dumb2.pitch;
				report("drm kms wb capture ADDFB2",
					   _KeFileIoControl(vfd, 0xC06864B8, &fb2), 0);

				/* the writeback connector's two atomic
				 * properties: the standard CRTC_ID and
				 * WRITEBACK_FB_ID, which only exists once
				 * the client opted into the writeback caps
				 * at the top of this block. */
				unsigned int wb_conn_id =
					(wb >= 0) ? conn_ids[wb] : 0;
				unsigned int wb_crtc_id = kms_prop_id(vfd,
					wb_conn_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
				unsigned int wb_fb_id = kms_prop_id(vfd,
					wb_conn_id, DRM_MODE_OBJECT_CONNECTOR,
					"WRITEBACK_FB_ID");
				report("drm kms wb atomic property ids",
					   (int)(wb_crtc_id != 0 && wb_fb_id != 0), 1);
				report("drm kms wb pixel formats prop",
					   (int)(kms_prop_id(vfd, wb_conn_id,
							     DRM_MODE_OBJECT_CONNECTOR,
							     "WRITEBACK_PIXEL_FORMATS") != 0), 1);

				/* the pipeline back up: connector, crtc and
				 * plane exactly as stage 2 committed them
				 * (the mode blob is still there). */
				a_objs[0] = conn_ids[virt];
				a_counts[0] = 1;
				a_objs[1] = crtc_ids[0];
				a_counts[1] = 2;
				a_objs[2] = plane_ids[0];
				a_counts[2] = 10;
				{
					unsigned int p = 0;
					a_props[p] = conn_crtc_id;
					a_vals[p++] = crtc_ids[0];
					a_props[p] = crtc_mode_id;
					a_vals[p++] = blob.blob_id;
					a_props[p] = crtc_active;
					a_vals[p++] = 1;
					a_props[p] = plane_fb_id;
					a_vals[p++] = fb.fb_id;
					a_props[p] = plane_crtc_id;
					a_vals[p++] = crtc_ids[0];
					a_props[p] = src_x;
					a_vals[p++] = 0;
					a_props[p] = src_y;
					a_vals[p++] = 0;
					a_props[p] = src_w;
					a_vals[p++] = 1024ULL << 16;
					a_props[p] = src_h;
					a_vals[p++] = 768ULL << 16;
					a_props[p] = crtc_x;
					a_vals[p++] = 0;
					a_props[p] = crtc_y;
					a_vals[p++] = 0;
					a_props[p] = crtc_w;
					a_vals[p++] = 1024;
					a_props[p] = crtc_h;
					a_vals[p++] = 768;
				}
				memset(&at, 0, sizeof at);
				at.count_objs = 3;
				at.objs_ptr = (unsigned long long)(size_t)a_objs;
				at.count_props_ptr =
					(unsigned long long)(size_t)a_counts;
				at.props_ptr = (unsigned long long)(size_t)a_props;
				at.prop_values_ptr =
					(unsigned long long)(size_t)a_vals;
				at.flags = 0x400;
				report("drm kms wb pipeline commit",
					   _KeFileIoControl(vfd, 0xC03864BC, &at), 0);

				/* the capture request: attach the writeback
				 * connector to the crtc and hand it the
				 * capture fb for this one commit. */
				static unsigned int w_props[2];
				static unsigned long long w_vals[2];
				a_objs[0] = wb_conn_id;
				a_counts[0] = 2;
				w_props[0] = wb_crtc_id;
				w_vals[0] = crtc_ids[0];
				w_props[1] = wb_fb_id;
				w_vals[1] = fb2.fb_id;
				memset(&at, 0, sizeof at);
				at.count_objs = 1;
				at.objs_ptr = (unsigned long long)(size_t)a_objs;
				at.count_props_ptr =
					(unsigned long long)(size_t)a_counts;
				at.props_ptr = (unsigned long long)(size_t)w_props;
				at.prop_values_ptr =
					(unsigned long long)(size_t)w_vals;
				at.flags = 0x400;
				report("drm kms wb capture commit",
					   _KeFileIoControl(vfd, 0xC03864BC, &at), 0);

				/* the frame came back: the composer read the
				 * source through the kernel's mapping, blended
				 * it, and wrote the capture buffer through the
				 * wb job's -- all of it before the ioctl
				 * returned. */
				{
					volatile unsigned int* cap =
						(volatile unsigned int*)(size_t)
							map2.offset;
					int row_ok;

					report("drm kms wb capture pixel (0,0)",
						   (int)(cap[0] ==
							 (WB_CAP_MASK |
							  wb_pat(0, 0))), 1);
					report("drm kms wb capture pixel center",
						   (int)(cap[384 * 1024 + 512] ==
							 (WB_CAP_MASK |
							  wb_pat(512, 384))), 1);
					report("drm kms wb capture pixel (1023,767)",
						   (int)(cap[767 * 1024 + 1023] ==
							 (WB_CAP_MASK |
							  wb_pat(1023, 767))), 1);
					row_ok = 1;
					for (int x = 0; x < 1024; x++)
						if (cap[100 * 1024 + x] !=
						    (WB_CAP_MASK |
						     wb_pat(x, 100)))
							row_ok = 0;
					report("drm kms wb capture row 100",
						   row_ok, 1);
					row_ok = 1;
					for (int x = 0; x < 1024; x++)
						if (cap[767 * 1024 + x] !=
						    (WB_CAP_MASK |
						     wb_pat(x, 767)))
							row_ok = 0;
					report("drm kms wb capture row 767",
						   row_ok, 1);
				}

				/* tear down: the writeback connector detaches
				 * and drops its spent job (the core's check
				 * releases a job with no fb), the main
				 * connector detaches, plane and crtc go back
				 * off. */
				a_objs[0] = conn_ids[virt];
				a_counts[0] = 1;
				a_objs[1] = wb_conn_id;
				a_counts[1] = 2;
				a_objs[2] = crtc_ids[0];
				a_counts[2] = 2;
				a_objs[3] = plane_ids[0];
				a_counts[3] = 2;
				a_props[0] = conn_crtc_id;
				a_vals[0] = 0;
				a_props[1] = wb_crtc_id;
				a_vals[1] = 0;
				a_props[2] = wb_fb_id;
				a_vals[2] = 0;
				a_props[3] = crtc_active;
				a_vals[3] = 0;
				a_props[4] = crtc_mode_id;
				a_vals[4] = 0;
				a_props[5] = plane_fb_id;
				a_vals[5] = 0;
				a_props[6] = plane_crtc_id;
				a_vals[6] = 0;
				memset(&at, 0, sizeof at);
				at.count_objs = 4;
				at.objs_ptr = (unsigned long long)(size_t)a_objs;
				at.count_props_ptr =
					(unsigned long long)(size_t)a_counts;
				at.props_ptr = (unsigned long long)(size_t)a_props;
				at.prop_values_ptr =
					(unsigned long long)(size_t)a_vals;
				at.flags = 0x400;
				report("drm kms wb atomic disable",
					   _KeFileIoControl(vfd, 0xC03864BC, &at), 0);

				{
					unsigned int rmfb_id = fb.fb_id;
					report("drm kms wb RMFB src",
						   _KeFileIoControl(vfd, 0xC00464AF,
								    &rmfb_id), 0);
					rmfb_id = fb2.fb_id;
					report("drm kms wb RMFB capture",
						   _KeFileIoControl(vfd, 0xC00464AF,
								    &rmfb_id), 0);
				}
				memset(&dd, 0, sizeof dd);
				dd.handle = dumb.handle;
				report("drm kms wb DESTROY_DUMB src",
					   _KeFileIoControl(vfd, 0xC00864B4, &dd), 0);
				memset(&dd2, 0, sizeof dd2);
				dd2.handle = dumb2.handle;
				report("drm kms wb DESTROY_DUMB capture",
					   _KeFileIoControl(vfd, 0xC00864B4, &dd2), 0);
			}
			_KeCloseFile(vfd);
		}
	}

	/* ── stage 5: the vendored tiny/bochs.c ───────────────────────────────
	 *
	 * When QEMU exposes a bochs-display, this is the tree's first PCI
	 * driver: no virtio rings anywhere, matched by the DCL PCI fabric
	 * (pci_shim.c) against 1234:1111 and bound at 0000:00:04.0.  The
	 * card is found by the name its DRM VERSION reports, not by index:
	 * whether bochs is present, and where it lands, is QEMU's decision.
	 * Current upstream does not add a bochs-display by default, so this
	 * stage skips -- and says so -- unless the machine provides one.
	 *
	 * Every uapi shape below mirrors the vkms block above -- same two-pass
	 * ioctls, same property names -- but the half userspace cannot see is
	 * the point: the commit's plane update memcpys the shadow buffer into
	 * the device's BAR0, which QEMU scans out.  The real pixel proof is
	 * taken from the host side (the gate screendumps QEMU's monitor and
	 * reads the pixels), so this block commits one known pattern and
	 * marks the moment in the log for the gate to wait on:
	 *
	 *     [dcltest] info bochs scanout committed      (the note() below)
	 *
	 * The pattern is 0x00123456 -- XRGB8888, so R 0x12, G 0x34, B 0x56 --
	 * written across the whole mapped buffer, which makes the check
	 * stride-proof: every way of sampling the buffer sees the same
	 * colour.
	 *
	 * One difference from the vkms block: the mode is not fixed at
	 * 1024x768.  bochs's connector offers its EDID's modes when the
	 * hardware has an EDID (QEMU's does) and defx x defy otherwise, so
	 * the app takes the one entry DRM marks DRM_MODE_TYPE_PREFERRED and
	 * sizes the dumb buffer, the framebuffer and both rectangles from it.
	 */
	{
		struct drm_version_arg {
			int major, minor, patchlevel;
			size_t name_len;
			char* name;
			size_t date_len;
			char* date;
			size_t desc_len;
			char* desc;
		} ver;
		struct drm_get_cap_arg {
			unsigned long long capability;
			unsigned long long value;
		} cap;
		struct drm_set_client_cap_arg {
			unsigned long long capability;
			unsigned long long value;
		} scc;
		struct drm_mode_card_res_arg {
			unsigned long long fb_id_ptr, crtc_id_ptr;
			unsigned long long connector_id_ptr;
			unsigned long long encoder_id_ptr;
			unsigned int count_fbs, count_crtcs;
			unsigned int count_connectors, count_encoders;
			unsigned int min_width, max_width;
			unsigned int min_height, max_height;
		} res;
		struct drm_mode_modeinfo_arg {
			unsigned int clock;
			unsigned short hdisplay, hsync_start, hsync_end;
			unsigned short htotal, hskew;
			unsigned short vdisplay, vsync_start, vsync_end;
			unsigned short vtotal, vscan;
			unsigned int vrefresh, flags, type;
			char name[32];
		};
		struct drm_mode_get_connector_arg {
			unsigned long long encoders_ptr, modes_ptr;
			unsigned long long props_ptr, prop_values_ptr;
			unsigned int count_modes, count_props;
			unsigned int count_encoders;
			unsigned int encoder_id, connector_id;
			unsigned int connector_type, connector_type_id;
			unsigned int connection, mm_width, mm_height;
			unsigned int subpixel, pad;
		} conn;
		struct drm_mode_get_plane_res_arg {
			unsigned long long plane_id_ptr;
			unsigned int count_planes;
		} pres;
		struct drm_mode_get_plane_arg {
			unsigned int plane_id, crtc_id, fb_id;
			unsigned int possible_crtcs, gamma_size;
			unsigned int count_format_types;
			unsigned long long format_type_ptr;
		} pl;
		struct drm_mode_create_dumb_arg {
			unsigned int height, width, bpp, flags;
			unsigned int handle, pitch;
			unsigned long long size;
		} dumb;
		struct drm_mode_map_dumb_arg {
			unsigned int handle, pad;
			unsigned long long offset;
		} map;
		struct drm_mode_fb_cmd2_arg {
			unsigned int fb_id, width, height;
			unsigned int pixel_format, flags;
			unsigned int handles[4], pitches[4], offsets[4];
			unsigned long long modifier[4];
		} fb;
		struct drm_mode_create_blob_arg {
			unsigned long long data;
			unsigned int length, blob_id;
		} blob;
		struct drm_mode_atomic_arg {
			unsigned int flags, count_objs;
			unsigned long long objs_ptr, count_props_ptr;
			unsigned long long props_ptr, prop_values_ptr;
			unsigned long long reserved, user_data;
		} at;
		struct drm_mode_crtc_arg {
			unsigned long long set_connectors_ptr;
			unsigned int count_connectors;
			unsigned int crtc_id, fb_id, x, y;
			unsigned int gamma_size, mode_valid;
			struct drm_mode_modeinfo_arg mode;
		} prog;
		static unsigned int crtc_ids[4], conn_ids[4], plane_ids[4];
		static unsigned int enc_ids[4];
		static unsigned int a_objs[4], a_counts[4];
		static unsigned int a_props[13];
		static unsigned long long a_vals[13];
		static struct drm_mode_modeinfo_arg modes[128];
		unsigned int conn_crtc_id, crtc_mode_id, crtc_active;
		unsigned int plane_fb_id, plane_crtc_id;
		unsigned int src_x, src_y, src_w, src_h;
		unsigned int crtc_x, crtc_y, crtc_w, crtc_h;
		unsigned int bw = 0, bh = 0, k, pick = 0;
		char bname[32], bdesc[64];
		char bpath[32];
		int bfd = -1;

		if (drm_find_card("bochs-drm", bpath, sizeof bpath) == 0) {
			char line[96];
			sprintf(line, "[dcltest] info bochs at %s\n", bpath);
			emit(line);
			bfd = opendev(bpath,
				      FILE_OPEN_READ_ONLY | FILE_OPEN_WRITE);
		} else {
			emit("[dcltest] info bochs-drm absent, stage 5 skipped\n");
		}

		if (bfd >= 0) {
			/* identity first: this is the PCI device's driver,
			 * not a second name for card0. */
			memset(&ver, 0, sizeof ver);
			memset(bname, 0, sizeof bname);
			memset(bdesc, 0, sizeof bdesc);
			ver.name = bname;
			ver.name_len = sizeof bname;
			ver.desc = bdesc;
			ver.desc_len = sizeof bdesc;
			report("drm bochs VERSION",
			       _KeFileIoControl(bfd, 0xC0406400, &ver), 0);
			report("drm bochs version major", ver.major, 1);
			report("drm bochs name is bochs-drm",
			       (int)memcmp(bname, "bochs-drm", 9), 0);
			report("drm bochs desc length", (int)ver.desc_len,
			       (int)strlen("bochs dispi vga interface (qemu stdvga)"));

			memset(&cap, 0, sizeof cap);
			cap.capability = 0x1;	/* DRM_CAP_DUMB_BUFFER */
			report("drm bochs GET_CAP",
			       _KeFileIoControl(bfd, 0xC010640C, &cap), 0);
			report("drm bochs dumb-buffer cap", (int)cap.value, 1);

			/* The caps the vkms block sets too: universal planes
			 * makes the primary plane listable, atomic makes the
			 * commit legal.  Both are _IOW('d', 0x0D). */
			scc.capability = 2;
			scc.value = 1;
			report("drm bochs universal planes cap",
			       _KeFileIoControl(bfd, 0x4010640D, &scc), 0);
			scc.capability = 3;
			report("drm bochs atomic cap",
			       _KeFileIoControl(bfd, 0x4010640D, &scc), 0);

			/* bochs registers one CRTC, one encoder and one
			 * connector.  The connector needs no hotplug poll:
			 * with no detect hook the probe helper treats the
			 * status as always connected -- so it is, and the
			 * first GETCONNECTOR is what builds the mode list. */
			memset(&res, 0, sizeof res);
			report("drm bochs GETRESOURCES",
			       _KeFileIoControl(bfd, 0xC04064A0, &res), 0);
			report("drm bochs crtcs", (int)res.count_crtcs, 1);
			report("drm bochs connectors",
			       (int)res.count_connectors, 1);
			report("drm bochs encoders", (int)res.count_encoders, 1);
			report("drm bochs fbs", (int)res.count_fbs, 0);

			res.crtc_id_ptr = (unsigned long long)(size_t)crtc_ids;
			res.count_crtcs = 4;
			res.connector_id_ptr =
				(unsigned long long)(size_t)conn_ids;
			res.count_connectors = 4;
			res.encoder_id_ptr =
				(unsigned long long)(size_t)enc_ids;
			res.count_encoders = 4;
			report("drm bochs GETRESOURCES (ids)",
			       _KeFileIoControl(bfd, 0xC04064A0, &res), 0);
			report("drm bochs crtc id",
			       (int)(crtc_ids[0] != 0), 1);
			report("drm bochs connector id",
			       (int)(conn_ids[0] != 0), 1);
			report("drm bochs encoder id",
			       (int)(enc_ids[0] != 0), 1);

			memset(&conn, 0, sizeof conn);
			conn.connector_id = conn_ids[0];
			report("drm bochs GETCONNECTOR (counts)",
			       _KeFileIoControl(bfd, 0xC05064A7, &conn), 0);
			report("drm bochs connector connected",
			       (int)(conn.connection == 1), 1);
			report("drm bochs connector has modes",
			       (int)(conn.count_modes > 0), 1);

			/* the list pass: 128 slots are more than enough for
			 * either source of modes, and encoders_ptr has to be
			 * real too -- the kernel fills it whenever
			 * count_encoders covers the connector's encoders,
			 * which the count pass above just reported. */
			memset(&conn, 0, sizeof conn);
			conn.connector_id = conn_ids[0];
			conn.modes_ptr = (unsigned long long)(size_t)modes;
			conn.count_modes = 128;
			conn.encoders_ptr = (unsigned long long)(size_t)enc_ids;
			conn.count_encoders = 4;
			report("drm bochs GETCONNECTOR (modes)",
			       _KeFileIoControl(bfd, 0xC05064A7, &conn), 0);
			report("drm bochs connector mode count",
			       (int)(conn.count_modes >= 1 &&
				     conn.count_modes <= 128), 1);

			/* the preferred entry: DRM marks exactly one mode
			 * DRM_MODE_TYPE_PREFERRED (1<<3) -- the EDID's
			 * first detailed timing when the hardware has an
			 * EDID, defx x defy when it does not. */
			for (k = 0; k < conn.count_modes && k < 128; k++) {
				if (modes[k].type & 0x8) {
					pick = k;
					break;
				}
			}
			bw = modes[pick].hdisplay;
			bh = modes[pick].vdisplay;
			note("bochs mode width", (int)bw);
			note("bochs mode height", (int)bh);
			report("drm bochs mode picked",
			       (int)(bw > 0 && bh > 0), 1);
			/* the bound bochs's mode_valid enforces: the mode
			 * has to fit the 16 MiB BAR0 at 32 bpp. */
			report("drm bochs mode fits vram",
			       (int)((unsigned long long)bw * bh * 4 <=
				     16ULL * 1024 * 1024), 1);

			pres.plane_id_ptr =
				(unsigned long long)(size_t)plane_ids;
			pres.count_planes = 4;
			report("drm bochs GETPLANERESOURCES",
			       _KeFileIoControl(bfd, 0xC01064B5, &pres), 0);
			report("drm bochs primary planes",
			       (int)pres.count_planes, 1);

			memset(&pl, 0, sizeof pl);
			pl.plane_id = plane_ids[0];
			report("drm bochs GETPLANE",
			       _KeFileIoControl(bfd, 0xC02064B6, &pl), 0);
			report("drm bochs plane on crtc 0",
			       (int)((pl.possible_crtcs & 1) == 1), 1);

			conn_crtc_id = kms_prop_id(bfd, conn_ids[0],
						   DRM_MODE_OBJECT_CONNECTOR,
						   "CRTC_ID");
			crtc_mode_id = kms_prop_id(bfd, crtc_ids[0],
						   DRM_MODE_OBJECT_CRTC,
						   "MODE_ID");
			crtc_active = kms_prop_id(bfd, crtc_ids[0],
						  DRM_MODE_OBJECT_CRTC,
						  "ACTIVE");
			plane_fb_id = kms_prop_id(bfd, plane_ids[0],
						  DRM_MODE_OBJECT_PLANE,
						  "FB_ID");
			plane_crtc_id = kms_prop_id(bfd, plane_ids[0],
						    DRM_MODE_OBJECT_PLANE,
						    "CRTC_ID");
			src_x = kms_prop_id(bfd, plane_ids[0],
					    DRM_MODE_OBJECT_PLANE, "SRC_X");
			src_y = kms_prop_id(bfd, plane_ids[0],
					    DRM_MODE_OBJECT_PLANE, "SRC_Y");
			src_w = kms_prop_id(bfd, plane_ids[0],
					    DRM_MODE_OBJECT_PLANE, "SRC_W");
			src_h = kms_prop_id(bfd, plane_ids[0],
					    DRM_MODE_OBJECT_PLANE, "SRC_H");
			crtc_x = kms_prop_id(bfd, plane_ids[0],
					     DRM_MODE_OBJECT_PLANE, "CRTC_X");
			crtc_y = kms_prop_id(bfd, plane_ids[0],
					     DRM_MODE_OBJECT_PLANE, "CRTC_Y");
			crtc_w = kms_prop_id(bfd, plane_ids[0],
					     DRM_MODE_OBJECT_PLANE, "CRTC_W");
			crtc_h = kms_prop_id(bfd, plane_ids[0],
					     DRM_MODE_OBJECT_PLANE, "CRTC_H");
			report("drm bochs atomic property ids",
			       (int)(conn_crtc_id && crtc_mode_id &&
				     crtc_active && plane_fb_id &&
				     plane_crtc_id && src_x && src_y &&
				     src_w && src_h && crtc_x && crtc_y &&
				     crtc_w && crtc_h), 1);

			/* the mode as a blob for crtc MODE_ID */
			memset(&blob, 0, sizeof blob);
			blob.data = (unsigned long long)(size_t)&modes[pick];
			blob.length = sizeof modes[pick];
			report("drm bochs CREATEPROPBLOB",
			       _KeFileIoControl(bfd, 0xC01064BD, &blob), 0);
			report("drm bochs blob id",
			       (int)(blob.blob_id != 0), 1);

			memset(&dumb, 0, sizeof dumb);
			dumb.width = bw;
			dumb.height = bh;
			dumb.bpp = 32;
			report("drm bochs CREATE_DUMB",
			       _KeFileIoControl(bfd, 0xC02064B2, &dumb), 0);
			report("drm bochs dumb handle",
			       (int)(dumb.handle != 0), 1);
			report("drm bochs dumb pitch",
			       (int)dumb.pitch, (int)bw * 4);
			report("drm bochs dumb size",
			       (int)(dumb.size ==
				     ((((unsigned long long)dumb.pitch * bh) +
				       4095) & ~4095ULL)), 1);

			/* the pattern, over the whole mapping: whatever
			 * stride QEMU ends up scanning with, every pixel
			 * the surface can show is this colour. */
			memset(&map, 0, sizeof map);
			map.handle = dumb.handle;
			report("drm bochs MAP_DUMB",
			       _KeFileIoControl(bfd, 0xC01064B3, &map), 0);
			{
				volatile unsigned int* pix =
					(volatile unsigned int*)(size_t)
						map.offset;
				unsigned long long words = dumb.size >> 2;
				for (unsigned long long i = 0; i < words; i++)
					pix[i] = 0x00123456u;
				report("drm bochs pattern written",
				       (int)(pix[0] == 0x00123456u), 1);
			}

			memset(&fb, 0, sizeof fb);
			fb.width = bw;
			fb.height = bh;
			fb.pixel_format = 0x34325258; /* 'X','R','2','4' */
			fb.handles[0] = dumb.handle;
			fb.pitches[0] = dumb.pitch;
			report("drm bochs ADDFB2",
			       _KeFileIoControl(bfd, 0xC06864B8, &fb), 0);
			report("drm bochs fb id", (int)(fb.fb_id != 0), 1);

			/* the atomic request: connector -> crtc, crtc mode +
			 * active, plane carrying the fb over the full mode
			 * with a 16.16 source rectangle. */
			a_objs[0] = conn_ids[0];
			a_counts[0] = 1;
			a_objs[1] = crtc_ids[0];
			a_counts[1] = 2;
			a_objs[2] = plane_ids[0];
			a_counts[2] = 10;
			{
				unsigned int p = 0;
				a_props[p] = conn_crtc_id;
				a_vals[p++] = crtc_ids[0];
				a_props[p] = crtc_mode_id;
				a_vals[p++] = blob.blob_id;
				a_props[p] = crtc_active;
				a_vals[p++] = 1;
				a_props[p] = plane_fb_id;
				a_vals[p++] = fb.fb_id;
				a_props[p] = plane_crtc_id;
				a_vals[p++] = crtc_ids[0];
				a_props[p] = src_x;
				a_vals[p++] = 0;
				a_props[p] = src_y;
				a_vals[p++] = 0;
				a_props[p] = src_w;
				a_vals[p++] = (unsigned long long)bw << 16;
				a_props[p] = src_h;
				a_vals[p++] = (unsigned long long)bh << 16;
				a_props[p] = crtc_x;
				a_vals[p++] = 0;
				a_props[p] = crtc_y;
				a_vals[p++] = 0;
				a_props[p] = crtc_w;
				a_vals[p++] = bw;
				a_props[p] = crtc_h;
				a_vals[p++] = bh;
			}
			memset(&at, 0, sizeof at);
			at.count_objs = 3;
			at.objs_ptr = (unsigned long long)(size_t)a_objs;
			at.count_props_ptr =
				(unsigned long long)(size_t)a_counts;
			at.props_ptr = (unsigned long long)(size_t)a_props;
			at.prop_values_ptr =
				(unsigned long long)(size_t)a_vals;

			/* TEST_ONLY|ALLOW_MODESET vets the request; then
			 * the same request for real.  This is the commit
			 * whose plane update writes the pattern into BAR0 --
			 * after it returns, QEMU is scanning it out. */
			at.flags = 0x100 | 0x400;
			report("drm bochs atomic TEST_ONLY",
			       _KeFileIoControl(bfd, 0xC03864BC, &at), 0);
			at.flags = 0x400;
			report("drm bochs atomic commit",
			       _KeFileIoControl(bfd, 0xC03864BC, &at), 0);

			memset(&prog, 0, sizeof prog);
			prog.crtc_id = crtc_ids[0];
			report("drm bochs GETCRTC",
			       _KeFileIoControl(bfd, 0xC06864A1, &prog), 0);
			report("drm bochs crtc programmed",
			       (int)prog.mode_valid, 1);
			report("drm bochs crtc mode matches",
			       (int)(prog.mode.hdisplay == bw &&
				     prog.mode.vdisplay == bh), 1);

			/* the marker the gate waits for before screendumping
			 * bochs0; the pattern stays on the scanout, nothing
			 * else draws on the bochs card. */
			note("bochs scanout committed", 1);
			_KeCloseFile(bfd);
		}
	}

	/* stage 6: the vendored virtio-gpu, found by driver name.  Absent
	 * images skip silently; the bypass gate is what makes it appear. */
	{
		char gpath[32];
		if (drm_find_card("virtio_gpu", gpath, sizeof gpath) == 0) {
			char line[96];
			sprintf(line, "[dcltest] info virtio-gpu at %s\n", gpath);
			emit(line);
			test_virtgpu_card(gpath);
		} else {
			emit("[dcltest] info virtio_gpu card absent, stage 6 skipped\n");
		}
	}

	/* privileged device nodes belong to the daemons group; an ordinary process (this
	 * one has world only) must be refused even when it asks for a write-mode open,
	 * which used to skip the permission check entirely */
	{
		static const char* priv[] = {"/dev/hwdev", "/dev/drvctl", "/dev/sndsrv"};
		for (int k = 0; k < 3; k++) {
			char what[64];
			sprintf(what, "%s refuses an ordinary process (write open)", priv[k]);
			int pfd = _KeOpenFile((char*)priv[k], FILE_OPEN_WRITE);
			report(what, pfd < 0, 1);
			if (pfd >= 0)
				_KeCloseFile(pfd);
		}
	}

	{
		char line[168];
		sprintf(line, "[dcltest] SUMMARY %d ok, %d failed\n", g_ok, g_bad);
		emit(line);
	}
	if (g_kmsg >= 0)
		_KeCloseFile(g_kmsg);
	return g_bad;
}
