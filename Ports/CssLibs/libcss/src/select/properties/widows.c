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

css_error css__cascade_widows(uint32_t opv, css_style *style,
		css_select_state *state)
{
	return css__cascade_number(opv, style, state, set_widows);
}

css_error css__set_widows_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_widows(style, hint->status, hint->data.integer);
}

css_error css__initial_widows(css_select_state *state)
{
	return set_widows(state->computed, CSS_WIDOWS_SET, 2);
}

css_error css__copy_widows(
		const css_computed_style *from,
		css_computed_style *to)
{
	int32_t count = 0;
	uint8_t type = get_widows(from, &count);

	if (from == to) {
		return CSS_OK;
	}

	return set_widows(to, type, count);
}

css_error css__compose_widows(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	int32_t count = 0;
	uint8_t type = get_widows(child, &count);

	return css__copy_widows(
			type == CSS_WIDOWS_INHERIT ? parent : child,
			result);
}

