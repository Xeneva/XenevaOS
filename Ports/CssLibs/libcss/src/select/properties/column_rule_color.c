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

css_error css__cascade_column_rule_color(uint32_t opv, css_style *style,
		css_select_state *state)
{
	enum flag_value flag_value = getFlagValue(opv);
	uint16_t value = CSS_COLUMN_RULE_COLOR_INHERIT;
	css_color color = 0;

	if (flag_value == FLAG_VALUE__NONE) {
		switch (getValue(opv)) {
		case COLUMN_RULE_COLOR_TRANSPARENT:
			value = CSS_COLUMN_RULE_COLOR_COLOR;
			break;
		case COLUMN_RULE_COLOR_CURRENT_COLOR:
			value = CSS_COLUMN_RULE_COLOR_CURRENT_COLOR;
			flag_value = FLAG_VALUE_INHERIT;
			break;
		case COLUMN_RULE_COLOR_SET:
			value = CSS_COLUMN_RULE_COLOR_COLOR;
			color = *((css_fixed *) style->bytecode);
			advance_bytecode(style, sizeof(color));
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			flag_value)) {
		return set_column_rule_color(state->computed, value, color);
	}

	return CSS_OK;
}

css_error css__set_column_rule_color_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_column_rule_color(style, hint->status, hint->data.color);
}

css_error css__initial_column_rule_color(css_select_state *state)
{
	return set_column_rule_color(state->computed,
			CSS_COLUMN_RULE_COLOR_CURRENT_COLOR, 0);
}

css_error css__copy_column_rule_color(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_color color;
	uint8_t type = get_column_rule_color(from, &color);

	if (from == to) {
		return CSS_OK;
	}

	return set_column_rule_color(to, type, color);
}

css_error css__compose_column_rule_color(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_color color;
	uint8_t type = get_column_rule_color(child, &color);

	return css__copy_column_rule_color(
			type == CSS_COLUMN_RULE_COLOR_INHERIT ? parent : child,
			result);
}

