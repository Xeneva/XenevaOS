/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

css_error css__cascade_fill_opacity(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_FILL_OPACITY_INHERIT;
	css_fixed fill_opacity = 0;

	if (hasFlagValue(opv) == false) {
		value = CSS_FILL_OPACITY_SET;

		fill_opacity = *((css_fixed *) style->bytecode);
		advance_bytecode(style, sizeof(fill_opacity));
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_fill_opacity(state->computed, value, fill_opacity);
	}

	return CSS_OK;
}

css_error css__set_fill_opacity_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_fill_opacity(style, hint->status, hint->data.fixed);
}

css_error css__initial_fill_opacity(css_select_state *state)
{
	return set_fill_opacity(state->computed, CSS_FILL_OPACITY_SET, INTTOFIX(1));
}

css_error css__copy_fill_opacity(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed fill_opacity = 0;
	uint8_t type = get_fill_opacity(from, &fill_opacity);

	if (from == to) {
		return CSS_OK;
	}

	return set_fill_opacity(to, type, fill_opacity);
}

css_error css__compose_fill_opacity(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed fill_opacity = 0;
	uint8_t type = get_fill_opacity(child, &fill_opacity);

	return css__copy_fill_opacity(
			type == CSS_FILL_OPACITY_INHERIT ? parent : child,
			result);
}

