/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2017 Lucas Neves <lcneves@gmail.com>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

css_error css__cascade_flex_grow(uint32_t opv, css_style *style, 
		css_select_state *state)
{
	uint16_t value = CSS_FLEX_GROW_INHERIT;
	css_fixed flex_grow = 0;

	if (hasFlagValue(opv) == false) {
		value = CSS_FLEX_GROW_SET;

		flex_grow = *((css_fixed *) style->bytecode);
		advance_bytecode(style, sizeof(flex_grow));
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_flex_grow(state->computed, value, flex_grow);
	}

	return CSS_OK;
}

css_error css__set_flex_grow_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_flex_grow(style, hint->status, hint->data.fixed);
}

css_error css__initial_flex_grow(css_select_state *state)
{
	return set_flex_grow(state->computed, CSS_FLEX_GROW_SET, INTTOFIX(0));
}

css_error css__copy_flex_grow(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed flex_grow = 0;
	uint8_t type = get_flex_grow(from, &flex_grow);

	if (from == to) {
		return CSS_OK;
	}

	return set_flex_grow(to, type, flex_grow);
}

css_error css__compose_flex_grow(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed flex_grow = 0;
	uint8_t type = get_flex_grow(child, &flex_grow);

	return css__copy_flex_grow(
			type == CSS_FLEX_GROW_INHERIT ? parent : child,
			result);
}

