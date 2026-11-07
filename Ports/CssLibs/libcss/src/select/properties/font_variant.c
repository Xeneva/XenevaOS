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

css_error css__cascade_font_variant(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_FONT_VARIANT_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case FONT_VARIANT_NORMAL:
			value = CSS_FONT_VARIANT_NORMAL;
			break;
		case FONT_VARIANT_SMALL_CAPS:
			value = CSS_FONT_VARIANT_SMALL_CAPS;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_font_variant(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_font_variant_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_font_variant(style, hint->status);
}

css_error css__initial_font_variant(css_select_state *state)
{
	return set_font_variant(state->computed, CSS_FONT_VARIANT_NORMAL);
}

css_error css__copy_font_variant(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_font_variant(to, get_font_variant(from));
}

css_error css__compose_font_variant(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_font_variant(child);

	return css__copy_font_variant(
			type == CSS_FONT_VARIANT_INHERIT ? parent : child,
			result);
}

