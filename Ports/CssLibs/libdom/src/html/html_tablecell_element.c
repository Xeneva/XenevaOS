/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */

#include <assert.h>
#include <stdlib.h>

#include <dom/html/html_tablecell_element.h>

#include "html/html_document.h"
#include "html/html_tablecell_element.h"

#include "core/node.h"
#include "core/attr.h"
#include "utils/utils.h"

static const struct dom_element_protected_vtable _protect_vtable = {
	{
		DOM_NODE_PROTECT_VTABLE_HTML_TABLE_CELL_ELEMENT
	},
	DOM_HTML_TABLE_CELL_ELEMENT_PROTECT_VTABLE
};

/**
 * Create a dom_html_table_cell_element table_cell
 *
 * \param params  The html element creation parameters
 * \param ele     The returned element table_cell
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_table_cell_element_create(
		struct dom_html_element_create_params *params,
		struct dom_html_table_cell_element **ele)
{
	struct dom_node_internal *node;

	*ele = malloc(sizeof(dom_html_table_cell_element));
	if (*ele == NULL)
		return DOM_NO_MEM_ERR;

	/* Set up vtables */
	node = (struct dom_node_internal *) *ele;
	node->base.vtable = &_dom_html_element_vtable;
	node->vtable = &_protect_vtable;

	return _dom_html_table_cell_element_initialise(params, *ele);
}

/**
 * Initialise a dom_html_table_cell_element table_cell
 *
 * \param params  The html element creation parameters
 * \param ele     The dom_html_table_cell_element table_cell
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_table_cell_element_initialise(
		struct dom_html_element_create_params *params,
		struct dom_html_table_cell_element *ele)
{
	return _dom_html_element_initialise(params, &ele->base);
}

/**
 * Finalise a dom_html_table_cell_element table_cell
 *
 * \param ele  The dom_html_table_cell_element table_cell
 */
void _dom_html_table_cell_element_finalise(struct dom_html_table_cell_element *ele)
{
	_dom_html_element_finalise(&ele->base);
}

/**
 * Destroy a dom_html_table_cell_element table_cell
 *
 * \param ele  The dom_html_table_cell_element table_cell
 */
void _dom_html_table_cell_element_destroy(struct dom_html_table_cell_element *ele)
{
	_dom_html_table_cell_element_finalise(ele);
	free(ele);
}

/*------------------------------------------------------------------------*/
/* The protected virtual functions */

/* The virtual function used to parse attribute value, see src/core/element.c
 * for detail */
