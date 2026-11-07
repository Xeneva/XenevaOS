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

css_error css__cascade_align_content(uint32_t opv, css_style *style, 
		css_select_state *state)
{
	uint16_t value = CSS_ALIGN_CONTENT_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case ALIGN_CONTENT_STRETCH:
			value = CSS_ALIGN_CONTENT_STRETCH;
			break;
		case ALIGN_CONTENT_FLEX_START:
			value = CSS_ALIGN_CONTENT_FLEX_START;
			break;
		case ALIGN_CONTENT_FLEX_END:
			value = CSS_ALIGN_CONTENT_FLEX_END;
			break;
		case ALIGN_CONTENT_CENTER:
			value = CSS_ALIGN_CONTENT_CENTER;
			break;
		case ALIGN_CONTENT_SPACE_BETWEEN:
			value = CSS_ALIGN_CONTENT_SPACE_BETWEEN;
			break;
		case ALIGN_CONTENT_SPACE_AROUND:
			value = CSS_ALIGN_CONTENT_SPACE_AROUND;
			break;
		case ALIGN_CONTENT_SPACE_EVENLY:
			value = CSS_ALIGN_CONTENT_SPACE_EVENLY;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_align_content(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_align_content_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_align_content(style, hint->status);
}

css_error css__initial_align_content(css_select_state *state)
{
	return set_align_content(state->computed, CSS_ALIGN_CONTENT_STRETCH);
}

css_error css__copy_align_content(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_align_content(to, get_align_content(from));
}

css_error css__compose_align_content(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_align_content(child);

	return css__copy_align_content(
			type == CSS_ALIGN_CONTENT_INHERIT ? parent : child,
			result);
}

