/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar<rsk1coder99@gmail.com>
 */
#include <assert.h>
#include <stdlib.h>

#include <dom/html/html_tablesection_element.h>

#include "html/html_document.h"
#include "html/html_tablesection_element.h"
#include "html/html_tablerow_element.h"

#include "html/html_collection.h"
#include "html/html_element.h"

#include "core/node.h"
#include "core/attr.h"
#include "utils/utils.h"

static const struct dom_element_protected_vtable _protect_vtable = {
	{
		DOM_NODE_PROTECT_VTABLE_HTML_TABLE_SECTION_ELEMENT
	},
	DOM_HTML_TABLE_SECTION_ELEMENT_PROTECT_VTABLE
};

/**
 * Create a dom_html_table_section_element object
 *
 * \param params  The html element creation parameters
 * \param ele     The returned element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_table_section_element_create(
		struct dom_html_element_create_params *params,
		struct dom_html_table_section_element **ele)
{
	struct dom_node_internal *node;

	*ele = malloc(sizeof(dom_html_table_section_element));
	if (*ele == NULL)
		return DOM_NO_MEM_ERR;

	/* Set up vtables */
	node = (struct dom_node_internal *) *ele;
	node->base.vtable = &_dom_html_element_vtable;
	node->vtable = &_protect_vtable;

	return _dom_html_table_section_element_initialise(params, *ele);
}

/**
 * Initialise a dom_html_table_section_element object
 *
 * \param params  The html element creation parameters
 * \param ele     The dom_html_table_section_element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_table_section_element_initialise(
		struct dom_html_element_create_params *params,
		struct dom_html_table_section_element *ele)
{
	return _dom_html_element_initialise(params, &ele->base);
}

/**
 * Finalise a dom_html_table_section_element object
 *
 * \table_section ele  The dom_html_table_section_element object
 */
void _dom_html_table_section_element_finalise(struct dom_html_table_section_element *ele)
{
	_dom_html_element_finalise(&ele->base);
}

/**
 * Destroy a dom_html_table_section_element object
 *
 * \table_section ele  The dom_html_table_section_element object
 */
void _dom_html_table_section_element_destroy(struct dom_html_table_section_element *ele)
{
	_dom_html_table_section_element_finalise(ele);
	free(ele);
}

/*------------------------------------------------------------------------*/
/* The protected virtual functions */

/* The virtual function used to parse attribute value, see src/core/element.c
 * for detail */
