/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */

#include <assert.h>
#include <stdlib.h>

#include <dom/html/html_fieldset_element.h>

#include "html/html_document.h"
#include "html/html_fieldset_element.h"

#include "core/node.h"
#include "utils/utils.h"

static const struct dom_element_protected_vtable _protect_vtable = {
	{
		DOM_NODE_PROTECT_VTABLE_HTML_FIELDSET_ELEMENT
	},
	DOM_HTML_FIELDSET_ELEMENT_PROTECT_VTABLE
};

/**
 * Create a dom_html_field_set_element object
 *
 * \param params  The html element creation parameters
 * \param ele     The returned element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_field_set_element_create(
		struct dom_html_element_create_params *params,
		struct dom_html_field_set_element **ele)
{
	struct dom_node_internal *node;

	*ele = malloc(sizeof(dom_html_field_set_element));
	if (*ele == NULL)
		return DOM_NO_MEM_ERR;
	
	/* Set up vtables */
	node = (struct dom_node_internal *) *ele;
	node->base.vtable = &_dom_html_element_vtable;
	node->vtable = &_protect_vtable;

	return _dom_html_field_set_element_initialise(params, *ele);
}

/**
 * Initialise a dom_html_field_set_element object
 *
 * \param params  The html element creation parameters
 * \param ele     The dom_html_field_set_element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_field_set_element_initialise(
		struct dom_html_element_create_params *params,
		struct dom_html_field_set_element *ele)
{
	return _dom_html_element_initialise(params, &ele->base);
}

/**
 * Finalise a dom_html_field_set_element object
 *
 * \param ele  The dom_html_field_set_element object
 */
void _dom_html_field_set_element_finalise(struct dom_html_field_set_element *ele)
{
	_dom_html_element_finalise(&ele->base);
}

/**
 * Destroy a dom_html_field_set_element object
 *
 * \param ele  The dom_html_field_set_element object
 */
void _dom_html_field_set_element_destroy(struct dom_html_field_set_element *ele)
{
	_dom_html_field_set_element_finalise(ele);
	free(ele);
}

/*------------------------------------------------------------------------*/
/* The protected virtual functions */

/* The virtual function used to parse attribute value, see src/core/element.c
 * for detail */
dom_exception _dom_html_field_set_element_parse_attribute(dom_element *ele,
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
void _dom_virtual_html_field_set_element_destroy(dom_node_internal *node)
{
	_dom_html_field_set_element_destroy((struct dom_html_field_set_element *) node);
}

/* The virtual copy function, see src/core/node.c for detail */
dom_exception _dom_html_field_set_element_copy(
		dom_node_internal *old, dom_node_internal **copy)
{
	dom_html_field_set_element *new_node;
	dom_exception err;

	new_node = malloc(sizeof(dom_html_field_set_element));
	if (new_node == NULL)
		return DOM_NO_MEM_ERR;

	err = dom_html_field_set_element_copy_internal(old, new_node);
	if (err != DOM_NO_ERR) {
		free(new_node);
		return err;
	}

	*copy = (dom_node_internal *) new_node;

	return DOM_NO_ERR;
}

dom_exception _dom_html_field_set_element_copy_internal(
		dom_html_field_set_element *old,
		dom_html_field_set_element *new)
{
	dom_exception err;

	err = dom_html_element_copy_internal(old, new);
	if (err != DOM_NO_ERR) {
		return err;
	}

	return DOM_NO_ERR;
}

/**
 * Get the dom_html_form_element object
 *
 * \param field_set	The dom_html_legend_element object
 * \param form		The returned dom_html_form_element object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */

dom_exception dom_html_field_set_element_get_form(
	dom_html_field_set_element *field_set, dom_html_form_element **form)
{
	dom_html_document *doc = 
		(dom_html_document *) ((dom_node_internal *) field_set)->owner;
	dom_node_internal *form_tmp = ((dom_node_internal *) field_set)->parent;

	/* Search ancestor chain for FIELDSET element */
	while (form_tmp != NULL) {
		if (form_tmp->type == DOM_ELEMENT_NODE &&
				dom_string_caseless_isequal(form_tmp->name,
						doc->elements[DOM_HTML_ELEMENT_TYPE_FORM]))
			break;

		form_tmp = form_tmp->parent;
	}

	if (form_tmp != NULL) {
		*form = (dom_html_form_element *) dom_node_ref(form_tmp);
		return DOM_NO_ERR;
	}

	*form = NULL;

	return DOM_NO_ERR;
}
