#ifndef __LINUX_FWNODE_H__
#define __LINUX_FWNODE_H__

/*
 * DCL <linux/fwnode.h> -- the one fwnode entry point the vendored drm core
 * links against, fwnode_handle_put().
 *
 * mainline v7.2 defines struct fwnode_handle in this header and spells
 * fwnode_handle_put() as a static inline in <linux/property.h> (a
 * fwnode_call_void_op over fwnode->ops->put).  DCL keeps the struct in
 * property.h -- that is where its only member reader, drm_connector.c:3492
 * (`connector->fwnode->secondary`), picked it up -- and has no body for
 * struct fwnode_operations at all, so an inline cannot reach ->ops->put.
 * The declaration is therefore an ordinary extern whose no-op body sits in
 * DCL/linux_device_shim.c, and that body is exactly what this kernel can
 * observe: dev_fwnode() (property.h) answers NULL for every device and
 * nothing creates a node, so the one caller, drm_connector_cleanup()
 * (drm_connector.c:798), only ever passes NULL.
 *
 * property.h is included rather than re-declaring the tag so that a later
 * consumer that names <linux/fwnode.h> the way mainline sources do finds a
 * complete struct; property.h does not include this header, so there is no
 * cycle.  The header exists because device.h includes it for the callers
 * that cannot name it themselves -- the same routing device.h's own comment
 * describes.
 */

#include <linux/property.h>	/* struct fwnode_handle */

void fwnode_handle_put(struct fwnode_handle* fwnode);

#endif /* __LINUX_FWNODE_H__ */
