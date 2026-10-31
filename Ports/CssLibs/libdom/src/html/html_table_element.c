/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar<rsk1coder99@gmail.com>
 */
#include <assert.h>
#include <stdlib.h>

#include <dom/html/html_table_element.h>

#include "html/html_document.h"
#include "html/html_table_element.h"
#include "html/html_tablecaption_element.h"
#include "html/html_tablesection_element.h"
#include "html/html_tablerow_element.h"
#include "html/html_collection.h"

#include "core/node.h"
#include "core/attr.h"
#include "utils/utils.h"

static const struct dom_element_protected_vtable _protect_vtable = {
	{
		DOM_NODE_PROTECT_VTABLE_HTML_TABLE_ELEMENT
	},
	DOM_HTML_TABLE_ELEMENT_PROTECT_VTABLE
};

/**
 * Create a dom_html_table_element object
 *
 * \param params  The html element creation parameters
 * \param ele     The returned element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_table_element_create(
		struct dom_html_element_create_params *params,
		struct dom_html_table_element **ele)
{
	struct dom_node_internal *node;

	*ele = malloc(sizeof(dom_html_table_element));
	if (*ele == NULL)
		return DOM_NO_MEM_ERR;

	/* Set up vtables */
	node = (struct dom_node_internal *) *ele;
	node->base.vtable = &_dom_html_element_vtable;
	node->vtable = &_protect_vtable;

	return _dom_html_table_element_initialise(params, *ele);
}

/**
 * Initialise a dom_html_table_element object
 *
 * \param params  The html element creation parameters
 * \param ele     The dom_html_table_element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_table_element_initialise(
		struct dom_html_element_create_params *params,
		struct dom_html_table_element *ele)
{
	return _dom_html_element_initialise(params, &ele->base);
}

/**
 * Finalise a dom_html_table_element object
 *
 * \param ele  The dom_html_table_element object
 */
void _dom_html_table_element_finalise(struct dom_html_table_element *ele)
{
	_dom_html_element_finalise(&ele->base);
}

/**
 * Destroy a dom_html_table_element object
 *
 * \param ele  The dom_html_table_element object
 */
void _dom_html_table_element_destroy(struct dom_html_table_element *ele)
{
	_dom_html_table_element_finalise(ele);
	free(ele);
}

/*------------------------------------------------------------------------*/
/* The protected virtual functions */

/* The virtual function used to parse attribute value, see src/core/element.c
 * for detail */
dom_exception _dom_html_table_element_parse_attribute(dom_element *ele,
		dom_string *name, dom_string *value,
		dom_string **parsed)
{
	UNUSED(ele);
	UNUSED(name);

	dom_string_ref(value);
	*parsed = value;

	return DOM_NO_ERR;
}

/* The virtual destroy function, see src/core/node.c for detail */
void _dom_virtual_html_table_element_destroy(dom_node_internal *node)
{
	_dom_html_table_element_destroy((struct dom_html_table_element *) node);
}

/* The virtual copy function, see src/core/node.c for detail */
dom_exception _dom_html_table_element_copy(
		dom_node_internal *old, dom_node_internal **copy)
{
	dom_html_table_element *new_node;
	dom_exception err;

	new_node = malloc(sizeof(dom_html_table_element));
	if (new_node == NULL)
		return DOM_NO_MEM_ERR;

	err = dom_html_table_element_copy_internal(old, new_node);
	if (err != DOM_NO_ERR) {
		free(new_node);
		return err;
	}

	*copy = (dom_node_internal *) new_node;

	return DOM_NO_ERR;
}

dom_exception _dom_html_table_element_copy_internal(
		dom_html_table_element *old,
		dom_html_table_element *new)
{
	dom_exception err;

	err = dom_html_element_copy_internal(old, new);
	if (err != DOM_NO_ERR) {
		return err;
	}

	return DOM_NO_ERR;
}

/*-----------------------------------------------------------------------*/
/* API functions */