dom_exception _dom_html_table_cell_element_parse_attribute(dom_element *ele,
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
void _dom_virtual_html_table_cell_element_destroy(dom_node_internal *node)
{
	_dom_html_table_cell_element_destroy((struct dom_html_table_cell_element *) node);
}

/* The virtual copy function, see src/core/node.c for detail */
dom_exception _dom_html_table_cell_element_copy(
		dom_node_internal *old, dom_node_internal **copy)
{
	dom_html_table_cell_element *new_node;
	dom_exception err;

	new_node = malloc(sizeof(dom_html_table_cell_element));
	if (new_node == NULL)
		return DOM_NO_MEM_ERR;

	err = dom_html_table_cell_element_copy_internal(old, new_node);
	if (err != DOM_NO_ERR) {
		free(new_node);
		return err;
	}

	*copy = (dom_node_internal *) new_node;

	return DOM_NO_ERR;
}

dom_exception _dom_html_table_cell_element_copy_internal(
		dom_html_table_cell_element *old,
		dom_html_table_cell_element *new)
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
	dom_exception dom_html_table_cell_element_get_##attr(		\
			dom_html_table_cell_element *element,			\
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
	dom_exception dom_html_table_cell_element_set_##attr(			\
			dom_html_table_cell_element *element,			\
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

SIMPLE_GET_SET(abbr);
SIMPLE_GET_SET(align);
SIMPLE_GET_SET(axis);
SIMPLE_GET_SET(bg_color);
SIMPLE_GET_SET(ch);
SIMPLE_GET_SET(ch_off);
SIMPLE_GET_SET(headers);
SIMPLE_GET_SET(height);
SIMPLE_GET_SET(scope);
SIMPLE_GET_SET(v_align);
SIMPLE_GET_SET(width);

/**
 * Get the cell_index property
 *
 * \param table_cell		The dom_html_table_cell_element object
 * \param cell_index		The status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_cell_element_get_cell_index(
		dom_html_table_cell_element *table_cell, dom_long *cell_index)
{
	dom_node_internal *n = ((dom_node_internal *)table_cell)->parent;
	dom_html_document *doc = (dom_html_document *)(n->owner);
	int32_t cnt = 0;
	dom_node_internal *root;
	while(n != NULL) {
		if(dom_string_caseless_isequal(
				doc->elements[DOM_HTML_ELEMENT_TYPE_TR],
				n->name)) {
			break;
		}
		n = n->parent;
	}
	root = n;
	while(n != NULL) {
		if(n == (dom_node_internal *)table_cell) {
			break;
		} else if((n->type == DOM_ELEMENT_NODE) &&
				(dom_string_caseless_isequal(
				doc->elements[DOM_HTML_ELEMENT_TYPE_TD],
				n->name) ||
				dom_string_caseless_isequal(
				doc->elements[DOM_HTML_ELEMENT_TYPE_TH],
				n->name))) {
			cnt += 1;
		}
		if(n->first_child != NULL) {
			n = n->first_child;
		} else if(n->next != NULL) {
			n = n->next;
		} else {
			/* No children and siblings */
			struct dom_node_internal *parent = n->parent;
			while (n == parent->last_child &&
					n != root) {
				n = parent;
				parent = parent->parent;
			}

			if(n == root) {
				n = NULL;
			} else {
				n = n->next;
			}
		}
	}
	*cell_index = cnt;
	return DOM_NO_ERR;
}

/**
 * Get the col_span property
 *
 * \param table_cell		The dom_html_table_cell_element object
 * \param no_wrap		The returned status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_cell_element_get_col_span(
		dom_html_table_cell_element *table_cell, dom_ulong *col_span)
{
	return dom_html_element_get_dom_ulong_property(&table_cell->base,
			"colspan", SLEN("colspan"), col_span);
}

/**
 * Set the col_span property
 *
 * \param table_cell		The dom_html_table_cell_element object
 * \param no_wrap		The status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_cell_element_set_col_span(
		dom_html_table_cell_element *table_cell, dom_ulong col_span)
{
	return dom_html_element_set_dom_ulong_property(&table_cell->base,
			"colspan", SLEN("colspan"), col_span);
}

/**
 * Get the row_span property
 *
 * \param table_cell		The dom_html_table_cell_element object
 * \param no_wrap		The returned status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_cell_element_get_row_span(
		dom_html_table_cell_element *table_cell, dom_ulong *row_span)
{
	return dom_html_element_get_dom_ulong_property(&table_cell->base,
			"rowspan", SLEN("rowspan"), row_span);
}

/**
 * Set the row_span property
 *
 * \param table_cell		The dom_html_table_cell_element object
 * \param no_wrap		The status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_cell_element_set_row_span(
		dom_html_table_cell_element *table_cell, dom_ulong row_span)
{
	return dom_html_element_set_dom_ulong_property(&table_cell->base,
			"rowspan", SLEN("rowspan"), row_span);
}

/**
 * Get the no_wrap property
 *
 * \param ele       The dom_html_table_cell_element object
 * \param no_wrap  The status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_cell_element_get_no_wrap(dom_html_table_cell_element *ele,
		bool *no_wrap)
{
	return dom_html_element_get_bool_property(&ele->base, "nowrap",
			SLEN("nowrap"), no_wrap);
}

/**
 * Set the no_wrap property
 *
 * \param ele       The dom_html_table_cell_element object
 * \param no_wrap  The status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_cell_element_set_no_wrap(dom_html_table_cell_element *ele,
		bool no_wrap)
{
	return dom_html_element_set_bool_property(&ele->base, "nowrap",
			SLEN("nowrap"), no_wrap);
}

