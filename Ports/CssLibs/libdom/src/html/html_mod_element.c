/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar<rsk1coder99@gmail.com>
 */
#include <assert.h>
#include <stdlib.h>

#include <dom/html/html_mod_element.h>

#include "html/html_document.h"
#include "html/html_mod_element.h"

#include "core/node.h"
#include "core/attr.h"
#include "utils/utils.h"

static const struct dom_element_protected_vtable _protect_vtable = {
	{
		DOM_NODE_PROTECT_VTABLE_HTML_MOD_ELEMENT
	},
	DOM_HTML_MOD_ELEMENT_PROTECT_VTABLE
};

/**
 * Create a dom_html_mod_element object
 *
 * \param params  The html element creation parameters
 * \param ele     The returned element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_mod_element_create(
		struct dom_html_element_create_params *params,
		struct dom_html_mod_element **ele)
{
	struct dom_node_internal *node;

	*ele = malloc(sizeof(dom_html_mod_element));
	if (*ele == NULL)
		return DOM_NO_MEM_ERR;

	/* Set up vtables */
	node = (struct dom_node_internal *) *ele;
	node->base.vtable = &_dom_html_element_vtable;
	node->vtable = &_protect_vtable;

	return _dom_html_mod_element_initialise(params, *ele);
}

/**
 * Initialise a dom_html_mod_element object
 *
 * \param params  The html element creation parameters
 * \param ele     The dom_html_mod_element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_mod_element_initialise(
		struct dom_html_element_create_params *params,
		struct dom_html_mod_element *ele)
{
	return _dom_html_element_initialise(params, &ele->base);
}

/**
 * Finalise a dom_html_mod_element object
 *
 * \param ele  The dom_html_mod_element object
 */
void _dom_html_mod_element_finalise(struct dom_html_mod_element *ele)
{
	_dom_html_element_finalise(&ele->base);
}

/**
 * Destroy a dom_html_mod_element object
 *
 * \param ele  The dom_html_mod_element object
 */
void _dom_html_mod_element_destroy(struct dom_html_mod_element *ele)
{
	_dom_html_mod_element_finalise(ele);
	free(ele);
}


/*------------------------------------------------------------------------*/
/* The protected virtual functions */

/* The virtual function used to parse attribute value, see src/core/element.c
 * for detail */
dom_exception _dom_html_mod_element_parse_attribute(dom_element *ele,
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
void _dom_virtual_html_mod_element_destroy(dom_node_internal *node)
{
	_dom_html_mod_element_destroy((struct dom_html_mod_element *) node);
}

/* The virtual copy function, see src/core/node.c for detail */
dom_exception _dom_html_mod_element_copy(
		dom_node_internal *old, dom_node_internal **copy)
{
	dom_html_mod_element *new_node;
	dom_exception err;

	new_node = malloc(sizeof(dom_html_mod_element));
	if (new_node == NULL)
		return DOM_NO_MEM_ERR;

	err = dom_html_mod_element_copy_internal(old, new_node);
	if (err != DOM_NO_ERR) {
		free(new_node);
		return err;
	}

	*copy = (dom_node_internal *) new_node;

	return DOM_NO_ERR;
}

dom_exception _dom_html_mod_element_copy_internal(
		dom_html_mod_element *old,
		dom_html_mod_element *new)
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
	dom_exception dom_html_mod_element_get_##attr(		\
		dom_html_mod_element *element,			\
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
dom_exception dom_html_mod_element_set_##attr(			\
		dom_html_mod_element *element,			\
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

SIMPLE_GET_SET(cite);
SIMPLE_GET_SET(date_time);

