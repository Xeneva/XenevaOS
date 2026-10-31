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

css_error css__cascade_column_count(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_COLUMN_COUNT_INHERIT;
	css_fixed count = 0;

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case COLUMN_COUNT_SET:
			value = CSS_COLUMN_COUNT_SET;
			count = *((css_fixed *) style->bytecode);
			advance_bytecode(style, sizeof(count));
			break;
		case COLUMN_COUNT_AUTO:
			value = CSS_COLUMN_COUNT_AUTO;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_column_count(state->computed, value, count);
	}

	return CSS_OK;
}

css_error css__set_column_count_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_column_count(style, hint->status, hint->data.integer);
}

css_error css__initial_column_count(css_select_state *state)
{
	return set_column_count(state->computed, CSS_COLUMN_COUNT_AUTO, 0);
}

css_error css__copy_column_count(
		const css_computed_style *from,
		css_computed_style *to)
{
	int32_t count = 0;
	uint8_t type = get_column_count(from, &count);

	if (from == to) {
		return CSS_OK;
	}

	return set_column_count(to, type, count);
}

css_error css__compose_column_count(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	int32_t count = 0;
	uint8_t type = get_column_count(child, &count);

	return css__copy_column_count(
			type == CSS_COLUMN_COUNT_INHERIT ? parent : child,
			result);
}
