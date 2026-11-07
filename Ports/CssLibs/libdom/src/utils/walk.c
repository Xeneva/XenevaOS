/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2021 Michael Drake <tlsa@netsurf-browser.org>
 */

/** \file
 * This is an API for walking a loaded DOM.
 */

#include <dom/dom.h>
#include <dom/walk.h>

/**
 * Wrapper for calling client callback.
 *
 * \param[in]  mask     Mask of stages to enable callback for.
 * \param[in]  stage    Whether the \ref node is being entered or left.
 * \param[in]  node     The node being walked.
 * \param[in]  cb       The client callback function.
 * \param[in]  pw       The client's private data.
 * \param[out] cmd_out  Walk instruction from client.
 * \return false for early termination of walk, true otherwise.
 */
static inline dom_exception dom_walk__cb(
		enum dom_walk_enable mask,
		enum dom_walk_stage stage,
		dom_node *node,
		dom_walk_cb cb,
		void *pw,
		enum dom_walk_cmd *cmd_out)
{
	if ((1 << stage) & mask) {
		dom_node_type type;
		dom_exception exc;

		exc = dom_node_get_node_type(node, &type);
		if (exc != DOM_NO_ERR) {
			return exc;
		}

		*cmd_out = cb(stage, type, node, pw);
	}

	return DOM_NO_ERR;
}

/* exported interface documented in include/dom/walk.h */
dom_exception libdom_treewalk(
		enum dom_walk_enable mask,
		dom_walk_cb cb,
		dom_node *root,
		void *pw)
{
	dom_node *node;
	dom_exception exc;
	enum dom_walk_cmd cmd = DOM_WALK_CMD_CONTINUE;

	node = dom_node_ref(root);

	while (cmd != DOM_WALK_CMD_ABORT) {
		dom_node *next = NULL;

		if (cmd != DOM_WALK_CMD_SKIP) {
			exc = dom_node_get_first_child(node, &next);
			if (exc != DOM_NO_ERR) {
				dom_node_unref(node);
				break;
			}
		}

		if (next != NULL) {
			dom_node_unref(node);
			node = next;
		} else {
			/* No children; siblings & ancestor's siblings */
			while (node != root) {
				exc = dom_walk__cb(mask, DOM_WALK_STAGE_LEAVE,
						node, cb, pw, &cmd);
				if (exc != DOM_NO_ERR ||
				    cmd == DOM_WALK_CMD_ABORT) {
					dom_node_unref(node);
					return exc;
				}

				exc = dom_node_get_next_sibling(node, &next);
				if (exc != DOM_NO_ERR) {
					dom_node_unref(node);
					node = NULL;
					break;
				}

				if (next != NULL) {
					/* Found next sibling. */
					break;
				}

				exc = dom_node_get_parent_node(node, &next);
				if (exc != DOM_NO_ERR) {
					dom_node_unref(node);
					return exc;
				}

				dom_node_unref(node);
				node = next;
			}

			if (node == root) {
				break;
			}

			dom_node_unref(node);
			node = next;
		}

		assert(node != NULL);
		assert(node != root);

		exc = dom_walk__cb(mask, DOM_WALK_STAGE_ENTER, node,
				cb, pw, &cmd);
		if (exc != DOM_NO_ERR) {
			return exc;
		}
	}

	dom_node_unref(node);

	return DOM_NO_ERR;
}
