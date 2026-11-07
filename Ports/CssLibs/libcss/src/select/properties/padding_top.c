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

css_error css__cascade_padding_top(uint32_t opv, css_style *style,
		css_select_state *state)
{
	return css__cascade_length(opv, style, state, set_padding_top);
}

css_error css__set_padding_top_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_padding_top(style, hint->status,
			hint->data.length.value, hint->data.length.unit);
}

css_error css__initial_padding_top(css_select_state *state)
{
	return set_padding_top(state->computed, CSS_PADDING_SET, 0, CSS_UNIT_PX);
}

css_error css__copy_padding_top(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed length = 0;
	css_unit unit = CSS_UNIT_PX;
	uint8_t type = get_padding_top(from, &length, &unit);

	if (from == to) {
		return CSS_OK;
	}

	return set_padding_top(to, type, length, unit);
}

css_error css__compose_padding_top(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed length = 0;
	css_unit unit = CSS_UNIT_PX;
	uint8_t type = get_padding_top(child, &length, &unit);

	return css__copy_padding_top(
			type == CSS_PADDING_INHERIT ? parent : child,
			result);
}

