/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2011 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

css_error css__cascade_opacity(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_OPACITY_INHERIT;
	css_fixed opacity = 0;

	if (hasFlagValue(opv) == false) {
		value = CSS_OPACITY_SET;

		opacity = *((css_fixed *) style->bytecode);
		advance_bytecode(style, sizeof(opacity));
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_opacity(state->computed, value, opacity);
	}

	return CSS_OK;
}

css_error css__set_opacity_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_opacity(style, hint->status, hint->data.fixed);
}

css_error css__initial_opacity(css_select_state *state)
{
	return set_opacity(state->computed, CSS_OPACITY_SET, INTTOFIX(1));
}

css_error css__copy_opacity(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed opacity = 0;
	uint8_t type = get_opacity(from, &opacity);

	if (from == to) {
		return CSS_OK;
	}

	return set_opacity(to, type, opacity);
}

css_error css__compose_opacity(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed opacity = 0;
	uint8_t type = get_opacity(child, &opacity);

	return css__copy_opacity(
			type == CSS_OPACITY_INHERIT ? parent : child,
			result);
}

