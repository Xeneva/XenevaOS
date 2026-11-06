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

css_error css__cascade_flex_shrink(uint32_t opv, css_style *style, 
		css_select_state *state)
{
	uint16_t value = CSS_FLEX_SHRINK_INHERIT;
	css_fixed flex_shrink = 0;

	if (hasFlagValue(opv) == false) {
		value = CSS_FLEX_SHRINK_SET;

		flex_shrink = *((css_fixed *) style->bytecode);
		advance_bytecode(style, sizeof(flex_shrink));
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_flex_shrink(state->computed, value, flex_shrink);
	}

	return CSS_OK;
}

css_error css__set_flex_shrink_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_flex_shrink(style, hint->status, hint->data.fixed);
}

css_error css__initial_flex_shrink(css_select_state *state)
{
	return set_flex_shrink(state->computed, CSS_FLEX_SHRINK_SET, INTTOFIX(1));
}

css_error css__copy_flex_shrink(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed flex_shrink = 0;
	uint8_t type = get_flex_shrink(from, &flex_shrink);

	if (from == to) {
		return CSS_OK;
	}

	return set_flex_shrink(to, type, flex_shrink);
}

css_error css__compose_flex_shrink(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed flex_shrink = 0;
	uint8_t type = get_flex_shrink(child, &flex_shrink);

	return css__copy_flex_shrink(
			type == CSS_FLEX_SHRINK_INHERIT ? parent : child,
			result);
}

