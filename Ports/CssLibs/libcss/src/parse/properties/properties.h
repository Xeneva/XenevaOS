/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef css_css__parse_properties_properties_h_
#define css_css__parse_properties_properties_h_

#include "stylesheet.h"
#include "lex/lex.h"
#include "parse/language.h"
#include "parse/propstrings.h"

/**
 * Type of property handler function
 */
typedef css_error (*css_prop_handler)(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);

extern const css_prop_handler property_handlers[LAST_PROP + 1 - FIRST_PROP];

css_error css__parse_align_content(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_align_items(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_align_self(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_azimuth(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_background(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_background_attachment(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_background_color(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_background_image(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_background_position(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_background_repeat(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_bottom(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_bottom_color(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_bottom_style(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_bottom_width(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_color(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_collapse(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_left(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_left_color(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_left_style(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_left_width(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_right(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_right_color(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_right_style(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_right_width(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_spacing(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_style(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_top(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_top_color(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_top_style(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_top_width(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_border_width(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_bottom(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_box_sizing(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_break_after(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_break_before(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_break_inside(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_caption_side(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_clear(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_clip(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_color(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_columns(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_column_count(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_column_fill(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_column_gap(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_column_rule(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_column_rule_color(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_column_rule_style(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_column_rule_width(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_column_span(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_column_width(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_content(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_counter_increment(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_counter_reset(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_cue(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_cue_after(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_cue_before(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_cursor(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_direction(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_display(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_elevation(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_empty_cells(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_fill_opacity(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_flex(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_flex_basis(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_flex_direction(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_flex_flow(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_flex_grow(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_flex_shrink(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_flex_wrap(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_float(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_font(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_font_family(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_font_size(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_font_style(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_font_variant(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_font_weight(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_height(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_justify_content(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_left(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_letter_spacing(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_line_height(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_list_style(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_list_style_image(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_list_style_position(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_list_style_type(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_margin(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_margin_bottom(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_margin_left(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_margin_right(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_margin_top(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_max_height(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_max_width(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_min_height(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_min_width(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_opacity(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_order(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_orphans(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_outline(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_outline_color(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_outline_style(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_outline_width(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_overflow(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_overflow_x(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_overflow_y(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_padding(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_padding_bottom(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_padding_left(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_padding_right(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_padding_top(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_page_break_after(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_page_break_before(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_page_break_inside(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_pause(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_pause_after(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_pause_before(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_pitch_range(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_pitch(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_play_during(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_position(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_quotes(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_richness(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_right(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_speak_header(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_speak_numeral(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_speak_punctuation(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_speak(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_speech_rate(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_stress(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_stroke_opacity(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_table_layout(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_text_align(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_text_decoration(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_text_indent(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_text_transform(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_top(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_unicode_bidi(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_vertical_align(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_visibility(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_voice_family(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_volume(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_white_space(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_widows(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_width(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_word_spacing(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_writing_mode(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);
css_error css__parse_z_index(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result);

/** Mapping from property bytecode index to bytecode unit class mask. */
extern const uint32_t property_unit_mask[CSS_N_PROPERTIES];

#define UNIT_MASK_AZIMUTH               (UNIT_ANGLE)
#define UNIT_MASK_BACKGROUND_ATTACHMENT (0)
#define UNIT_MASK_BACKGROUND_COLOR      (0)
#define UNIT_MASK_BACKGROUND_IMAGE      (0)
#define UNIT_MASK_BACKGROUND_POSITION   (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_BACKGROUND_REPEAT     (0)
#define UNIT_MASK_BORDER_COLLAPSE       (0)
#define UNIT_MASK_BORDER_SPACING        (UNIT_LENGTH)
#define UNIT_MASK_BORDER_SIDE_COLOR     (0)
#define UNIT_MASK_BORDER_SIDE_STYLE     (0)
#define UNIT_MASK_BORDER_SIDE_WIDTH     (UNIT_LENGTH)
#define UNIT_MASK_BOTTOM                (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_CAPTION_SIDE          (0)
#define UNIT_MASK_CLEAR                 (0)
#define UNIT_MASK_CLIP                  (UNIT_LENGTH)
#define UNIT_MASK_COLOR                 (0)
#define UNIT_MASK_CONTENT               (0)
#define UNIT_MASK_COUNTER_INCREMENT     (0)
#define UNIT_MASK_COUNTER_RESET         (0)
#define UNIT_MASK_CUE_AFTER             (0)
#define UNIT_MASK_CUE_BEFORE            (0)
#define UNIT_MASK_CURSOR                (0)
#define UNIT_MASK_DIRECTION             (0)
#define UNIT_MASK_DISPLAY               (0)
#define UNIT_MASK_ELEVATION             (UNIT_ANGLE)
#define UNIT_MASK_EMPTY_CELLS           (0)
#define UNIT_MASK_FLOAT                 (0)
#define UNIT_MASK_FONT_FAMILY           (0)
#define UNIT_MASK_FONT_SIZE             (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_FONT_STYLE            (0)
#define UNIT_MASK_FONT_VARIANT          (0)
#define UNIT_MASK_FONT_WEIGHT           (0)
#define UNIT_MASK_HEIGHT                (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_LEFT                  (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_LETTER_SPACING        (UNIT_LENGTH)
#define UNIT_MASK_LINE_HEIGHT           (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_LIST_STYLE_IMAGE      (0)
#define UNIT_MASK_LIST_STYLE_POSITION   (0)
#define UNIT_MASK_LIST_STYLE_TYPE       (0)
#define UNIT_MASK_MARGIN_SIDE           (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_MAX_HEIGHT            (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_MAX_WIDTH             (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_MIN_HEIGHT            (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_MIN_WIDTH             (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_ORPHANS               (0)
#define UNIT_MASK_OUTLINE_COLOR         (0)
#define UNIT_MASK_OUTLINE_STYLE         (0)
#define UNIT_MASK_OUTLINE_WIDTH         (UNIT_LENGTH)
#define UNIT_MASK_OVERFLOW_X            (0)
#define UNIT_MASK_PADDING_SIDE          (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_PAGE_BREAK_AFTER      (0)
#define UNIT_MASK_PAGE_BREAK_BEFORE     (0)
#define UNIT_MASK_PAGE_BREAK_INSIDE     (0)
#define UNIT_MASK_PAUSE_AFTER           (UNIT_TIME | UNIT_PCT)
#define UNIT_MASK_PAUSE_BEFORE          (UNIT_TIME | UNIT_PCT)
#define UNIT_MASK_PITCH_RANGE           (0)
#define UNIT_MASK_PITCH                 (UNIT_FREQ)
#define UNIT_MASK_PLAY_DURING           (0)
#define UNIT_MASK_POSITION              (0)
#define UNIT_MASK_QUOTES                (0)
#define UNIT_MASK_RICHNESS              (0)
#define UNIT_MASK_RIGHT                 (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_SPEAK_HEADER          (0)
#define UNIT_MASK_SPEAK_NUMERAL         (0)
#define UNIT_MASK_SPEAK_PUNCTUATION     (0)
#define UNIT_MASK_SPEAK                 (0)
#define UNIT_MASK_SPEECH_RATE           (0)
#define UNIT_MASK_STRESS                (0)
#define UNIT_MASK_TABLE_LAYOUT          (0)
#define UNIT_MASK_TEXT_ALIGN            (0)
#define UNIT_MASK_TEXT_DECORATION       (0)
#define UNIT_MASK_TEXT_INDENT           (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_TEXT_TRANSFORM        (0)
#define UNIT_MASK_TOP                   (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_UNICODE_BIDI          (0)
#define UNIT_MASK_VERTICAL_ALIGN        (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_VISIBILITY            (0)
#define UNIT_MASK_VOICE_FAMILY          (0)
#define UNIT_MASK_VOLUME                (UNIT_PCT)
#define UNIT_MASK_WHITE_SPACE           (0)
#define UNIT_MASK_WIDOWS                (0)
#define UNIT_MASK_WIDTH                 (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_WORD_SPACING          (UNIT_LENGTH)
#define UNIT_MASK_Z_INDEX               (0)
#define UNIT_MASK_OPACITY               (0)
#define UNIT_MASK_FILL_OPACITY          (0)
#define UNIT_MASK_STROKE_OPACITY        (0)
#define UNIT_MASK_BREAK_AFTER           (0)
#define UNIT_MASK_BREAK_BEFORE          (0)
#define UNIT_MASK_BREAK_INSIDE          (0)
#define UNIT_MASK_COLUMN_COUNT          (0)
#define UNIT_MASK_COLUMN_FILL           (0)
#define UNIT_MASK_COLUMN_GAP            (UNIT_LENGTH)
#define UNIT_MASK_COLUMN_RULE_COLOR     (0)
#define UNIT_MASK_COLUMN_RULE_STYLE     (0)
#define UNIT_MASK_COLUMN_RULE_WIDTH     (UNIT_LENGTH)
#define UNIT_MASK_COLUMN_SPAN           (0)
#define UNIT_MASK_COLUMN_WIDTH          (UNIT_LENGTH)
#define UNIT_MASK_WRITING_MODE          (0)
#define UNIT_MASK_OVERFLOW_Y            (0)
#define UNIT_MASK_BOX_SIZING            (0)
#define UNIT_MASK_ALIGN_CONTENT         (0)
#define UNIT_MASK_ALIGN_ITEMS           (0)
#define UNIT_MASK_ALIGN_SELF            (0)
#define UNIT_MASK_FLEX_BASIS            (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_FLEX_DIRECTION        (0)
#define UNIT_MASK_FLEX_GROW             (0)
#define UNIT_MASK_FLEX_SHRINK           (0)
#define UNIT_MASK_FLEX_WRAP             (0)
#define UNIT_MASK_JUSTIFY_CONTENT       (0)
#define UNIT_MASK_ORDER                 (0)

#endif
