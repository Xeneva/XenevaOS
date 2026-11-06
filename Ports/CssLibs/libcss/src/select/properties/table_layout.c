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

css_error css__cascade_table_layout(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_TABLE_LAYOUT_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case TABLE_LAYOUT_AUTO:
			value = CSS_TABLE_LAYOUT_AUTO;
			break;
		case TABLE_LAYOUT_FIXED:
			value = CSS_TABLE_LAYOUT_FIXED;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_table_layout(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_table_layout_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_table_layout(style, hint->status);
}

css_error css__initial_table_layout(css_select_state *state)
{
	return set_table_layout(state->computed, CSS_TABLE_LAYOUT_AUTO);
}

css_error css__copy_table_layout(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_table_layout(to, get_table_layout(from));
}

css_error css__compose_table_layout(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_table_layout(child);

	return css__copy_table_layout(
			type == CSS_TABLE_LAYOUT_INHERIT ? parent : child,
			result);
}

