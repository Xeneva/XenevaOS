# This file is part of LibCSS.
# Licensed under the MIT License,
# http://www.opensource.org/licenses/mit-license.php
# Copyright 2017 Lucas Neves <lcneves@gmail.com>

# Configuration of CSS values.
# The tuples in this set will be unpacked as arguments to the CSSValue
# class.
# Args: see docstring for class CSSValue in select_generator.py.
values = {
    ('length', 'css_fixed', 4, '0',
        'unit', 'css_unit', 5, 'CSS_UNIT_PX'),
    ('integer', 'int32_t', 4, '0'),
    ('fixed', 'css_fixed', 4, '0'),
    ('color', 'css_color', 4, '0'),
    ('string', 'lwc_string*'),
    ('string_arr', 'lwc_string**'),
    ('counter_arr', 'css_computed_counter*'),
    ('content_item', 'css_computed_content_item*')
}

# Configuration of property groups.
# The tuples in these sets will be unpacked as arguments to the
# CSSproperty class.
# Args: see docstring for class CSSProperty in select_generator.py.
style = {
    # Style group, only opcode
    ('align_content', 3),
    ('align_items', 3),
    ('align_self', 3),
    ('background_attachment', 2),
    ('background_repeat', 3),
    ('border_collapse', 2),
    ('border_top_style', 4),
    ('border_right_style', 4),
    ('border_bottom_style', 4),
    ('border_left_style', 4),
    ('box_sizing', 2),
    ('caption_side', 2),
    ('clear', 3),
    ('direction', 2),
    ('display', 5),
    ('empty_cells', 2),
    ('flex_direction', 3),
    ('flex_wrap', 2),
    ('float', 2),
    ('font_style', 2),
    ('font_variant', 2),
    ('font_weight', 4),
    ('justify_content', 3),
    ('list_style_position', 2),
    ('list_style_type', 6),
    ('overflow_x', 3),
    ('overflow_y', 3),
    ('outline_style', 4),
    ('position', 3),
    ('table_layout', 2),
    ('text_align', 4),
    ('text_decoration', 5),
    ('text_transform', 3),
    ('unicode_bidi', 2),
    ('visibility', 2),
    ('white_space', 3),
    # Style group, with additional value
    ('background_color', 2, 'color'),
    ('background_image', 1, 'string'),
    ('background_position', 1, (('length',), ('length',)),
        'CSS_BACKGROUND_POSITION_SET'),
    ('border_top_color', 2, 'color'),
    ('border_right_color', 2, 'color'),
    ('border_bottom_color', 2, 'color'),
    ('border_left_color', 2, 'color'),
    ('border_top_width', 3, 'length', 'CSS_BORDER_WIDTH_WIDTH'),
    ('border_right_width', 3, 'length', 'CSS_BORDER_WIDTH_WIDTH'),
    ('border_bottom_width', 3, 'length', 'CSS_BORDER_WIDTH_WIDTH'),
    ('border_left_width', 3, 'length', 'CSS_BORDER_WIDTH_WIDTH'),
    ('top', 2, 'length', 'CSS_TOP_SET', None, None, 'get'),
    ('right', 2, 'length', 'CSS_RIGHT_SET', None, None, 'get'),
    ('bottom', 2, 'length', 'CSS_BOTTOM_SET', None, None, 'get'),
    ('left', 2, 'length', 'CSS_LEFT_SET', None, None, 'get'),
    ('color', 1, 'color'),
    ('flex_basis', 2, 'length', 'CSS_FLEX_BASIS_SET'),
    ('flex_grow', 1, 'fixed', 'CSS_FLEX_GROW_SET'),
    ('flex_shrink', 1, 'fixed', 'CSS_FLEX_SHRINK_SET'),
    ('font_size', 4, 'length', 'CSS_FONT_SIZE_DIMENSION'),
    ('height', 2, 'length', 'CSS_HEIGHT_SET'),
    ('line_height', 2, 'length', None, None, None, 'get'),
    ('list_style_image', 1, 'string'),
    ('margin_top', 2, 'length', 'CSS_MARGIN_SET'),
    ('margin_right', 2, 'length', 'CSS_MARGIN_SET'),
    ('margin_bottom', 2, 'length', 'CSS_MARGIN_SET'),
    ('margin_left', 2, 'length', 'CSS_MARGIN_SET'),
    ('max_height', 2, 'length', 'CSS_MAX_HEIGHT_SET'),
    ('max_width', 2, 'length', 'CSS_MAX_WIDTH_SET'),
    ('min_height', 2, 'length', 'CSS_MIN_HEIGHT_SET'),
    ('min_width', 2, 'length', 'CSS_MIN_WIDTH_SET'),
    ('opacity', 1, 'fixed', 'CSS_OPACITY_SET'),
    ('fill_opacity', 1, 'fixed', 'CSS_FILL_OPACITY_SET'),
    ('stroke_opacity', 1, 'fixed', 'CSS_STROKE_OPACITY_SET'),
    ('order', 1, 'integer', 'CSS_ORDER_SET'),
    ('padding_top', 1, 'length', 'CSS_PADDING_SET'),
    ('padding_right', 1, 'length', 'CSS_PADDING_SET'),
    ('padding_left', 1, 'length', 'CSS_PADDING_SET'),
    ('padding_bottom', 1, 'length', 'CSS_PADDING_SET'),
    ('text_indent', 1, 'length', 'CSS_TEXT_INDENT_SET'),
    ('vertical_align', 4, 'length', 'CSS_VERTICAL_ALIGN_SET'),
    ('width', 2, 'length', 'CSS_WIDTH_SET'),
    ('z_index', 2, 'integer'),
    # Style group, arrays
    ('font_family', 3, 'string_arr', None, None,
        'Encode font family as an array of string objects, terminated with a '
        'blank entry.'),
    ('quotes', 1, 'string_arr', None, None,
        'Encode quotes as an array of string objects, terminated with a '
        'blank entry.'),
    # Page group
    ('page_break_after', 3, None, None, 'CSS_PAGE_BREAK_AFTER_AUTO'),
    ('page_break_before', 3, None, None, 'CSS_PAGE_BREAK_BEFORE_AUTO'),
    ('page_break_inside', 2, None, None, 'CSS_PAGE_BREAK_INSIDE_AUTO'),
    ('widows', 1, (('integer', '2'),), None,
        'CSS_WIDOWS_SET'),
    ('orphans', 1, (('integer', '2'),), None,
        'CSS_ORPHANS_SET'),
    # Uncommon group
    ('border_spacing', 1, (('length',), ('length',)), 'CSS_BORDER_SPACING_SET',
        'CSS_BORDER_SPACING_SET'),
    ('break_after', 4, None, None, 'CSS_BREAK_AFTER_AUTO'),
    ('break_before', 4, None, None, 'CSS_BREAK_BEFORE_AUTO'),
    ('break_inside', 4, None, None, 'CSS_BREAK_INSIDE_AUTO'),
    ('clip', 6, (('length',), ('length',), ('length',), ('length',)),
        'CSS_CLIP_RECT', 'CSS_CLIP_AUTO', None, ('get', 'set')),
    ('column_count', 2, 'integer', None, 'CSS_COLUMN_COUNT_AUTO'),
    ('column_fill', 2, None, None, 'CSS_COLUMN_FILL_BALANCE'),
    ('column_gap', 2, 'length',
        'CSS_COLUMN_GAP_SET', 'CSS_COLUMN_GAP_NORMAL'),
    ('column_rule_color', 2, 'color', None,
        'CSS_COLUMN_RULE_COLOR_CURRENT_COLOR'),
    ('column_rule_style', 4, None, None, 'CSS_COLUMN_RULE_STYLE_NONE'),
    ('column_rule_width', 3, 'length',
        'CSS_COLUMN_RULE_WIDTH_WIDTH', 'CSS_COLUMN_RULE_WIDTH_MEDIUM'),
    ('column_span', 2, None, None, 'CSS_COLUMN_SPAN_NONE'),
    ('column_width', 2, 'length',
        'CSS_COLUMN_WIDTH_SET', 'CSS_COLUMN_WIDTH_AUTO'),
    ('letter_spacing', 2, 'length',
        'CSS_LETTER_SPACING_SET', 'CSS_LETTER_SPACING_NORMAL'),
    ('outline_color', 2, 'color',
        'CSS_OUTLINE_COLOR_COLOR', 'CSS_OUTLINE_COLOR_INVERT'),
    ('outline_width', 3, 'length',
        'CSS_OUTLINE_WIDTH_WIDTH', 'CSS_OUTLINE_WIDTH_MEDIUM'),
    ('word_spacing', 2, 'length',
        'CSS_WORD_SPACING_SET', 'CSS_WORD_SPACING_NORMAL'),
    ('writing_mode', 2, None, None, 'CSS_WRITING_MODE_HORIZONTAL_TB'),
    # Uncommon group, arrays
    ('counter_increment', 1, 'counter_arr', None, 'CSS_COUNTER_INCREMENT_NONE',
        'Encode counter_increment as an array of name, value pairs, '
        'terminated with a blank entry.'),
    ('counter_reset', 1, 'counter_arr', None, 'CSS_COUNTER_RESET_NONE',
        'Encode counter_reset as an array of name, value pairs, '
        'terminated with a blank entry.'),
    ('cursor', 5, 'string_arr', None, 'CSS_CURSOR_AUTO',
        'Encode cursor uri(s) as an array of string objects, terminated '
        'with a blank entry'),
    ('content', 2, 'content_item', 'CSS_CONTENT_SET', 'CSS_CONTENT_NORMAL',
        'Encode content as an array of content items, terminated with '
        'a blank entry.', 'set')
}

groups = [
    { 'name': 'style', 'props': style }
]
