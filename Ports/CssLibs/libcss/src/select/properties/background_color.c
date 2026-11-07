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

css_error css__cascade_background_color(uint32_t opv, css_style *style,
		css_select_state *state)
{
	return css__cascade_bg_border_color(opv, style, state, set_background_color);
}

css_error css__set_background_color_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_background_color(style, hint->status, hint->data.color);
}

css_error css__initial_background_color(css_select_state *state)
{
	return set_background_color(state->computed,
			CSS_BACKGROUND_COLOR_COLOR, 0);
}

css_error css__copy_background_color(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_color color;
	uint8_t type = get_background_color(from, &color);

	if (from == to) {
		return CSS_OK;
	}

	return set_background_color(to, type, color);
}

css_error css__compose_background_color(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_color color;
	uint8_t type = get_background_color(child, &color);

	return css__copy_background_color(
			type == CSS_BACKGROUND_COLOR_INHERIT ? parent : child,
			result);
}

