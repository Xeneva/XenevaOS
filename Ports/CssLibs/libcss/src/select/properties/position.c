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

css_error css__cascade_position(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_POSITION_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case POSITION_STATIC:
			value = CSS_POSITION_STATIC;
			break;
		case POSITION_RELATIVE:
			value = CSS_POSITION_RELATIVE;
			break;
		case POSITION_ABSOLUTE:
			value = CSS_POSITION_ABSOLUTE;
			break;
		case POSITION_FIXED:
			value = CSS_POSITION_FIXED;
			break;
		case POSITION_STICKY:
			value = CSS_POSITION_STICKY;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_position(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_position_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_position(style, hint->status);
}

css_error css__initial_position(css_select_state *state)
{
	return set_position(state->computed, CSS_POSITION_STATIC);
}

css_error css__copy_position(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_position(to, get_position(from));
}

css_error css__compose_position(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_position(child);

	return css__copy_position(
			type == CSS_POSITION_INHERIT ? parent : child,
			result);
}

