/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */

#include <assert.h>
#include <stdlib.h>

#include <dom/html/html_tablerow_element.h>
#include <dom/html/html_table_element.h>

#include "html/html_document.h"
#include "html/html_tablerow_element.h"
#include "html/html_collection.h"

#include "core/node.h"
#include "core/attr.h"
#include "utils/utils.h"

static const struct dom_element_protected_vtable _protect_vtable = {
	{
		DOM_NODE_PROTECT_VTABLE_HTML_TABLE_ROW_ELEMENT
	},
	DOM_HTML_TABLE_ROW_ELEMENT_PROTECT_VTABLE
};

/**
 * Create a dom_html_table_row_element table_row
 *
 * \param params  The html element creation parameters
 * \param ele     The returned element table_row
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_table_row_element_create(
		struct dom_html_element_create_params *params,
		struct dom_html_table_row_element **ele)
{
	struct dom_node_internal *node;

	*ele = malloc(sizeof(dom_html_table_row_element));
	if (*ele == NULL)
		return DOM_NO_MEM_ERR;

	/* Set up vtables */
	node = (struct dom_node_internal *) *ele;
	node->base.vtable = &_dom_html_element_vtable;
	node->vtable = &_protect_vtable;

	return _dom_html_table_row_element_initialise(params, *ele);
}

/**
 * Initialise a dom_html_table_row_element table_row
 *
 * \param params  The html element creation parameters
 * \param ele     The dom_html_table_row_element table_row
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_table_row_element_initialise(
		struct dom_html_element_create_params *params,
		struct dom_html_table_row_element *ele)
{
	return _dom_html_element_initialise(params, &ele->base);
}

/**
 * Finalise a dom_html_table_row_element table_row
 *
 * \param ele  The dom_html_table_row_element table_row
 */
void _dom_html_table_row_element_finalise(struct dom_html_table_row_element *ele)
{
	_dom_html_element_finalise(&ele->base);
}

/**
 * Destroy a dom_html_table_row_element table_row
 *
 * \param ele  The dom_html_table_row_element table_row
 */
void _dom_html_table_row_element_destroy(struct dom_html_table_row_element *ele)
{
	_dom_html_table_row_element_finalise(ele);
	free(ele);
}

/*------------------------------------------------------------------------*/
/* The protected virtual functions */

/* The virtual function used to parse attribute value, see src/core/element.c
 * for detail */
