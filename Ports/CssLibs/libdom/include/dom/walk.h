/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2021 Michael Drake <tlsa@netsurf-browser.org>
 */

/** \file
 * This is an API for walking a loaded DOM.
 */

#ifndef dom_walk_h_
#define dom_walk_h_

enum dom_walk_stage {
	DOM_WALK_STAGE_ENTER,
	DOM_WALK_STAGE_LEAVE,
};

enum dom_walk_enable {
	DOM_WALK_ENABLE_ENTER = (1 << DOM_WALK_STAGE_ENTER),
	DOM_WALK_ENABLE_LEAVE = (1 << DOM_WALK_STAGE_LEAVE),
	DOM_WALK_ENABLE_ALL   = DOM_WALK_ENABLE_ENTER | DOM_WALK_ENABLE_LEAVE,
};

enum dom_walk_cmd {
	DOM_WALK_CMD_CONTINUE, /**< Continue the tree walk. */
	DOM_WALK_CMD_ABORT,    /**< Early termination of the tree walk. */
	DOM_WALK_CMD_SKIP,     /**< Skip children (only for \ref DOM_WALK_ENABLE_ENTER). */
};

/**
 * DOM walking callback.
 *
 * Client callback for DOM walk.
 *
 * \param[in] stage  Whether the \ref node is being entered or left.
 * \param[in] node   The node being walked.  Client must take ref itself.
 * \param[in] type   The node type.
 * \param[in] pw     Client private data.
 * \return Tree walking client command.
 */
typedef enum dom_walk_cmd (*dom_walk_cb)(
		enum dom_walk_stage stage,
		dom_node_type type,
		dom_node *node,
		void *pw);


/**
 * Walk a DOM subtree.
 *
 * \param[in] mask  Mask of stages to enable callback for.
 * \param[in] cb    The client callback function.
 * \param[in] root  Node to start walk from.
 * \param[in] pw    The client's private data.
 * \return false for early termination of walk, true otherwise.
 */
dom_exception libdom_treewalk(
		enum dom_walk_enable mask,
		dom_walk_cb cb,
		dom_node *root,
		void *pw);

#endif
