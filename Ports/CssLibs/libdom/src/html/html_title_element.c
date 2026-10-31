/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku.com>
 */

#include <assert.h>
#include <stdlib.h>

#include <dom/core/characterdata.h>
#include <dom/core/text.h>

#include "html/html_document.h"
#include "html/html_title_element.h"

#include "core/node.h"
#include "utils/utils.h"

static const struct dom_element_protected_vtable _protect_vtable = {
	{
		DOM_NODE_PROTECT_VTABLE_HTML_TITLE_ELEMENT
	},
	DOM_HTML_TITLE_ELEMENT_PROTECT_VTABLE
};

/**
 * Create a dom_html_title_element object
 *
 * \param params  The html element creation parameters
 * \param ele     The returned element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_title_element_create(
		struct dom_html_element_create_params *params,
		struct dom_html_title_element **ele)
{
	struct dom_node_internal *node;

	*ele = malloc(sizeof(dom_html_title_element));
	if (*ele == NULL)
		return DOM_NO_MEM_ERR;
	
	/* Set up vtables */
	node = (struct dom_node_internal *) *ele;
	node->base.vtable = &_dom_html_element_vtable;
	node->vtable = &_protect_vtable;

	return _dom_html_title_element_initialise(params, *ele);
}

/**
 * Initialise a dom_html_title_element object
 *
 * \param params  The html element creation parameters
 * \param ele     The dom_html_title_element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_title_element_initialise(
		struct dom_html_element_create_params *params,
		struct dom_html_title_element *ele)
{
	return _dom_html_element_initialise(params, &ele->base);
}

/**
 * Finalise a dom_html_title_element object
 *
 * \param ele  The dom_html_title_element object
 */
void _dom_html_title_element_finalise(struct dom_html_title_element *ele)
{
	_dom_html_element_finalise(&ele->base);
}

/**
 * Destroy a dom_html_title_element object
 *
 * \param ele  The dom_html_title_element object
 */
void _dom_html_title_element_destroy(struct dom_html_title_element *ele)
{
	_dom_html_title_element_finalise(ele);
	free(ele);
}

/*------------------------------------------------------------------------*/
/* The protected virtual functions */

/* The virtual function used to parse attribute value, see src/core/element.c
 * for detail */
dom_exception _dom_html_title_element_parse_attribute(dom_element *ele,
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
void _dom_virtual_html_title_element_destroy(dom_node_internal *node)
{
	_dom_html_title_element_destroy((struct dom_html_title_element *) node);
}

/* The virtual copy function, see src/core/node.c for detail */
dom_exception _dom_html_title_element_copy(
		dom_node_internal *old, dom_node_internal **copy)
{
	dom_html_title_element *new_node;
	dom_exception err;

	new_node = malloc(sizeof(dom_html_title_element));
	if (new_node == NULL)
		return DOM_NO_MEM_ERR;

	err = dom_html_title_element_copy_internal(old, new_node);
	if (err != DOM_NO_ERR) {
		free(new_node);
		return err;
	}

	*copy = (dom_node_internal *) new_node;

	return DOM_NO_ERR;
}

dom_exception _dom_html_title_element_copy_internal(
		dom_html_title_element *old,
		dom_html_title_element *new)
{
	dom_exception err;

	err = dom_html_element_copy_internal(old, new);
	if (err != DOM_NO_ERR) {
		return err;
	}

	return DOM_NO_ERR;
}

/*-----------------------------------------------------------------------*/
/* Public APIs */

/**
 * Get the text of title
 *
 * \param ele   The title element
 * \param text  The returned text
 * \return DOM_NO_ERR on success, appropriated dom_exception on failure.
 */
dom_exception dom_html_title_element_get_text(dom_html_title_element *ele,
		dom_string **text)
{
	return dom_node_get_text_content(ele, text);
}

/**
 * Set the text of title
 *
 * \param ele   The title element
 * \param text  The text data to be set
 * \return DOM_NO_ERR on success, appropriated dom_exception on failure.
 */
dom_exception dom_html_title_element_set_text(dom_html_title_element *ele,
		dom_string *text)
{
	return dom_node_set_text_content(ele, text);
}

