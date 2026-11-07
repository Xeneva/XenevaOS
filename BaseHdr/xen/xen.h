/*
 * DCL xen/xen.h -- xen_pv_domain() is false; drm_cache.c uses it only to
 * pick a cache-flush flavour and Xeneva never runs under Xen.
 */
#ifndef _DCL_XEN_XEN_H
#define _DCL_XEN_XEN_H

static inline bool xen_pv_domain(void)
{
	return false;
}

#endif /* _DCL_XEN_XEN_H */
