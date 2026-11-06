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

css_error css__cascade_top(uint32_t opv, css_style *style,
		css_select_state *state)
{
	return css__cascade_length_auto(opv, style, state, set_top);
}

css_error css__set_top_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_top(style, hint->status,
			hint->data.length.value, hint->data.length.unit);
}

css_error css__initial_top(css_select_state *state)
{
	return set_top(state->computed, CSS_TOP_AUTO, 0, CSS_UNIT_PX);
}

css_error css__copy_top(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed length = 0;
	css_unit unit = CSS_UNIT_PX;
	uint8_t type = get_top(from, &length, &unit);

	if (from == to) {
		return CSS_OK;
	}

	return set_top(to, type, length, unit);
}

css_error css__compose_top(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed length = 0;
	css_unit unit = CSS_UNIT_PX;
	uint8_t type = get_top(child, &length, &unit);

	return css__copy_top(
			type == CSS_TOP_INHERIT ? parent : child,
			result);
}

