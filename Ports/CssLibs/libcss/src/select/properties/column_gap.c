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

css_error css__cascade_column_gap(uint32_t opv, css_style *style,
		css_select_state *state)
{
	return css__cascade_length_normal(opv, style, state, set_column_gap);
}

css_error css__set_column_gap_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_column_gap(style, hint->status,
			hint->data.length.value, hint->data.length.unit);
}

css_error css__initial_column_gap(css_select_state *state)
{
	return set_column_gap(state->computed, CSS_COLUMN_GAP_NORMAL,
			INTTOFIX(1), CSS_UNIT_EM);
}

css_error css__copy_column_gap(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed length = INTTOFIX(1);
	css_unit unit = CSS_UNIT_EM;
	uint8_t type = get_column_gap(from, &length, &unit);

	if (from == to) {
		return CSS_OK;
	}

	return set_column_gap(to, type, length, unit);
}

css_error css__compose_column_gap(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed length = INTTOFIX(1);
	css_unit unit = CSS_UNIT_EM;
	uint8_t type = get_column_gap(child, &length, &unit);

	return css__copy_column_gap(
			type == CSS_COLUMN_GAP_INHERIT ? parent : child,
			result);
}
