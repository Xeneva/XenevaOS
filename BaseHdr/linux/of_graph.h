/*
 * DCL of_graph.h -- device-tree graph traversal answers "absent".
 *
 * drm_of.h includes this; the of_* functions in drm_bridge.c sit inside
 * #ifdef CONFIG_OF (drm_bridge.c:1518-1626) and never compile here, but
 * drm_of.h's own declarations and any stray callers still parse.
 */
#ifndef _LINUX_OF_GRAPH_H
#define _LINUX_OF_GRAPH_H

#include <linux/of.h>

struct device_node;

/*
 * struct of_endpoint -- mainline's, and mainline keeps it in this header
 * (include/linux/of_graph.h), which is why it is here rather than in of.h.
 *
 * It was a promise rather than a definition until drm_of.h:202 and :212
 * needed it: those two lines declare `struct of_endpoint endpoint;` by
 * value, and a local of an incomplete type is an error at the declaration --
 * unlike the pointers everywhere else, a by-value variable has to know how
 * big it is.
 *
 * All four members are carried, not only the two drm_of.h reads back
 * (`endpoint.id` and `endpoint.port`): see the note in <linux/mm.h> on why a
 * three-member struct is worse than a seven-member one. There are no
 * device-tree nodes in this kernel, so of_graph_get_remote_node() below
 * always answers 0 and drm_of_encoder_active_*() is never reached with a
 * live endpoint to fill.
 */
struct of_endpoint {
	unsigned int port;
	unsigned int id;
	const struct device_node *local_node;
	const struct device_node *remote_node;
};

static inline bool of_graph_is_present(const struct device_node *np)
{
	(void)np;
	return false;
}

static inline struct device_node *
of_graph_get_remote_node(const struct device_node *np, int port, int endpoint)
{
	(void)np; (void)port; (void)endpoint;
	return 0;
}

#endif /* _LINUX_OF_GRAPH_H */