dom_exception _dom_html_table_row_element_parse_attribute(dom_element *ele,
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
void _dom_virtual_html_table_row_element_destroy(dom_node_internal *node)
{
	_dom_html_table_row_element_destroy((struct dom_html_table_row_element *) node);
}

/* The virtual copy function, see src/core/node.c for detail */
dom_exception _dom_html_table_row_element_copy(
		dom_node_internal *old, dom_node_internal **copy)
{
	dom_html_table_row_element *new_node;
	dom_exception err;

	new_node = malloc(sizeof(dom_html_table_row_element));
	if (new_node == NULL)
		return DOM_NO_MEM_ERR;

	err = dom_html_table_row_element_copy_internal(old, new_node);
	if (err != DOM_NO_ERR) {
		free(new_node);
		return err;
	}

	*copy = (dom_node_internal *) new_node;

	return DOM_NO_ERR;
}

dom_exception _dom_html_table_row_element_copy_internal(
		dom_html_table_row_element *old,
		dom_html_table_row_element *new)
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
	dom_exception dom_html_table_row_element_get_##attr(		\
			dom_html_table_row_element *element,			\
			dom_string **attr)					\
{								\
	dom_exception ret;					\
	dom_string *_memo_##attr;				\
	\
	_memo_##attr =						\
	((struct dom_html_document *)			\
	 ((struct dom_node_internal *)element)->owner)->\
	memoised[hds_##attr];				\
	\
	ret = dom_element_get_attribute(element, _memo_##attr, attr); \
	\
	return ret;						\
}
#define SIMPLE_SET(attr)						\
	dom_exception dom_html_table_row_element_set_##attr(			\
			dom_html_table_row_element *element,			\
			dom_string *attr)					\
{								\
	dom_exception ret;					\
	dom_string *_memo_##attr;				\
	\
	_memo_##attr =						\
	((struct dom_html_document *)			\
	 ((struct dom_node_internal *)element)->owner)->\
	memoised[hds_##attr];				\
	\
	ret = dom_element_set_attribute(element, _memo_##attr, attr); \
	\
	return ret;						\
}

#define SIMPLE_GET_SET(attr) SIMPLE_GET(attr) SIMPLE_SET(attr)

SIMPLE_GET_SET(align);
SIMPLE_GET_SET(bg_color);
SIMPLE_GET_SET(ch);
SIMPLE_GET_SET(ch_off);
SIMPLE_GET_SET(v_align);

/**
 * Get the index of the Row in logical order
 *
 * \param element       The dom_html_table_row_element object
 * \param index         The Status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_row_element_get_row_index(
		dom_html_table_row_element *table_row, int32_t *row_index)
{
	dom_exception exp;
	dom_node_internal *n =
		((dom_node_internal *)table_row)->parent;
	dom_node_internal *parent = n; 
	dom_html_document *doc =
		(dom_html_document *) ((dom_node_internal *) table_row)->owner;

	uint32_t count = 0;

	for (n = n->first_child; n != (dom_node_internal *)table_row;
			n = n->next) {
		if(n->type == DOM_ELEMENT_NODE &&
				dom_string_caseless_isequal(n->name,
				doc->elements[DOM_HTML_ELEMENT_TYPE_TR])) {
			count += 1;
		}
	}

	if (dom_string_caseless_isequal((parent->parent)->name,
			doc->elements[DOM_HTML_ELEMENT_TYPE_TABLE]) &&
			dom_string_caseless_isequal(parent->name,
			doc->elements[DOM_HTML_ELEMENT_TYPE_THEAD])) {
		*row_index = count;
	} else if (dom_string_caseless_isequal((parent->parent)->name,
				doc->elements[DOM_HTML_ELEMENT_TYPE_TABLE]) &&
				(dom_string_caseless_isequal(parent->name,
				doc->elements[DOM_HTML_ELEMENT_TYPE_TBODY]) ||
				dom_string_caseless_isequal(parent->name,
				doc->elements[DOM_HTML_ELEMENT_TYPE_TFOOT]))) {
		uint32_t len;
		dom_html_table_section_element *t_head;
		dom_html_collection *rows;

		n = parent->parent;
		exp = dom_html_table_element_get_t_head(
				(dom_html_table_element *)(parent->parent),
				&t_head);
		if (exp != DOM_NO_ERR) {
			return exp;
		}

		if (t_head != NULL) {
			exp = dom_html_table_section_element_get_rows(t_head, &rows);
			dom_node_unref(t_head);
			if (exp != DOM_NO_ERR) {
				return exp;
			}

			dom_html_collection_get_length(rows, &len);
			dom_html_collection_unref(rows);
			count += len;
		}

		for (n = n->first_child;n != parent && n != NULL;
			n = n->next) {
			if (dom_string_caseless_isequal(n->name,
					doc->elements[DOM_HTML_ELEMENT_TYPE_TBODY])) {
				exp = dom_html_table_section_element_get_rows(
						(dom_html_table_section_element *)n,
						&rows);
				if (exp != DOM_NO_ERR) {
					return exp;
				}

				exp = dom_html_collection_get_length(rows, &len);
				dom_html_collection_unref(rows);
				if (exp != DOM_NO_ERR) {
					return exp;
				}

				count += len;
			}
		}
		*row_index = (int32_t)count;

	} else {
		return DOM_HIERARCHY_REQUEST_ERR;
	}
	return DOM_NO_ERR;
}

/**
 * Get the index of a row within its Section
 *
 * \param element       The dom_html_table_row_element object
 * \param index         The Status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_row_element_get_section_row_index(
		dom_html_table_row_element *table_row, int32_t *section_row_index)
{
	dom_node_internal *n = ((dom_node_internal *)table_row)->parent;
	dom_html_document *doc = (dom_html_document *) ((dom_node_internal *) table_row)->owner;
	int32_t count = 0;
	for (n = n->first_child; n != (dom_node_internal *)table_row;
			n = n->next) {
		if (n->type == DOM_ELEMENT_NODE &&
				dom_string_caseless_isequal(n->name,
				doc->elements[DOM_HTML_ELEMENT_TYPE_TR])) {
			count += 1;
		}
	}
	*section_row_index = count;
	return DOM_NO_ERR;
}

/**
 * Callback for creating the Cells collection
 *
 * \param node		The dom_node_internal object
 * \param ctx		The dom_html_document object (void *)
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
static bool table_cells_callback(struct dom_node_internal *node, void *ctx)
{
	dom_html_document *doc = ctx;
	if (node->type == DOM_ELEMENT_NODE &&
			dom_string_caseless_isequal(node->name,
				doc->elements[DOM_HTML_ELEMENT_TYPE_TD])) {
		return true;
	}
	return false;
}

/**
 * Get the Cells collection
 *
 * \param element	The dom_html_table_element object
 * \param t_bodies	The Status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_row_element_get_cells(
		dom_html_table_row_element *element,
		dom_html_collection **cells)
{
	dom_html_document *doc = (dom_html_document *) ((dom_node_internal *) element)->owner;
	return _dom_html_collection_create(doc, (dom_node_internal *)element, 
			table_cells_callback, (void *)doc, cells);
}

/**
 * Insert Cell before the given Index
 *
 * \param element       The dom_html_table_row_element object
 * \param index         The Index of the Cell node to be inserted
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_row_element_insert_cell(
		dom_html_table_row_element *element,
		int32_t index, dom_html_element **cell) {
	dom_html_document *doc = (dom_html_document *) ((dom_node_internal *) element)->owner;

	dom_html_element *new_cell;

	dom_html_collection *cells;	/*< The collection of cells in input table_row_element*/
	uint32_t len; 			/*< The size of the cell collection */
	dom_exception exp;		/*< Variable for getting the exceptions*/

	struct dom_html_element_create_params params = {
		.type = DOM_HTML_ELEMENT_TYPE_TD,
		.doc = doc,
		.name = doc->elements[DOM_HTML_ELEMENT_TYPE_TD],
		.namespace = ((dom_node_internal *)element)->namespace,
		.prefix = ((dom_node_internal *)element)->prefix
	};

	exp = _dom_html_element_create(&params, &new_cell);
	if (exp != DOM_NO_ERR)
		return exp;
	
	exp = dom_html_table_row_element_get_cells(element, &cells);
	if (exp != DOM_NO_ERR) {
		dom_node_unref(new_cell);
		return exp;
	}

	exp = dom_html_collection_get_length(cells, &len);
	if (exp != DOM_NO_ERR) {
		dom_node_unref(new_cell);
		dom_html_collection_unref(cells);
		return exp;
	}
	
	if (index < -1 || index > (int32_t)len) {
		/* Check for index validity */
		dom_node_unref(new_cell);
		dom_html_collection_unref (cells);
		return DOM_INDEX_SIZE_ERR;
	} else if (index == -1 || index == (int32_t)len) {
		dom_html_collection_unref(cells);

		exp = dom_node_append_child(element, new_cell, cell);
		dom_node_unref(new_cell);

	} else {
		dom_node *node;
		dom_html_collection_item(cells, index, &node);
		dom_html_collection_unref(cells);

		exp = dom_node_insert_before(element, new_cell, node, cell);
		dom_node_unref(new_cell);
		dom_node_unref(node);
	}

	return exp;
}

/**
 * Delete Cell at given Index
 *
 * \param element       The dom_html_table_row_element object
 * \param index		The Index of the Cell node to be deleted
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_row_element_delete_cell(
		dom_html_table_row_element *element,
		int32_t index) {
	dom_node *node, *old_node;	/*< The node at the (index)th position*/
	dom_html_collection *cells; 	/*< The collection of rows in input table_row_element*/
	uint32_t len; 			/*< The size of the row collection */
	dom_exception exp;		/*< Temporary variable to store & check the exceptions*/

	exp = dom_html_table_row_element_get_cells(element, &cells);
	if (exp != DOM_NO_ERR) {
		return exp;
	}

	exp = dom_html_collection_get_length(cells, &len);
	if (exp != DOM_NO_ERR) {
		dom_html_collection_unref(cells);
		return exp;
	}

	if (index < -1 || index >= (int32_t) len || len == 0) {
		/* Check for index validity */
		dom_html_collection_unref(cells);
		return DOM_INDEX_SIZE_ERR;
	}

	if (index == -1)
		index = len - 1;

	exp = dom_html_collection_item(cells, index, &node);
	if (exp != DOM_NO_ERR) {
		dom_html_collection_unref(cells);
		return exp;
	}

	exp = dom_node_remove_child(element, node, &old_node);
	if (exp == DOM_NO_ERR)
		dom_node_unref(old_node);

	dom_node_unref(node);
	dom_html_collection_unref(cells);

	return exp;
}