#define SIMPLE_GET(attr)						\
	dom_exception dom_html_table_element_get_##attr(		\
			dom_html_table_element *element,			\
			dom_string **attr)					\
{								\
	dom_exception ret;					\
	dom_string *_memo_##attr;				\
	\
	_memo_##attr =						\
	((struct dom_html_document *)			\
	 ((struct dom_node_internal *)element)->owner)-> \
	memoised[hds_##attr];				\
	\
	ret = dom_element_get_attribute(element, _memo_##attr, attr); \
	\
	return ret;						\
}
#define SIMPLE_SET(attr)						\
	dom_exception dom_html_table_element_set_##attr(			\
			dom_html_table_element *element,			\
			dom_string *attr)					\
{								\
	dom_exception ret;					\
	dom_string *_memo_##attr;				\
	\
	_memo_##attr =						\
	((struct dom_html_document *)			\
	 ((struct dom_node_internal *)element)->owner)-> \
	memoised[hds_##attr];				\
	\
	ret = dom_element_set_attribute(element, _memo_##attr, attr); \
	\
	return ret;						\
}

#define SIMPLE_GET_SET(attr) SIMPLE_GET(attr) SIMPLE_SET(attr)
SIMPLE_GET_SET(align);
SIMPLE_GET_SET(bg_color);
SIMPLE_GET_SET(border);
SIMPLE_GET_SET(cell_padding);
SIMPLE_GET_SET(cell_spacing);
SIMPLE_GET_SET(frame);
SIMPLE_GET_SET(rules);
SIMPLE_GET_SET(summary);
SIMPLE_GET_SET(width);

/**
 * Get the caption Attribute
 *
 * \param table	The dom_html_table_element object
 */
dom_exception dom_html_table_element_get_caption(
		dom_html_table_element *table, dom_html_table_caption_element **caption)
{
	dom_node_internal *node_tmp = ((dom_node_internal *)table);
	dom_html_document *doc = (dom_html_document *)(node_tmp->owner);

	for (node_tmp = node_tmp->first_child; node_tmp != NULL; node_tmp = node_tmp->next) {
		if((node_tmp->type == DOM_ELEMENT_NODE) &&
				dom_string_caseless_isequal(
				doc->elements[DOM_HTML_ELEMENT_TYPE_CAPTION],
				node_tmp->name)) {
			break;
		}
	}

	*caption = (dom_html_table_caption_element *)node_tmp;
	if(*caption != NULL)
		dom_node_ref(*caption);

	return DOM_NO_ERR;
}

/**
 * Set the caption Attribute
 *
 * \param table	The dom_html_table_element object
 * \param table	The dom_html_table_element object
 */
dom_exception dom_html_table_element_set_caption(
		dom_html_table_element *table, dom_html_table_caption_element *caption)
{
	dom_node_internal *check_node = ((dom_node_internal *)caption);
	dom_html_document *doc = (dom_html_document *)(((dom_node_internal *)table)->owner);
	dom_exception exp;
	dom_node *new_caption;

	if (check_node == NULL) {
		return DOM_HIERARCHY_REQUEST_ERR;
	}
	if (!dom_string_caseless_isequal(
			doc->elements[DOM_HTML_ELEMENT_TYPE_CAPTION],
			check_node->name)) {
		return DOM_HIERARCHY_REQUEST_ERR;
	}

	exp = dom_html_table_element_delete_caption(table);
	if(exp != DOM_NO_ERR)
		return exp;

	/* Create a new caption */
	return dom_node_append_child(table, caption,
			&new_caption);
}

/**
 * Get the t_head Attribute
 *
 * \param table	The dom_html_table_element object
 */
dom_exception dom_html_table_element_get_t_head(
		dom_html_table_element *table, dom_html_table_section_element **t_head)
{
	dom_node_internal *node_tmp = ((dom_node_internal *)table);
	dom_html_document *doc = (dom_html_document *)(node_tmp->owner);

	for (node_tmp = node_tmp->first_child; node_tmp != NULL; node_tmp = node_tmp->next) {
		if((node_tmp->type == DOM_ELEMENT_NODE) &&
				dom_string_caseless_isequal(
				doc->elements[DOM_HTML_ELEMENT_TYPE_THEAD],
				node_tmp->name)) {
			break;
		}
	}

	*t_head = (dom_html_table_section_element *)node_tmp;
	if (*t_head != NULL)
		dom_node_ref(*t_head);
	return DOM_NO_ERR;
}

/**
 * Set the t_head Attribute
 *
 * \param table	The dom_html_table_element object
 * \param table	The dom_html_table_element object
 */
dom_exception dom_html_table_element_set_t_head(
		dom_html_table_element *table, dom_html_table_section_element *t_head)
{
	dom_node_internal *check_node = ((dom_node_internal *)t_head);
	dom_html_document *doc = (dom_html_document *)(((dom_node_internal *)table)->owner);
	dom_exception exp;
	dom_node *new_t_head;

	if (check_node == NULL) {
		return DOM_HIERARCHY_REQUEST_ERR;
	}
	if (!dom_string_caseless_isequal(
			doc->elements[DOM_HTML_ELEMENT_TYPE_CAPTION],
			check_node->name)) {
		return DOM_HIERARCHY_REQUEST_ERR;
	}

	exp = dom_html_table_element_delete_t_head(table);
	if(exp != DOM_NO_ERR)
		return exp;

	return dom_node_append_child(table,
			t_head, &new_t_head);

}

/**
 * Get the t_foot Attribute
 *
 * \param table	The dom_html_table_element object
 */
dom_exception dom_html_table_element_get_t_foot(
		dom_html_table_element *table, dom_html_table_section_element **t_foot)
{
	dom_node_internal *node_tmp = ((dom_node_internal *)table);
	dom_html_document *doc = (dom_html_document *)(node_tmp->owner);

	for (node_tmp = node_tmp->first_child; node_tmp != NULL; node_tmp = node_tmp->next) {
		if ((node_tmp->type == DOM_ELEMENT_NODE) &&
				dom_string_caseless_isequal(
				doc->elements[DOM_HTML_ELEMENT_TYPE_TFOOT],
				node_tmp->name)) {
			break;
		}
	}

	*t_foot = (dom_html_table_section_element *)node_tmp;
	if (*t_foot != NULL)
		dom_node_ref(*t_foot);

	return DOM_NO_ERR;
}

/**
 * Set the t_foot Attribute
 *
 * \param table	The dom_html_table_element object
 */
dom_exception dom_html_table_element_set_t_foot(
		dom_html_table_element *table, dom_html_table_section_element *t_foot)
{
	dom_node_internal *check_node = ((dom_node_internal *)t_foot); /*< temporary node to check for raised exceptions */
	dom_html_document *doc = (dom_html_document *)(((dom_node_internal *)table)->owner);
	dom_exception exp;
	dom_node *new_t_foot;

	if(check_node == NULL) {
		return DOM_HIERARCHY_REQUEST_ERR;
	}

	if(!dom_string_caseless_isequal(
			doc->elements[DOM_HTML_ELEMENT_TYPE_TFOOT],
			check_node->name)) {
		return DOM_HIERARCHY_REQUEST_ERR;
	}

	exp = dom_html_table_element_delete_t_foot(table);
	if(exp != DOM_NO_ERR)
		return exp;

	return dom_node_append_child(table, t_foot,
				&new_t_foot);

}

/**
 * Callback for creating the rows collection
 *
 * \param node		The dom_html_table_element object
 * \param ctx		The dom_html_document object (void *)
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
static bool table_rows_callback(struct dom_node_internal *node, void *ctx)
{
	dom_html_document *doc = ctx;
	if(node->type == DOM_ELEMENT_NODE &&
			dom_string_caseless_isequal(node->name,
				doc->elements[DOM_HTML_ELEMENT_TYPE_TR])) {
		return true;
	}
	return false;
}

/**
 * Get the rows collection
 *
 * \param element       The dom_html_table_element object
 * \param rows          The Status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_element_get_rows(
		dom_html_table_element *element,
		dom_html_collection **rows)
{
	dom_html_document *doc = (dom_html_document *) ((dom_node_internal *) element)->owner;
	return _dom_html_collection_create(doc, (dom_node_internal *)element, 
			table_rows_callback, (void *)doc, rows);
}

/**
 * Callback for creating the tbodies collection
 *
 * \param node		The dom_html_table_element object
 * \param ctx		The dom_html_document object (void *)
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
static bool table_t_bodies_callback(struct dom_node_internal *node, void *ctx)
{
	dom_html_document *doc = ctx;
	if(node->type == DOM_ELEMENT_NODE && 
			dom_string_caseless_isequal(node->name,
				doc->elements[DOM_HTML_ELEMENT_TYPE_TBODY])) {
		return true;
	}
	return false;
}

/**
 * Get the tBodies collection
 *
 * \param element	The dom_html_table_element object
 * \param t_bodies	The Status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */

dom_exception dom_html_table_element_get_t_bodies(
		dom_html_table_element *element,
		dom_html_collection **t_bodies)
{
	dom_html_document *doc = (dom_html_document *) ((dom_node_internal *) element)->owner;
	return _dom_html_collection_create(doc, (dom_node_internal *)element, 
			table_t_bodies_callback, (void *)doc, t_bodies);
}

/**
 * Get or Create the table caption
 *
 * \param element	The dom_html_table_element object
 * \param caption	The Status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_element_create_caption(
		dom_html_table_element *element,
		dom_html_element **caption)
{
	dom_exception exp;
	if((exp = dom_html_table_element_get_caption(element, 
					(dom_html_table_caption_element **)caption)) != DOM_NO_ERR) {
		dom_node_unref(*caption);
		return exp;
	}
	if((*caption) == NULL) {
		dom_node *new_caption;
		dom_html_document *doc = (dom_html_document *)
				((dom_node_internal *) element)->owner;

		struct dom_html_element_create_params params = {
			.type = DOM_HTML_ELEMENT_TYPE_CAPTION,
			.doc = doc,
			.name = doc->elements[DOM_HTML_ELEMENT_TYPE_CAPTION],
			.namespace = ((dom_node_internal *)element)->namespace,
			.prefix = ((dom_node_internal *)element)->prefix
		};

		exp = _dom_html_table_caption_element_create(&params,
				(dom_html_table_caption_element **)caption);
		if(exp != DOM_NO_ERR) {
			dom_node_unref(*caption);
			return exp;
		}

		exp = dom_node_append_child(element, *caption,
				&new_caption);
		dom_node_unref(*caption);
		if(exp == DOM_NO_ERR)
			*caption = (dom_html_element *)new_caption;
	}
	return exp;
}

/**
 * Delete the table caption, if one exists
 *
 * \param element	The dom_html_table_element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_element_delete_caption(
		dom_html_table_element *element)
{
	dom_html_table_caption_element *caption;
	dom_node *old_caption;
	dom_exception err;

	err = dom_html_table_element_get_caption(element, &caption);
	if (err != DOM_NO_ERR || caption == NULL)
		return err;

	err = dom_node_remove_child(element, caption, &old_caption);
	if (err == DOM_NO_ERR) {
		dom_node_unref(old_caption);
	}

	dom_node_unref(caption);

	return err;
}

/**
 * Get or Create the table Foot
 *
 * \param element	The dom_html_table_element object
 * \param t_foot	The Status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_element_create_t_foot(
		dom_html_table_element *element,
		dom_html_element **t_foot)
{
	dom_exception exp;
	exp = dom_html_table_element_get_t_foot(element,
			(dom_html_table_section_element **)t_foot);
	if (exp !=DOM_NO_ERR)
		return exp;

	if ((*t_foot) == NULL) {
		dom_node *new_t_foot;
		dom_html_document *doc = (dom_html_document *)
				((dom_node_internal *) element)->owner;

		struct dom_html_element_create_params params = {
			.type = DOM_HTML_ELEMENT_TYPE_TFOOT,
			.doc = doc,
			.name = doc->elements[DOM_HTML_ELEMENT_TYPE_TFOOT],
			.namespace = ((dom_node_internal *)element)->namespace,
			.prefix = ((dom_node_internal *)element)->prefix
		};

		exp = _dom_html_table_section_element_create(&params,
				(dom_html_table_section_element **)t_foot);
		if (exp != DOM_NO_ERR) {
			dom_node_unref(*t_foot);
			return exp;
		}

		exp = dom_node_append_child(element, *t_foot,
				&new_t_foot);
		dom_node_unref(*t_foot);
		if (exp == DOM_NO_ERR)
			*t_foot = (dom_html_element *)new_t_foot;

		return exp;

	}
	return DOM_NO_ERR;
}

/**
 * Delete the table Foot, if one exists
 *
 * \param element	The dom_html_table_element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_element_delete_t_foot(
		dom_html_table_element *element)
{
	dom_html_table_section_element *t_foot;
	dom_node *old_t_foot;
	dom_exception err;

	err = dom_html_table_element_get_t_foot(element, &t_foot);
	if (err != DOM_NO_ERR || t_foot == NULL)
		return err;

	err = dom_node_remove_child(element, t_foot, &old_t_foot);
	if (err == DOM_NO_ERR) {
		dom_node_unref(old_t_foot);
	}

	dom_node_unref(t_foot);

	return err;
}

/**
 * Get or Create the table Head
 *
 * \param element	The dom_html_table_element object
 * \param t_head	The Status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_element_create_t_head(
		dom_html_table_element *element,
		dom_html_element **t_head)
{
	dom_exception exp;
	exp = dom_html_table_element_get_t_head(element,
			(dom_html_table_section_element **)t_head);
	if(exp != DOM_NO_ERR) {
		dom_node_unref(*t_head);
		return exp;
	}
	if((*t_head) == NULL) {
		dom_exception exp;
		dom_node *new_t_head;
		dom_html_document *doc = (dom_html_document *)
				((dom_node_internal *) element)->owner;

		struct dom_html_element_create_params params = {
			.type = DOM_HTML_ELEMENT_TYPE_THEAD,
			.doc = doc,
			.name = doc->elements[DOM_HTML_ELEMENT_TYPE_THEAD],
			.namespace = ((dom_node_internal *)element)->namespace,
			.prefix = ((dom_node_internal *)element)->prefix
		};

		exp = _dom_html_table_section_element_create(&params,
				(dom_html_table_section_element **)t_head);
		if(exp != DOM_NO_ERR) {
			dom_node_unref(*t_head);
			return exp;
		}

		exp = dom_node_append_child(element,
				*t_head, &new_t_head);
		if(exp == DOM_NO_ERR) {
			dom_node_unref(*t_head);
			*t_head = (dom_html_element *)new_t_head;
		}
		return exp;
	}
	return DOM_NO_ERR;
}

/**
 * Delete the table Head, if one exists
 *
 * \param element	The dom_html_table_element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_element_delete_t_head(
		dom_html_table_element *element)
{
	dom_html_table_section_element *t_head;
	dom_node *old_t_head;
	dom_exception err;

	err = dom_html_table_element_get_t_head(element, &t_head);
	if (err != DOM_NO_ERR || t_head == NULL)
		return err;

	err = dom_node_remove_child(element, t_head, &old_t_head);
	if (err == DOM_NO_ERR) {
		dom_node_unref(old_t_head);
	}

	dom_node_unref(t_head);

	return err;
}

/**
 * Get or Create the table Body
 *
 * \param element	The dom_html_table_element object
 * \param t_head	The Status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
static dom_exception dom_html_table_element_create_t_body(
		dom_html_table_element *element,
		dom_html_table_section_element **t_body)
{
	dom_html_collection *t_bodies;
	uint32_t len;
	dom_exception exp;
	exp = dom_html_table_element_get_t_bodies(element,
			&t_bodies);
	if(exp != DOM_NO_ERR) {
		return exp;
	}
	exp = dom_html_collection_get_length(t_bodies,
			&len);
	if(exp != DOM_NO_ERR) {
		dom_html_collection_unref(t_bodies);
		return exp;
	}
	if(len == 0) {
		dom_node *new_t_body;
		dom_html_document *doc = (dom_html_document *)
				((dom_node_internal *) element)->owner;

		struct dom_html_element_create_params params = {
			.type = DOM_HTML_ELEMENT_TYPE_TBODY,
			.doc = doc,
			.name = doc->elements[DOM_HTML_ELEMENT_TYPE_TBODY],
			.namespace = ((dom_node_internal *)element)->namespace,
			.prefix = ((dom_node_internal *)element)->prefix
		};

		exp = _dom_html_table_section_element_create(&params, t_body);
		if(exp != DOM_NO_ERR) {
			dom_node_unref(*t_body);
			dom_html_collection_unref(t_bodies);
			return exp;
		}

		exp = dom_node_append_child(element, *t_body,
				&new_t_body);
		if(exp == DOM_NO_ERR) {
			dom_node_unref(*t_body);
			*t_body = (dom_html_table_section_element *)new_t_body;
		}
	} else {
		exp = dom_html_collection_item(t_bodies,
				0, (dom_node **)t_body);
	}
	dom_html_collection_unref(t_bodies);
	return exp;
}

/**
 * Insert a new Row into the table
 *
 * \param element	The dom_html_table_element object
 * \param index		The Index to insert the Row
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_element_insert_row(
		dom_html_table_element *element,
		int32_t index,
		dom_html_element **row_out)
{
	dom_exception exp;
	dom_html_collection* rows;
	uint32_t len;
	dom_html_document *doc = (dom_html_document *)
		((dom_node_internal *) element)->owner;

	exp = dom_html_table_element_get_rows(element, &rows);
	if(exp != DOM_NO_ERR) {
		return exp;
	}
	exp = dom_html_collection_get_length(rows, &len);
	dom_html_collection_unref(rows);
	if(exp != DOM_NO_ERR) {
		return exp;
	}

	if(index > (int32_t)len || index < -1) {
		exp = DOM_INDEX_SIZE_ERR;
	} else if(len == 0) {
		dom_html_table_section_element *new_body;
		dom_html_table_row_element *row;
		dom_node *new_row;

		struct dom_html_element_create_params params = {
			.type = DOM_HTML_ELEMENT_TYPE_TR,
			.doc = doc,
			.name = doc->elements[DOM_HTML_ELEMENT_TYPE_TR],
			.namespace = ((dom_node_internal *)element)->namespace,
			.prefix = ((dom_node_internal *)element)->prefix
		};

		exp = _dom_html_table_row_element_create(&params, &row);
		if(exp != DOM_NO_ERR) {
			return exp;
		}

		exp = dom_html_table_element_create_t_body(element, &new_body);
		if(exp != DOM_NO_ERR) {
			dom_node_unref(row);
			return exp;
		}

		exp = dom_node_append_child(new_body, (dom_html_element *) row,
				&new_row);
		dom_node_unref(new_body);
		dom_node_unref(row);
		if(exp == DOM_NO_ERR) {
			*row_out = (dom_html_element *)new_row;
		}
	} else {
		uint32_t window_len = 0, section_len;
		dom_html_table_section_element *t_head;
		dom_html_table_section_element *t_foot;
		dom_html_element *row;
		dom_node_internal *n;

		if(index ==-1) {
			index = (int32_t)len;
		}

		exp = dom_html_table_element_get_t_head(element, &t_head);
		if (exp != DOM_NO_ERR) {
			return exp;
		}

		exp = dom_html_table_section_element_get_rows(t_head, &rows);
		if(exp != DOM_NO_ERR) {
			dom_node_unref(t_head);
			return exp;
		}

		exp = dom_html_collection_get_length(rows, &section_len);
		dom_html_collection_unref(rows);
		if(exp != DOM_NO_ERR) {
			dom_node_unref(t_head);
			return exp;
		}

		if(window_len + section_len > (uint32_t)index ||
				window_len + section_len == len) {
			exp = dom_html_table_section_element_insert_row(t_head,
					index-window_len, &row);
			dom_node_unref(t_head);
			if (exp == DOM_NO_ERR) {
				*row_out = row;
			}
			return exp;
		}
		dom_node_unref(t_head);

		window_len += section_len;

		n = (dom_node_internal *)element;

		for (n = n->first_child; n != NULL; n = n->next) {
			if((n->type == DOM_ELEMENT_NODE) &&
					dom_string_caseless_isequal(
					doc->elements[DOM_HTML_ELEMENT_TYPE_TBODY],
					n->name)) {

				exp = dom_html_table_section_element_get_rows(
						(dom_html_table_section_element *)n,
						&rows);
				if (exp != DOM_NO_ERR) {
					return exp;
				}
				exp = dom_html_collection_get_length(rows, &section_len);
				dom_html_collection_unref(rows);
				if (exp != DOM_NO_ERR) {
					return exp;
				}

				if(window_len + section_len > (uint32_t)index ||
						window_len + section_len == len) {
					exp = dom_html_table_section_element_insert_row(
							(dom_html_table_section_element *)n,
							index-window_len, &row);
					if (exp == DOM_NO_ERR) {
						*row_out = row;
					}
					return exp;
				}

				window_len += section_len;
			}
		}
		exp = dom_html_table_element_get_t_foot(element, &t_foot);
		if(exp != DOM_NO_ERR) {
			return exp;
		}

		exp = dom_html_table_section_element_get_rows(t_foot, &rows);
		if(exp != DOM_NO_ERR) {
			dom_node_unref(t_foot);
			return exp;
		}

		exp = dom_html_collection_get_length(rows, &section_len);
		dom_html_collection_unref(rows);
		if(exp != DOM_NO_ERR) {
			dom_node_unref(t_foot);
			return exp;
		}

		if(window_len + section_len > (uint32_t)index ||
				window_len +section_len == len) {
			exp = dom_html_table_section_element_insert_row(t_foot,
					index-window_len, &row);
			dom_node_unref(t_foot);
			if (exp == DOM_NO_ERR) {
				*row_out = row;
			}
			return exp;
		}
		dom_node_unref(t_foot);
		exp = DOM_INDEX_SIZE_ERR;
	}

	return exp;
}

/**
 * Delete the table Head, if one exists
 *
 * \param element	The dom_html_table_element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_element_delete_row(
		dom_html_table_element *element,
		int32_t index)
{
	dom_exception exp;
	dom_html_collection* rows;
	uint32_t len;
	dom_html_document *doc = (dom_html_document *) 
		((dom_node_internal *) element)->owner;

	exp = dom_html_table_element_get_rows(element, &rows);
	if(exp != DOM_NO_ERR) {
		return exp;
	}
	exp = dom_html_collection_get_length(rows, &len);
	dom_html_collection_unref(rows);
	if(exp != DOM_NO_ERR) {
		return exp;
	}

	if(index >= (int32_t)len || index < -1 || len ==0) {
		return DOM_INDEX_SIZE_ERR;
	} else {
		uint32_t window_len = 0, section_len;
		dom_html_table_section_element *t_head;
		dom_html_table_section_element *t_foot;
		dom_node_internal *n;

		if(index ==-1) {
			index = (int32_t)len-1;
		}

		exp = dom_html_table_element_get_t_head(element, &t_head);
		if(exp != DOM_NO_ERR)
			return exp;

		exp = dom_html_table_section_element_get_rows(t_head, &rows);
		if (exp != DOM_NO_ERR) {
			dom_node_unref(t_head);
			return DOM_NO_ERR;
		}

		exp = dom_html_collection_get_length(rows, &section_len);
		dom_html_collection_unref(rows);
		if(exp != DOM_NO_ERR) {
			dom_node_unref(t_head);
			return exp;
		}

		if(window_len + section_len > (uint32_t)index) {
			exp = dom_html_table_section_element_delete_row(t_head,
					index-window_len);
			dom_node_unref(t_head);
			return exp;
		}
		dom_node_unref(t_head);

		window_len += section_len;
		n = (dom_node_internal *)element;

		for (n = n->first_child; n != NULL; n = n->next) {
			if((n->type == DOM_ELEMENT_NODE) &&
					dom_string_caseless_isequal(
					doc->elements[DOM_HTML_ELEMENT_TYPE_TBODY],
					n->name)) {
				exp = dom_html_table_section_element_get_rows(
						(dom_html_table_section_element *)n,
						&rows);
				if(exp != DOM_NO_ERR) {
					return exp;
				}

				dom_html_collection_get_length(rows, &section_len);
				dom_html_collection_unref(rows);
				if(exp != DOM_NO_ERR)
					return exp;

				if(window_len + section_len > (uint32_t)index) {
					return dom_html_table_section_element_delete_row(
							(dom_html_table_section_element *)n,
							index-window_len);
				}
				window_len += section_len;
			}
		}
		exp = dom_html_table_element_get_t_foot(element, &t_foot);
		if(exp != DOM_NO_ERR)
			return exp;

		exp = dom_html_table_section_element_get_rows(t_foot, &rows);
		if(exp != DOM_NO_ERR) {
			dom_node_unref(t_foot);
			return exp;
		}
		exp = dom_html_collection_get_length(rows, &section_len);
		dom_html_collection_unref(rows);
		if (exp != DOM_NO_ERR) {
			dom_node_unref(t_foot);
			return exp;
		}

		if(window_len + section_len > (uint32_t)index) {
			exp = dom_html_table_section_element_delete_row(t_foot,
					index-window_len);
			dom_node_unref(t_foot);
			return exp;
		}
		return DOM_INDEX_SIZE_ERR;
	}

}

