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

css_error css__cascade_color(uint32_t opv, css_style *style,
		css_select_state *state)
{
	enum flag_value flag_value = getFlagValue(opv);
	uint16_t value = CSS_COLOR_INHERIT;
	css_color color = 0;

	if (flag_value == FLAG_VALUE__NONE) {
		switch (getValue(opv)) {
		case COLOR_TRANSPARENT:
			value = CSS_COLOR_COLOR;
			break;
		case COLOR_CURRENT_COLOR:
			/* color: currentColor always computes to inherit */
			value = CSS_COLOR_INHERIT;
			flag_value = FLAG_VALUE_INHERIT;
			break;
		case COLOR_SET:
			value = CSS_COLOR_COLOR;
			color = *((css_color *) style->bytecode);
			advance_bytecode(style, sizeof(color));
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			flag_value)) {
		return set_color(state->computed, value, color);
	}

	return CSS_OK;
}

css_error css__set_color_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_color(style, hint->status, hint->data.color);
}

css_error css__initial_color(css_select_state *state)
{
	css_hint hint;
	css_error error;

	error = state->handler->ua_default_for_property(state->pw,
			CSS_PROP_COLOR, &hint);
	if (error != CSS_OK)
		return error;

	return css__set_color_from_hint(&hint, state->computed);
}

css_error css__copy_color(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_color color;
	uint8_t type = get_color(from, &color);

	if (from == to) {
		return CSS_OK;
	}

	return set_color(to, type, color);
}

css_error css__compose_color(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_color color;
	uint8_t type = get_color(child, &color);

	return css__copy_color(
			type == CSS_COLOR_INHERIT ? parent : child,
			result);
}

