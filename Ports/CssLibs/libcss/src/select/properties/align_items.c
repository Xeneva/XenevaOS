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

css_error css__cascade_align_items(uint32_t opv, css_style *style, 
		css_select_state *state)
{
	uint16_t value = CSS_ALIGN_ITEMS_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case ALIGN_ITEMS_STRETCH:
			value = CSS_ALIGN_ITEMS_STRETCH;
			break;
		case ALIGN_ITEMS_FLEX_START:
			value = CSS_ALIGN_ITEMS_FLEX_START;
			break;
		case ALIGN_ITEMS_FLEX_END:
			value = CSS_ALIGN_ITEMS_FLEX_END;
			break;
		case ALIGN_ITEMS_CENTER:
			value = CSS_ALIGN_ITEMS_CENTER;
			break;
		case ALIGN_ITEMS_BASELINE:
			value = CSS_ALIGN_ITEMS_BASELINE;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_align_items(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_align_items_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_align_items(style, hint->status);
}

css_error css__initial_align_items(css_select_state *state)
{
	return set_align_items(state->computed, CSS_ALIGN_ITEMS_STRETCH);
}

css_error css__copy_align_items(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_align_items(to, get_align_items(from));
}

css_error css__compose_align_items(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_align_items(child);

	return css__copy_align_items(
			type == CSS_ALIGN_ITEMS_INHERIT ? parent : child,
			result);
}

