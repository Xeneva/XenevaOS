/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Michael Drake <tlsa@netsurf-browser.org>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

css_error css__cascade_break_after(uint32_t opv, css_style *style,
		css_select_state *state)
{
	return css__cascade_break_after_before_inside(opv, style, state,
			set_break_after);
}

css_error css__set_break_after_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_break_after(style, hint->status);
}

css_error css__initial_break_after(css_select_state *state)
{
	return set_break_after(state->computed, CSS_BREAK_AFTER_AUTO);
}

css_error css__copy_break_after(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_break_after(to, get_break_after(from));
}

css_error css__compose_break_after(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_break_after(child);

	return css__copy_break_after(
			type == CSS_BREAK_AFTER_INHERIT ? parent : child,
			result);
}

