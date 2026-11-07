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

css_error css__cascade_outline_color(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_OUTLINE_COLOR_INHERIT;
	css_color color = 0;

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case OUTLINE_COLOR_TRANSPARENT:
			value = CSS_OUTLINE_COLOR_COLOR;
			break;
		case OUTLINE_COLOR_CURRENT_COLOR:
			value = CSS_OUTLINE_COLOR_CURRENT_COLOR;
			break;
		case OUTLINE_COLOR_SET:
			value = CSS_OUTLINE_COLOR_COLOR;
			color = *((css_color *) style->bytecode);
			advance_bytecode(style, sizeof(color));
			break;
		case OUTLINE_COLOR_INVERT:
			value = CSS_OUTLINE_COLOR_INVERT;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_outline_color(state->computed, value, color);
	}

	return CSS_OK;
}

css_error css__set_outline_color_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_outline_color(style, hint->status, hint->data.color);
}

css_error css__initial_outline_color(css_select_state *state)
{
	return set_outline_color(state->computed, CSS_OUTLINE_COLOR_INVERT, 0);
}

css_error css__copy_outline_color(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_color color = 0;
	uint8_t type = get_outline_color(from, &color);

	if (from == to) {
		return CSS_OK;
	}

	return set_outline_color(to, type, color);
}

css_error css__compose_outline_color(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_color color = 0;
	uint8_t type = get_outline_color(child, &color);

	return css__copy_outline_color(
			type == CSS_OUTLINE_COLOR_INHERIT ? parent : child,
			result);
}
