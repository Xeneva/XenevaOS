/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

css_error css__cascade_list_style_image(uint32_t opv, css_style *style,
		css_select_state *state)
{
	return css__cascade_uri_none(opv, style, state, set_list_style_image);
}

css_error css__set_list_style_image_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	css_error error;

	error = set_list_style_image(style, hint->status, hint->data.string);

	if (hint->data.string != NULL)
		lwc_string_unref(hint->data.string);

	return error;
}

css_error css__initial_list_style_image(css_select_state *state)
{
	return set_list_style_image(state->computed,
			CSS_LIST_STYLE_IMAGE_NONE, NULL);
}

css_error css__copy_list_style_image(
		const css_computed_style *from,
		css_computed_style *to)
{
	lwc_string *url;
	uint8_t type = get_list_style_image(from, &url);

	if (from == to) {
		return CSS_OK;
	}

	return set_list_style_image(to, type, url);
}

css_error css__compose_list_style_image(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	lwc_string *url;
	uint8_t type = get_list_style_image(child, &url);

	return css__copy_list_style_image(
			type == CSS_LIST_STYLE_IMAGE_INHERIT ? parent : child,
			result);
}

