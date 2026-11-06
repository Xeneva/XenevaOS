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

css_error css__cascade_break_inside(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_BREAK_INSIDE_AUTO;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case BREAK_INSIDE_AUTO:
			value = CSS_BREAK_INSIDE_AUTO;
			break;
		case BREAK_INSIDE_AVOID:
			value = CSS_BREAK_INSIDE_AVOID;
			break;
		case BREAK_INSIDE_AVOID_PAGE:
			value = CSS_BREAK_INSIDE_AVOID_PAGE;
			break;
		case BREAK_INSIDE_AVOID_COLUMN:
			value = CSS_BREAK_INSIDE_AVOID_COLUMN;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_break_inside(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_break_inside_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_break_inside(style, hint->status);
}

css_error css__initial_break_inside(css_select_state *state)
{
	return set_break_inside(state->computed, CSS_BREAK_INSIDE_AUTO);
}

css_error css__copy_break_inside(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_break_inside(to, get_break_inside(from));
}

css_error css__compose_break_inside(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_break_inside(child);

	return css__copy_break_inside(
			type == CSS_BREAK_INSIDE_INHERIT ? parent : child,
			result);
}

