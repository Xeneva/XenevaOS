/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2017 Michael Drake <tlsa@netsurf-browser.org>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

css_error css__cascade_box_sizing(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_BOX_SIZING_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case BOX_SIZING_CONTENT_BOX:
			value = CSS_BOX_SIZING_CONTENT_BOX;
			break;
		case BOX_SIZING_BORDER_BOX:
			value = CSS_BOX_SIZING_BORDER_BOX;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_box_sizing(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_box_sizing_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_box_sizing(style, hint->status);
}

css_error css__initial_box_sizing(css_select_state *state)
{
	return set_box_sizing(state->computed, CSS_BOX_SIZING_CONTENT_BOX);
}

css_error css__copy_box_sizing(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_box_sizing(to, get_box_sizing(from));
}

css_error css__compose_box_sizing(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_box_sizing(child);

	return css__copy_box_sizing(
			type == CSS_BOX_SIZING_INHERIT ? parent : child,
			result);
}