dom_exception _dom_html_table_section_element_parse_attribute(dom_element *ele,
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
void _dom_virtual_html_table_section_element_destroy(dom_node_internal *node)
{
	_dom_html_table_section_element_destroy((struct dom_html_table_section_element *) node);
}

/* The virtual copy function, see src/core/node.c for detail */
dom_exception _dom_html_table_section_element_copy(
		dom_node_internal *old, dom_node_internal **copy)
{
	dom_html_table_section_element *new_node;
	dom_exception err;

	new_node = malloc(sizeof(dom_html_table_section_element));
	if (new_node == NULL)
		return DOM_NO_MEM_ERR;

	err = dom_html_table_section_element_copy_internal(old, new_node);
	if (err != DOM_NO_ERR) {
		free(new_node);
		return err;
	}

	*copy = (dom_node_internal *) new_node;

	return DOM_NO_ERR;
}

dom_exception _dom_html_table_section_element_copy_internal(
		dom_html_table_section_element *old,
		dom_html_table_section_element *new)
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
	dom_exception dom_html_table_section_element_get_##attr(		\
			dom_html_table_section_element *element,			\
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
	dom_exception dom_html_table_section_element_set_##attr(			\
			dom_html_table_section_element *element,			\
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
SIMPLE_GET_SET(ch);
SIMPLE_GET_SET(ch_off);
SIMPLE_GET_SET(v_align);

/* The callback function for  _dom_html_collection_create*/
static bool table_section_callback(struct dom_node_internal *node, void *ctx)
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
 * \param element       The dom_html_table_section_element object
 * \param rows		The Status
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_section_element_get_rows(
		dom_html_table_section_element *element,
		dom_html_collection **rows)
{
	dom_html_document *doc = (dom_html_document *) ((dom_node_internal *) element)->owner;
	return _dom_html_collection_create(doc, (dom_node_internal *)element, 
			table_section_callback, (void *)doc, rows);
}

/**
 * Insert Row before the given Index
 *
 * \param element       The dom_html_table_section_element object
 * \param index         The Index of the Row node to be inserted
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_section_element_insert_row(
		dom_html_table_section_element *element,
		int32_t index, dom_html_element **new_row)
{
	dom_html_document *doc = (dom_html_document *) ((dom_node_internal *) element)->owner;
	dom_html_collection *rows; 	/*< The collection of rows in input table_section_element*/
	uint32_t len; 			/*< The size of the row collection */
	dom_exception exp;		/*< Variable for getting the exceptions*/
	dom_node *new_node;

	struct dom_html_element_create_params params = {
		.type = DOM_HTML_ELEMENT_TYPE_TR,
		.doc = doc,
		.name = doc->elements[DOM_HTML_ELEMENT_TYPE_TR],
		.namespace = ((dom_node_internal *)element)->namespace,
		.prefix = ((dom_node_internal *)element)->prefix
	};

	exp = _dom_html_table_row_element_create(&params,
			(dom_html_table_row_element **)(void *) &new_node);
	if(exp != DOM_NO_ERR)
		return exp;
	
	exp = dom_html_table_section_element_get_rows(element, &rows);
	if(exp != DOM_NO_ERR) {
		dom_node_unref(new_node);
		return exp;
	}

	exp = dom_html_collection_get_length(rows, &len);
	if (exp != DOM_NO_ERR) {
		dom_node_unref(new_node);
		dom_html_collection_unref(rows);
		return exp;
	}
	
	if (index < -1 || index > (int32_t)len) {
		/* Check for index validity */
		dom_html_collection_unref(rows);
		dom_node_unref(new_node);
		return DOM_INDEX_SIZE_ERR;

	} else if (index == -1 || index == (int32_t)len) {
		dom_html_collection_unref(rows);
		exp = dom_node_append_child(element, new_node, new_row);
	} else {
		dom_node *node;
		dom_html_collection_item(rows, index, &node);
		dom_html_collection_unref(rows);

		exp = dom_node_insert_before(element,
				new_node, node, new_row);
		dom_node_unref(node);
	}

	dom_node_unref(new_node);

	return exp;
}

/**
 * Delete Row at given Index
 *
 * \param element       The dom_html_table_section_element object
 * \param index		The Index of the Row node to be deleted
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_html_table_section_element_delete_row(
		dom_html_table_section_element *element,
		int32_t index) {
	dom_node *node, *old_node;	/*< The node at the (index)th position*/
	dom_html_collection *rows; 	/*< The collection of rows in input table_section_element*/
	uint32_t len; 			/*< The size of the row collection */
	dom_exception exp;		/*< Temporary variable to store & check the exceptions*/

	exp = dom_html_table_section_element_get_rows(element, &rows);
	if (exp != DOM_NO_ERR) {
		return exp;
	}

	exp = dom_html_collection_get_length(rows, &len);
	if (exp != DOM_NO_ERR) {
		dom_html_collection_unref(rows);
		return exp;
	}

	if (index < -1 || index >= (int32_t) len || (index == -1 && len == 0)) {
		/* Check for index validity */
		dom_html_collection_unref(rows);
		return DOM_INDEX_SIZE_ERR;
	} 

	if (index == -1)
		index = len - 1;

	exp = dom_html_collection_item(rows, index, &node);
	if (exp != DOM_NO_ERR) {
		dom_html_collection_unref(rows);
		return exp;
	}

	exp = dom_node_remove_child(element, node, &old_node);
	if (exp == DOM_NO_ERR)
		dom_node_unref(old_node);

	dom_node_unref(node);
	dom_html_collection_unref(rows);

	return exp;
}

