/*
 * DCL dynamic_debug.h -- dynamic_pr_debug() and friends are dropped.
 *
 * drm_print.c/drm_crtc_helper.c enable the DYNAMIC_DEBUG label; DCL keeps
 * dynamic_dev_dbg/pr_debug as statements that discard their arguments, same
 * policy as mainline's CONFIG_DYNAMIC_DEBUG=n + !DEBUG builds.
 */
#ifndef _LINUX_DYNAMIC_DEBUG_H
#define _LINUX_DYNAMIC_DEBUG_H

#include <linux/printk.h>

#define dynamic_pr_debug(desc, fmt, ...)	do { (void)(desc); } while (0)
#define dynamic_dev_dbg(dev, fmt, ...)		do { (void)(dev); } while (0)
#define dynamic_dev_warn(dev, fmt, ...)		do { (void)(dev); } while (0)
#define DEFINE_DYNAMIC_DEBUG_METADATA(id, fmt)
#define DEFINE_DYNAMIC_DEBUG_METADATA_CLS(cls, id, fmt)
#define DYNAMIC_DEBUG_BRANCH(descriptor)	0

/*
 * struct _ddebug_class_map and DECLARE_DYNDBG_CLASSMAP -- the one piece of
 * the dyndbg surface that has to declare a real object instead of discarding
 * its arguments.
 *
 * drm_crtc_helper.c:53 spells the macro *unguarded*, and only drm_print.c's
 * copy sits inside `#if defined(CONFIG_DRM_USE_DYNAMIC_DEBUG)` (drm_print.c:
 * 58-72). So the expansion has to put a name to something, or that line
 * decays to a list of bare identifiers and clang reports "type specifier
 * missing" at the first one.
 *
 * `static` is not an oversight: when CONFIG_DRM_USE_DYNAMIC_DEBUG *is*
 * defined, drm_print.c:60 expands this same macro, and two extern definitions
 * of one symbol would not link. It also keeps drm_crtc_helper.c's copy from
 * being reported as unused in a build where the map is written but never read.
 *
 * The variadic tail -- the ten "DRM_UT_*" strings -- is dropped. They exist
 * in mainline so the dyndbg control file can name a class as text, and there
 * is no control file here; the class *values* are what the drm core
 * compares against, and they come from enum drm_debug_category, not from
 * these strings.
 */
struct _ddebug_class_map {
	const char* modname;
	int class_id;
	const char* name;
};

#define DD_CLASS_TYPE_DISJOINT_BITS	0

#define DECLARE_DYNDBG_CLASSMAP(mapname, type, _idx, ...)		\
	static const struct _ddebug_class_map mapname = {		\
		.modname	= "drm",				\
		.class_id	= (type),				\
		.name		= #mapname,				\
	}

#endif /* _LINUX_DYNAMIC_DEBUG_H */
