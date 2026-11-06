/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef css_select_properties_helpers_h_
#define css_select_properties_helpers_h_

#include "select/helpers.h"

uint32_t generic_destroy_color(void *bytecode);
uint32_t generic_destroy_uri(void *bytecode);
uint32_t generic_destroy_length(void *bytecode);
uint32_t generic_destroy_number(void *bytecode);

css_error css__cascade_bg_border_color(uint32_t opv, css_style *style,
		css_select_state *state,
		css_error (*fun)(css_computed_style *, uint8_t, css_color));
css_error css__cascade_uri_none(uint32_t opv, css_style *style,
		css_select_state *state,
		css_error (*fun)(css_computed_style *, uint8_t,
				lwc_string *));
css_error css__cascade_border_style(uint32_t opv, css_style *style,
		css_select_state *state,
		css_error (*fun)(css_computed_style *, uint8_t));
css_error css__cascade_border_width(uint32_t opv, css_style *style,
		css_select_state *state,
		css_error (*fun)(css_computed_style *, uint8_t, css_fixed,
				css_unit));
css_error css__cascade_length_auto(uint32_t opv, css_style *style,
		css_select_state *state,
		css_error (*fun)(css_computed_style *, uint8_t, css_fixed,
				css_unit));
css_error css__cascade_length_normal(uint32_t opv, css_style *style,
		css_select_state *state,
		css_error (*fun)(css_computed_style *, uint8_t, css_fixed,
				css_unit));
css_error css__cascade_length_none(uint32_t opv, css_style *style,
		css_select_state *state,
		css_error (*fun)(css_computed_style *, uint8_t, css_fixed,
				css_unit));
css_error css__cascade_length(uint32_t opv, css_style *style,
		css_select_state *state,
		css_error (*fun)(css_computed_style *, uint8_t, css_fixed,
				css_unit));
css_error css__cascade_number(uint32_t opv, css_style *style,
		css_select_state *state,
		css_error (*fun)(css_computed_style *, uint8_t, css_fixed));
css_error css__cascade_page_break_after_before_inside(uint32_t opv,
		css_style *style, css_select_state *state,
		css_error (*fun)(css_computed_style *, uint8_t));
css_error css__cascade_break_after_before_inside(uint32_t opv,
		css_style *style, css_select_state *state,
		css_error (*fun)(css_computed_style *, uint8_t));
css_error css__cascade_counter_increment_reset(uint32_t opv, css_style *style,
		css_select_state *state,
		css_error (*fun)(css_computed_style *, uint8_t,
				css_computed_counter *));

/** Copy NULL terminated array of lwc_string pointers. */
static inline css_error css__copy_lwc_string_array(
		bool ref,
		lwc_string *const*orig,
		lwc_string ***copy_out)
{
	size_t count = 0;
	lwc_string **copy = NULL;

	if (orig != NULL) {
		for (lwc_string *const*i = orig; (*i) != NULL; i++) {
			count++;
		}

		copy = malloc((count + 1) * sizeof(*copy));
		if (copy == NULL) {
			return CSS_NOMEM;
		}

		if (ref) {
			for (size_t i = 0; i < count; i++) {
				copy[i] = lwc_string_ref(orig[i]);
			}
			copy[count] = NULL;
		} else {
			memcpy(copy, orig, (count + 1) * sizeof(*copy));
		}
	}

	*copy_out = copy;
	return CSS_OK;
}

/** Copy NULL-name terminated array of css_computed_counter items. */
static inline css_error css__copy_computed_counter_array(
		bool ref,
		const css_computed_counter *orig,
		css_computed_counter **copy_out)
{
	size_t count = 0;
	css_computed_counter *copy = NULL;

	if (orig != NULL) {
		for (const css_computed_counter *i = orig;
				i->name != NULL; i++) {
			count++;
		}

		copy = malloc((count + 1) * sizeof(*copy));
		if (copy == NULL) {
			return CSS_NOMEM;
		}

		if (ref) {
			for (size_t i = 0; i < count; i++) {
				copy[i].name = lwc_string_ref(orig[i].name);
				copy[i].value = orig[i].value;
			}
			copy[count].name = NULL;
			copy[count].value = 0;
		} else {
			memcpy(copy, orig, (count + 1) * sizeof(*copy));
		}
	}

	*copy_out = copy;
	return CSS_OK;
}

/** Copy type:none terminated array of css_computed_content_item items. */
static inline css_error css__copy_computed_content_item_array(
		bool ref,
		const css_computed_content_item *orig,
		css_computed_content_item **copy_out)
{
	size_t count = 0;
	css_computed_content_item *copy = NULL;

	if (orig != NULL) {
		for (const css_computed_content_item *i = orig;
				i->type != CSS_COMPUTED_CONTENT_NONE; i++) {
			count++;
		}

		copy = malloc((count + 1) * sizeof(*copy));
		if (copy == NULL) {
			return CSS_NOMEM;
		}

		if (ref) {
			for (size_t i = 0; i < count; i++) {
				switch (orig[i].type) {
				case CSS_COMPUTED_CONTENT_STRING:
					copy[i].data.string = lwc_string_ref(
							orig[i].data.string);
					break;
				case CSS_COMPUTED_CONTENT_URI:
					copy[i].data.uri = lwc_string_ref(
							orig[i].data.uri);
					break;
				case CSS_COMPUTED_CONTENT_ATTR:
					copy[i].data.attr = lwc_string_ref(
							orig[i].data.attr);
					break;
				case CSS_COMPUTED_CONTENT_COUNTER:
					copy[i].data.counter.name = lwc_string_ref(
							orig[i].data.counter.name);
					copy[i].data.counter.style =
							orig[i].data.counter.style;
					break;
				case CSS_COMPUTED_CONTENT_COUNTERS:
					copy[i].data.counters.name = lwc_string_ref(
							orig[i].data.counters.name);
					copy[i].data.counters.sep = lwc_string_ref(
							orig[i].data.counters.sep);
					copy[i].data.counters.style =
							orig[i].data.counters.style;
					break;
				default:
					break;
				}
				copy[i].type = orig[i].type;
			}
			copy[count].type = CSS_COMPUTED_CONTENT_NONE;
		} else {
			memcpy(copy, orig, (count + 1) * sizeof(*copy));
		}
	}

	*copy_out = copy;
	return CSS_OK;
}

#endif
