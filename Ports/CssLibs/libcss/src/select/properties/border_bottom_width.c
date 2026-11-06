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

css_error css__cascade_border_bottom_width(uint32_t opv, css_style *width,
		css_select_state *state)
{
	return css__cascade_border_width(opv, width, state, set_border_bottom_width);
}

css_error css__set_border_bottom_width_from_hint(const css_hint *hint,
		css_computed_style *width)
{
	return set_border_bottom_width(width, hint->status,
			hint->data.length.value, hint->data.length.unit);
}

css_error css__initial_border_bottom_width(css_select_state *state)
{
	return set_border_bottom_width(state->computed, CSS_BORDER_WIDTH_MEDIUM,
			0, CSS_UNIT_PX);
}

css_error css__copy_border_bottom_width(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed length = 0;
	css_unit unit = CSS_UNIT_PX;
	uint8_t type = get_border_bottom_width(from, &length, &unit);

	if (from == to) {
		return CSS_OK;
	}

	return set_border_bottom_width(to, type, length, unit);
}

css_error css__compose_border_bottom_width(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed length = 0;
	css_unit unit = CSS_UNIT_PX;
	uint8_t type = get_border_bottom_width(child, &length, &unit);

	return css__copy_border_bottom_width(
			type == CSS_BORDER_WIDTH_INHERIT ? parent : child,
			result);
}

