/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2017 The NetSurf Project
 */


#define ALIGN_CONTENT_INDEX 10
#define ALIGN_CONTENT_SHIFT 20
#define ALIGN_CONTENT_MASK 0x700000
static inline uint8_t get_align_content_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[ALIGN_CONTENT_INDEX];
	bits &= ALIGN_CONTENT_MASK;
	bits >>= ALIGN_CONTENT_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_align_content(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[ALIGN_CONTENT_INDEX];
	bits &= ALIGN_CONTENT_MASK;
	bits >>= ALIGN_CONTENT_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef ALIGN_CONTENT_INDEX
#undef ALIGN_CONTENT_SHIFT
#undef ALIGN_CONTENT_MASK

#define ALIGN_ITEMS_INDEX 10
#define ALIGN_ITEMS_SHIFT 23
#define ALIGN_ITEMS_MASK 0x3800000
static inline uint8_t get_align_items_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[ALIGN_ITEMS_INDEX];
	bits &= ALIGN_ITEMS_MASK;
	bits >>= ALIGN_ITEMS_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_align_items(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[ALIGN_ITEMS_INDEX];
	bits &= ALIGN_ITEMS_MASK;
	bits >>= ALIGN_ITEMS_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef ALIGN_ITEMS_INDEX
#undef ALIGN_ITEMS_SHIFT
#undef ALIGN_ITEMS_MASK

#define ALIGN_SELF_INDEX 10
#define ALIGN_SELF_SHIFT 26
#define ALIGN_SELF_MASK 0x1c000000
static inline uint8_t get_align_self_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[ALIGN_SELF_INDEX];
	bits &= ALIGN_SELF_MASK;
	bits >>= ALIGN_SELF_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_align_self(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[ALIGN_SELF_INDEX];
	bits &= ALIGN_SELF_MASK;
	bits >>= ALIGN_SELF_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef ALIGN_SELF_INDEX
#undef ALIGN_SELF_SHIFT
#undef ALIGN_SELF_MASK

#define BACKGROUND_ATTACHMENT_INDEX 14
#define BACKGROUND_ATTACHMENT_SHIFT 28
#define BACKGROUND_ATTACHMENT_MASK 0x30000000
static inline uint8_t get_background_attachment_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[BACKGROUND_ATTACHMENT_INDEX];
	bits &= BACKGROUND_ATTACHMENT_MASK;
	bits >>= BACKGROUND_ATTACHMENT_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_background_attachment(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BACKGROUND_ATTACHMENT_INDEX];
	bits &= BACKGROUND_ATTACHMENT_MASK;
	bits >>= BACKGROUND_ATTACHMENT_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef BACKGROUND_ATTACHMENT_INDEX
#undef BACKGROUND_ATTACHMENT_SHIFT
#undef BACKGROUND_ATTACHMENT_MASK

#define BACKGROUND_COLOR_INDEX 14
#define BACKGROUND_COLOR_SHIFT 30
#define BACKGROUND_COLOR_MASK 0xc0000000
static inline uint8_t get_background_color_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BACKGROUND_COLOR_INDEX];
	bits &= BACKGROUND_COLOR_MASK;
	bits >>= BACKGROUND_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_background_color(const css_computed_style *style,
		css_color *color)
{
	uint32_t bits = style->i.bits[BACKGROUND_COLOR_INDEX];
	bits &= BACKGROUND_COLOR_MASK;
	bits >>= BACKGROUND_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	*color = style->i.background_color;
	
	return (bits & 0x3);
}
#undef BACKGROUND_COLOR_INDEX
#undef BACKGROUND_COLOR_SHIFT
#undef BACKGROUND_COLOR_MASK

#define BACKGROUND_IMAGE_INDEX 14
#define BACKGROUND_IMAGE_SHIFT 16
#define BACKGROUND_IMAGE_MASK 0x10000
static inline uint8_t get_background_image_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BACKGROUND_IMAGE_INDEX];
	bits &= BACKGROUND_IMAGE_MASK;
	bits >>= BACKGROUND_IMAGE_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_background_image(const css_computed_style *style,
		lwc_string **string)
{
	uint32_t bits = style->i.bits[BACKGROUND_IMAGE_INDEX];
	bits &= BACKGROUND_IMAGE_MASK;
	bits >>= BACKGROUND_IMAGE_SHIFT;
	
	/* 1bit: t : type */
	*string = style->i.background_image;
	
	return (bits & 0x1);
}
#undef BACKGROUND_IMAGE_INDEX
#undef BACKGROUND_IMAGE_SHIFT
#undef BACKGROUND_IMAGE_MASK

#define BACKGROUND_POSITION_INDEX 12
#define BACKGROUND_POSITION_SHIFT 10
#define BACKGROUND_POSITION_MASK 0x1ffc00
static inline uint8_t get_background_position_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[BACKGROUND_POSITION_INDEX];
	bits &= BACKGROUND_POSITION_MASK;
	bits >>= BACKGROUND_POSITION_SHIFT;
	
	/* 11bits: aaaaabbbbbt : unit_a | unit_b | type */
	return (bits & 0x1);
}
static inline uint8_t get_background_position(const css_computed_style *style,
		css_fixed *length_a, css_unit *unit_a, css_fixed *length_b,
		css_unit *unit_b)
{
	uint32_t bits = style->i.bits[BACKGROUND_POSITION_INDEX];
	bits &= BACKGROUND_POSITION_MASK;
	bits >>= BACKGROUND_POSITION_SHIFT;
	
	/* 11bits: aaaaabbbbbt : unit_a | unit_b | type */
	if ((bits & 0x1) == CSS_BACKGROUND_POSITION_SET) {
		*length_a = style->i.background_position_a;
		*length_b = style->i.background_position_b;
		*unit_a = bits >> 6;
		*unit_b = (bits & 0x3e) >> 1;
	}
	
	return (bits & 0x1);
}
#undef BACKGROUND_POSITION_INDEX
#undef BACKGROUND_POSITION_SHIFT
#undef BACKGROUND_POSITION_MASK

#define BACKGROUND_REPEAT_INDEX 10
#define BACKGROUND_REPEAT_SHIFT 29
#define BACKGROUND_REPEAT_MASK 0xe0000000
static inline uint8_t get_background_repeat_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[BACKGROUND_REPEAT_INDEX];
	bits &= BACKGROUND_REPEAT_MASK;
	bits >>= BACKGROUND_REPEAT_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_background_repeat(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BACKGROUND_REPEAT_INDEX];
	bits &= BACKGROUND_REPEAT_MASK;
	bits >>= BACKGROUND_REPEAT_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef BACKGROUND_REPEAT_INDEX
#undef BACKGROUND_REPEAT_SHIFT
#undef BACKGROUND_REPEAT_MASK

#define BORDER_BOTTOM_COLOR_INDEX 11
#define BORDER_BOTTOM_COLOR_SHIFT 0
#define BORDER_BOTTOM_COLOR_MASK 0x3
static inline uint8_t get_border_bottom_color_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[BORDER_BOTTOM_COLOR_INDEX];
	bits &= BORDER_BOTTOM_COLOR_MASK;
	bits >>= BORDER_BOTTOM_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_border_bottom_color(const css_computed_style *style,
		css_color *color)
{
	uint32_t bits = style->i.bits[BORDER_BOTTOM_COLOR_INDEX];
	bits &= BORDER_BOTTOM_COLOR_MASK;
	bits >>= BORDER_BOTTOM_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	*color = style->i.border_bottom_color;
	
	return (bits & 0x3);
}
#undef BORDER_BOTTOM_COLOR_INDEX
#undef BORDER_BOTTOM_COLOR_SHIFT
#undef BORDER_BOTTOM_COLOR_MASK

#define BORDER_BOTTOM_STYLE_INDEX 13
#define BORDER_BOTTOM_STYLE_SHIFT 28
#define BORDER_BOTTOM_STYLE_MASK 0xf0000000
static inline uint8_t get_border_bottom_style_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[BORDER_BOTTOM_STYLE_INDEX];
	bits &= BORDER_BOTTOM_STYLE_MASK;
	bits >>= BORDER_BOTTOM_STYLE_SHIFT;
	
	/* 4bits: tttt : type */
	return (bits & 0xf);
}
static inline uint8_t get_border_bottom_style(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BORDER_BOTTOM_STYLE_INDEX];
	bits &= BORDER_BOTTOM_STYLE_MASK;
	bits >>= BORDER_BOTTOM_STYLE_SHIFT;
	
	/* 4bits: tttt : type */
	
	return (bits & 0xf);
}
#undef BORDER_BOTTOM_STYLE_INDEX
#undef BORDER_BOTTOM_STYLE_SHIFT
#undef BORDER_BOTTOM_STYLE_MASK

#define BORDER_BOTTOM_WIDTH_INDEX 0
#define BORDER_BOTTOM_WIDTH_SHIFT 0
#define BORDER_BOTTOM_WIDTH_MASK 0xff
static inline uint8_t get_border_bottom_width_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[BORDER_BOTTOM_WIDTH_INDEX];
	bits &= BORDER_BOTTOM_WIDTH_MASK;
	bits >>= BORDER_BOTTOM_WIDTH_SHIFT;
	
	/* 8bits: uuuuuttt : unit | type */
	return (bits & 0x7);
}
static inline uint8_t get_border_bottom_width(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[BORDER_BOTTOM_WIDTH_INDEX];
	bits &= BORDER_BOTTOM_WIDTH_MASK;
	bits >>= BORDER_BOTTOM_WIDTH_SHIFT;
	
	/* 8bits: uuuuuttt : unit | type */
	if ((bits & 0x7) == CSS_BORDER_WIDTH_WIDTH) {
		*length = style->i.border_bottom_width;
		*unit = bits >> 3;
	}
	
	return (bits & 0x7);
}
#undef BORDER_BOTTOM_WIDTH_INDEX
#undef BORDER_BOTTOM_WIDTH_SHIFT
#undef BORDER_BOTTOM_WIDTH_MASK

#define BORDER_COLLAPSE_INDEX 11
#define BORDER_COLLAPSE_SHIFT 2
#define BORDER_COLLAPSE_MASK 0xc
static inline uint8_t get_border_collapse_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BORDER_COLLAPSE_INDEX];
	bits &= BORDER_COLLAPSE_MASK;
	bits >>= BORDER_COLLAPSE_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_border_collapse(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BORDER_COLLAPSE_INDEX];
	bits &= BORDER_COLLAPSE_MASK;
	bits >>= BORDER_COLLAPSE_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef BORDER_COLLAPSE_INDEX
#undef BORDER_COLLAPSE_SHIFT
#undef BORDER_COLLAPSE_MASK

#define BORDER_LEFT_COLOR_INDEX 11
#define BORDER_LEFT_COLOR_SHIFT 4
#define BORDER_LEFT_COLOR_MASK 0x30
static inline uint8_t get_border_left_color_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[BORDER_LEFT_COLOR_INDEX];
	bits &= BORDER_LEFT_COLOR_MASK;
	bits >>= BORDER_LEFT_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_border_left_color(const css_computed_style *style,
		css_color *color)
{
	uint32_t bits = style->i.bits[BORDER_LEFT_COLOR_INDEX];
	bits &= BORDER_LEFT_COLOR_MASK;
	bits >>= BORDER_LEFT_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	*color = style->i.border_left_color;
	
	return (bits & 0x3);
}
#undef BORDER_LEFT_COLOR_INDEX
#undef BORDER_LEFT_COLOR_SHIFT
#undef BORDER_LEFT_COLOR_MASK

#define BORDER_LEFT_STYLE_INDEX 9
#define BORDER_LEFT_STYLE_SHIFT 3
#define BORDER_LEFT_STYLE_MASK 0x78
static inline uint8_t get_border_left_style_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[BORDER_LEFT_STYLE_INDEX];
	bits &= BORDER_LEFT_STYLE_MASK;
	bits >>= BORDER_LEFT_STYLE_SHIFT;
	
	/* 4bits: tttt : type */
	return (bits & 0xf);
}
static inline uint8_t get_border_left_style(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BORDER_LEFT_STYLE_INDEX];
	bits &= BORDER_LEFT_STYLE_MASK;
	bits >>= BORDER_LEFT_STYLE_SHIFT;
	
	/* 4bits: tttt : type */
	
	return (bits & 0xf);
}
#undef BORDER_LEFT_STYLE_INDEX
#undef BORDER_LEFT_STYLE_SHIFT
#undef BORDER_LEFT_STYLE_MASK

#define BORDER_LEFT_WIDTH_INDEX 0
#define BORDER_LEFT_WIDTH_SHIFT 8
#define BORDER_LEFT_WIDTH_MASK 0xff00
static inline uint8_t get_border_left_width_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[BORDER_LEFT_WIDTH_INDEX];
	bits &= BORDER_LEFT_WIDTH_MASK;
	bits >>= BORDER_LEFT_WIDTH_SHIFT;
	
	/* 8bits: uuuuuttt : unit | type */
	return (bits & 0x7);
}
static inline uint8_t get_border_left_width(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[BORDER_LEFT_WIDTH_INDEX];
	bits &= BORDER_LEFT_WIDTH_MASK;
	bits >>= BORDER_LEFT_WIDTH_SHIFT;
	
	/* 8bits: uuuuuttt : unit | type */
	if ((bits & 0x7) == CSS_BORDER_WIDTH_WIDTH) {
		*length = style->i.border_left_width;
		*unit = bits >> 3;
	}
	
	return (bits & 0x7);
}
#undef BORDER_LEFT_WIDTH_INDEX
#undef BORDER_LEFT_WIDTH_SHIFT
#undef BORDER_LEFT_WIDTH_MASK

#define BORDER_RIGHT_COLOR_INDEX 11
#define BORDER_RIGHT_COLOR_SHIFT 6
#define BORDER_RIGHT_COLOR_MASK 0xc0
static inline uint8_t get_border_right_color_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[BORDER_RIGHT_COLOR_INDEX];
	bits &= BORDER_RIGHT_COLOR_MASK;
	bits >>= BORDER_RIGHT_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_border_right_color(const css_computed_style *style,
		css_color *color)
{
	uint32_t bits = style->i.bits[BORDER_RIGHT_COLOR_INDEX];
	bits &= BORDER_RIGHT_COLOR_MASK;
	bits >>= BORDER_RIGHT_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	*color = style->i.border_right_color;
	
	return (bits & 0x3);
}
#undef BORDER_RIGHT_COLOR_INDEX
#undef BORDER_RIGHT_COLOR_SHIFT
#undef BORDER_RIGHT_COLOR_MASK

#define BORDER_RIGHT_STYLE_INDEX 9
#define BORDER_RIGHT_STYLE_SHIFT 7
#define BORDER_RIGHT_STYLE_MASK 0x780
static inline uint8_t get_border_right_style_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[BORDER_RIGHT_STYLE_INDEX];
	bits &= BORDER_RIGHT_STYLE_MASK;
	bits >>= BORDER_RIGHT_STYLE_SHIFT;
	
	/* 4bits: tttt : type */
	return (bits & 0xf);
}
static inline uint8_t get_border_right_style(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BORDER_RIGHT_STYLE_INDEX];
	bits &= BORDER_RIGHT_STYLE_MASK;
	bits >>= BORDER_RIGHT_STYLE_SHIFT;
	
	/* 4bits: tttt : type */
	
	return (bits & 0xf);
}
#undef BORDER_RIGHT_STYLE_INDEX
#undef BORDER_RIGHT_STYLE_SHIFT
#undef BORDER_RIGHT_STYLE_MASK

#define BORDER_RIGHT_WIDTH_INDEX 0
#define BORDER_RIGHT_WIDTH_SHIFT 16
#define BORDER_RIGHT_WIDTH_MASK 0xff0000
static inline uint8_t get_border_right_width_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[BORDER_RIGHT_WIDTH_INDEX];
	bits &= BORDER_RIGHT_WIDTH_MASK;
	bits >>= BORDER_RIGHT_WIDTH_SHIFT;
	
	/* 8bits: uuuuuttt : unit | type */
	return (bits & 0x7);
}
static inline uint8_t get_border_right_width(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[BORDER_RIGHT_WIDTH_INDEX];
	bits &= BORDER_RIGHT_WIDTH_MASK;
	bits >>= BORDER_RIGHT_WIDTH_SHIFT;
	
	/* 8bits: uuuuuttt : unit | type */
	if ((bits & 0x7) == CSS_BORDER_WIDTH_WIDTH) {
		*length = style->i.border_right_width;
		*unit = bits >> 3;
	}
	
	return (bits & 0x7);
}
#undef BORDER_RIGHT_WIDTH_INDEX
#undef BORDER_RIGHT_WIDTH_SHIFT
#undef BORDER_RIGHT_WIDTH_MASK

#define BORDER_SPACING_INDEX 12
#define BORDER_SPACING_SHIFT 21
#define BORDER_SPACING_MASK 0xffe00000
static inline uint8_t get_border_spacing_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BORDER_SPACING_INDEX];
	bits &= BORDER_SPACING_MASK;
	bits >>= BORDER_SPACING_SHIFT;
	
	/* 11bits: aaaaabbbbbt : unit_a | unit_b | type */
	return (bits & 0x1);
}
static inline uint8_t get_border_spacing(const css_computed_style *style,
		css_fixed *length_a, css_unit *unit_a, css_fixed *length_b,
		css_unit *unit_b)
{
	uint32_t bits = style->i.bits[BORDER_SPACING_INDEX];
	bits &= BORDER_SPACING_MASK;
	bits >>= BORDER_SPACING_SHIFT;
	
	/* 11bits: aaaaabbbbbt : unit_a | unit_b | type */
	if ((bits & 0x1) == CSS_BORDER_SPACING_SET) {
		*length_a = style->i.border_spacing_a;
		*length_b = style->i.border_spacing_b;
		*unit_a = bits >> 6;
		*unit_b = (bits & 0x3e) >> 1;
	}
	
	return (bits & 0x1);
}
#undef BORDER_SPACING_INDEX
#undef BORDER_SPACING_SHIFT
#undef BORDER_SPACING_MASK

#define BORDER_TOP_COLOR_INDEX 11
#define BORDER_TOP_COLOR_SHIFT 8
#define BORDER_TOP_COLOR_MASK 0x300
static inline uint8_t get_border_top_color_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BORDER_TOP_COLOR_INDEX];
	bits &= BORDER_TOP_COLOR_MASK;
	bits >>= BORDER_TOP_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_border_top_color(const css_computed_style *style,
		css_color *color)
{
	uint32_t bits = style->i.bits[BORDER_TOP_COLOR_INDEX];
	bits &= BORDER_TOP_COLOR_MASK;
	bits >>= BORDER_TOP_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	*color = style->i.border_top_color;
	
	return (bits & 0x3);
}
#undef BORDER_TOP_COLOR_INDEX
#undef BORDER_TOP_COLOR_SHIFT
#undef BORDER_TOP_COLOR_MASK

#define BORDER_TOP_STYLE_INDEX 9
#define BORDER_TOP_STYLE_SHIFT 11
#define BORDER_TOP_STYLE_MASK 0x7800
static inline uint8_t get_border_top_style_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BORDER_TOP_STYLE_INDEX];
	bits &= BORDER_TOP_STYLE_MASK;
	bits >>= BORDER_TOP_STYLE_SHIFT;
	
	/* 4bits: tttt : type */
	return (bits & 0xf);
}
static inline uint8_t get_border_top_style(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BORDER_TOP_STYLE_INDEX];
	bits &= BORDER_TOP_STYLE_MASK;
	bits >>= BORDER_TOP_STYLE_SHIFT;
	
	/* 4bits: tttt : type */
	
	return (bits & 0xf);
}
#undef BORDER_TOP_STYLE_INDEX
#undef BORDER_TOP_STYLE_SHIFT
#undef BORDER_TOP_STYLE_MASK

#define BORDER_TOP_WIDTH_INDEX 0
#define BORDER_TOP_WIDTH_SHIFT 24
#define BORDER_TOP_WIDTH_MASK 0xff000000
static inline uint8_t get_border_top_width_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BORDER_TOP_WIDTH_INDEX];
	bits &= BORDER_TOP_WIDTH_MASK;
	bits >>= BORDER_TOP_WIDTH_SHIFT;
	
	/* 8bits: uuuuuttt : unit | type */
	return (bits & 0x7);
}
static inline uint8_t get_border_top_width(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[BORDER_TOP_WIDTH_INDEX];
	bits &= BORDER_TOP_WIDTH_MASK;
	bits >>= BORDER_TOP_WIDTH_SHIFT;
	
	/* 8bits: uuuuuttt : unit | type */
	if ((bits & 0x7) == CSS_BORDER_WIDTH_WIDTH) {
		*length = style->i.border_top_width;
		*unit = bits >> 3;
	}
	
	return (bits & 0x7);
}
#undef BORDER_TOP_WIDTH_INDEX
#undef BORDER_TOP_WIDTH_SHIFT
#undef BORDER_TOP_WIDTH_MASK

#define BOTTOM_INDEX 3
#define BOTTOM_SHIFT 11
#define BOTTOM_MASK 0x3f800
static inline uint8_t get_bottom_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BOTTOM_INDEX];
	bits &= BOTTOM_MASK;
	bits >>= BOTTOM_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_bottom(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[BOTTOM_INDEX];
	bits &= BOTTOM_MASK;
	bits >>= BOTTOM_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_BOTTOM_SET) {
		*length = style->i.bottom;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef BOTTOM_INDEX
#undef BOTTOM_SHIFT
#undef BOTTOM_MASK

#define BOX_SIZING_INDEX 11
#define BOX_SIZING_SHIFT 10
#define BOX_SIZING_MASK 0xc00
static inline uint8_t get_box_sizing_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BOX_SIZING_INDEX];
	bits &= BOX_SIZING_MASK;
	bits >>= BOX_SIZING_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_box_sizing(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BOX_SIZING_INDEX];
	bits &= BOX_SIZING_MASK;
	bits >>= BOX_SIZING_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef BOX_SIZING_INDEX
#undef BOX_SIZING_SHIFT
#undef BOX_SIZING_MASK

#define BREAK_AFTER_INDEX 9
#define BREAK_AFTER_SHIFT 15
#define BREAK_AFTER_MASK 0x78000
static inline uint8_t get_break_after_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BREAK_AFTER_INDEX];
	bits &= BREAK_AFTER_MASK;
	bits >>= BREAK_AFTER_SHIFT;
	
	/* 4bits: tttt : type */
	return (bits & 0xf);
}
static inline uint8_t get_break_after(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BREAK_AFTER_INDEX];
	bits &= BREAK_AFTER_MASK;
	bits >>= BREAK_AFTER_SHIFT;
	
	/* 4bits: tttt : type */
	
	return (bits & 0xf);
}
#undef BREAK_AFTER_INDEX
#undef BREAK_AFTER_SHIFT
#undef BREAK_AFTER_MASK

#define BREAK_BEFORE_INDEX 9
#define BREAK_BEFORE_SHIFT 19
#define BREAK_BEFORE_MASK 0x780000
static inline uint8_t get_break_before_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BREAK_BEFORE_INDEX];
	bits &= BREAK_BEFORE_MASK;
	bits >>= BREAK_BEFORE_SHIFT;
	
	/* 4bits: tttt : type */
	return (bits & 0xf);
}
static inline uint8_t get_break_before(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BREAK_BEFORE_INDEX];
	bits &= BREAK_BEFORE_MASK;
	bits >>= BREAK_BEFORE_SHIFT;
	
	/* 4bits: tttt : type */
	
	return (bits & 0xf);
}
#undef BREAK_BEFORE_INDEX
#undef BREAK_BEFORE_SHIFT
#undef BREAK_BEFORE_MASK

#define BREAK_INSIDE_INDEX 9
#define BREAK_INSIDE_SHIFT 23
#define BREAK_INSIDE_MASK 0x7800000
static inline uint8_t get_break_inside_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BREAK_INSIDE_INDEX];
	bits &= BREAK_INSIDE_MASK;
	bits >>= BREAK_INSIDE_SHIFT;
	
	/* 4bits: tttt : type */
	return (bits & 0xf);
}
static inline uint8_t get_break_inside(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[BREAK_INSIDE_INDEX];
	bits &= BREAK_INSIDE_MASK;
	bits >>= BREAK_INSIDE_SHIFT;
	
	/* 4bits: tttt : type */
	
	return (bits & 0xf);
}
#undef BREAK_INSIDE_INDEX
#undef BREAK_INSIDE_SHIFT
#undef BREAK_INSIDE_MASK

#define CAPTION_SIDE_INDEX 11
#define CAPTION_SIDE_SHIFT 12
#define CAPTION_SIDE_MASK 0x3000
static inline uint8_t get_caption_side_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[CAPTION_SIDE_INDEX];
	bits &= CAPTION_SIDE_MASK;
	bits >>= CAPTION_SIDE_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_caption_side(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[CAPTION_SIDE_INDEX];
	bits &= CAPTION_SIDE_MASK;
	bits >>= CAPTION_SIDE_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef CAPTION_SIDE_INDEX
#undef CAPTION_SIDE_SHIFT
#undef CAPTION_SIDE_MASK

#define CLEAR_INDEX 13
#define CLEAR_SHIFT 1
#define CLEAR_MASK 0xe
static inline uint8_t get_clear_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[CLEAR_INDEX];
	bits &= CLEAR_MASK;
	bits >>= CLEAR_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_clear(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[CLEAR_INDEX];
	bits &= CLEAR_MASK;
	bits >>= CLEAR_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef CLEAR_INDEX
#undef CLEAR_SHIFT
#undef CLEAR_MASK

#define CLIP_INDEX 2
#define CLIP_SHIFT 6
#define CLIP_MASK 0xffffffc0
static inline uint8_t get_clip_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[CLIP_INDEX];
	bits &= CLIP_MASK;
	bits >>= CLIP_SHIFT;
	
	/* 26bits: aaaaabbbbbcccccdddddtttttt : unit_a | unit_b | unit_c |
			unit_d | type */
	return (bits & 0x3f);
}
static inline uint8_t get_clip(
		const css_computed_style *style,
		css_computed_clip_rect *rect)
{
	uint32_t bits = style->i.bits[CLIP_INDEX];
	bits &= CLIP_MASK;
	bits >>= CLIP_SHIFT;

	/*
	26bits: tt tttr rrrr bbbb blll llTR BLyy:
	units: top | right | bottom | left
	opcodes: top | right | bottom | left | type
	*/

	if ((bits & 0x3) == CSS_CLIP_RECT) {
		rect->left_auto = (bits & 0x4);
		rect->bottom_auto = (bits & 0x8);
		rect->right_auto = (bits & 0x10);
		rect->top_auto = (bits & 0x20);

		rect->top = style->i.clip_a;
		rect->tunit = bits & 0x3e00000 >> 21;

		rect->right = style->i.clip_b;
		rect->runit = bits & 0x1f0000 >> 16;

		rect->bottom = style->i.clip_c;
		rect->bunit = (bits & 0xf800) >> 11;

		rect->left = style->i.clip_d;
		rect->lunit = (bits & 0x7c0) >> 6;
	}

	return (bits & 0x3);
}
#undef CLIP_INDEX
#undef CLIP_SHIFT
#undef CLIP_MASK

#define COLOR_INDEX 14
#define COLOR_SHIFT 17
#define COLOR_MASK 0x20000
static inline uint8_t get_color_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[COLOR_INDEX];
	bits &= COLOR_MASK;
	bits >>= COLOR_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_color(const css_computed_style *style, css_color
		*color)
{
	uint32_t bits = style->i.bits[COLOR_INDEX];
	bits &= COLOR_MASK;
	bits >>= COLOR_SHIFT;
	
	/* 1bit: t : type */
	*color = style->i.color;
	
	return (bits & 0x1);
}
#undef COLOR_INDEX
#undef COLOR_SHIFT
#undef COLOR_MASK

#define COLUMN_COUNT_INDEX 11
#define COLUMN_COUNT_SHIFT 14
#define COLUMN_COUNT_MASK 0xc000
static inline uint8_t get_column_count_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[COLUMN_COUNT_INDEX];
	bits &= COLUMN_COUNT_MASK;
	bits >>= COLUMN_COUNT_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_column_count(const css_computed_style *style, int32_t
		*integer)
{
	uint32_t bits = style->i.bits[COLUMN_COUNT_INDEX];
	bits &= COLUMN_COUNT_MASK;
	bits >>= COLUMN_COUNT_SHIFT;
	
	/* 2bits: tt : type */
	*integer = style->i.column_count;
	
	return (bits & 0x3);
}
#undef COLUMN_COUNT_INDEX
#undef COLUMN_COUNT_SHIFT
#undef COLUMN_COUNT_MASK

#define COLUMN_FILL_INDEX 11
#define COLUMN_FILL_SHIFT 16
#define COLUMN_FILL_MASK 0x30000
static inline uint8_t get_column_fill_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[COLUMN_FILL_INDEX];
	bits &= COLUMN_FILL_MASK;
	bits >>= COLUMN_FILL_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_column_fill(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[COLUMN_FILL_INDEX];
	bits &= COLUMN_FILL_MASK;
	bits >>= COLUMN_FILL_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef COLUMN_FILL_INDEX
#undef COLUMN_FILL_SHIFT
#undef COLUMN_FILL_MASK

#define COLUMN_GAP_INDEX 3
#define COLUMN_GAP_SHIFT 18
#define COLUMN_GAP_MASK 0x1fc0000
static inline uint8_t get_column_gap_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[COLUMN_GAP_INDEX];
	bits &= COLUMN_GAP_MASK;
	bits >>= COLUMN_GAP_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_column_gap(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[COLUMN_GAP_INDEX];
	bits &= COLUMN_GAP_MASK;
	bits >>= COLUMN_GAP_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_COLUMN_GAP_SET) {
		*length = style->i.column_gap;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef COLUMN_GAP_INDEX
#undef COLUMN_GAP_SHIFT
#undef COLUMN_GAP_MASK

#define COLUMN_RULE_COLOR_INDEX 11
#define COLUMN_RULE_COLOR_SHIFT 18
#define COLUMN_RULE_COLOR_MASK 0xc0000
static inline uint8_t get_column_rule_color_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[COLUMN_RULE_COLOR_INDEX];
	bits &= COLUMN_RULE_COLOR_MASK;
	bits >>= COLUMN_RULE_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_column_rule_color(const css_computed_style *style,
		css_color *color)
{
	uint32_t bits = style->i.bits[COLUMN_RULE_COLOR_INDEX];
	bits &= COLUMN_RULE_COLOR_MASK;
	bits >>= COLUMN_RULE_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	*color = style->i.column_rule_color;
	
	return (bits & 0x3);
}
#undef COLUMN_RULE_COLOR_INDEX
#undef COLUMN_RULE_COLOR_SHIFT
#undef COLUMN_RULE_COLOR_MASK

#define COLUMN_RULE_STYLE_INDEX 7
#define COLUMN_RULE_STYLE_SHIFT 0
#define COLUMN_RULE_STYLE_MASK 0xf
static inline uint8_t get_column_rule_style_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[COLUMN_RULE_STYLE_INDEX];
	bits &= COLUMN_RULE_STYLE_MASK;
	bits >>= COLUMN_RULE_STYLE_SHIFT;
	
	/* 4bits: tttt : type */
	return (bits & 0xf);
}
static inline uint8_t get_column_rule_style(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[COLUMN_RULE_STYLE_INDEX];
	bits &= COLUMN_RULE_STYLE_MASK;
	bits >>= COLUMN_RULE_STYLE_SHIFT;
	
	/* 4bits: tttt : type */
	
	return (bits & 0xf);
}
#undef COLUMN_RULE_STYLE_INDEX
#undef COLUMN_RULE_STYLE_SHIFT
#undef COLUMN_RULE_STYLE_MASK

#define COLUMN_RULE_WIDTH_INDEX 1
#define COLUMN_RULE_WIDTH_SHIFT 7
#define COLUMN_RULE_WIDTH_MASK 0x7f80
static inline uint8_t get_column_rule_width_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[COLUMN_RULE_WIDTH_INDEX];
	bits &= COLUMN_RULE_WIDTH_MASK;
	bits >>= COLUMN_RULE_WIDTH_SHIFT;
	
	/* 8bits: uuuuuttt : unit | type */
	return (bits & 0x7);
}
static inline uint8_t get_column_rule_width(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[COLUMN_RULE_WIDTH_INDEX];
	bits &= COLUMN_RULE_WIDTH_MASK;
	bits >>= COLUMN_RULE_WIDTH_SHIFT;
	
	/* 8bits: uuuuuttt : unit | type */
	if ((bits & 0x7) == CSS_COLUMN_RULE_WIDTH_WIDTH) {
		*length = style->i.column_rule_width;
		*unit = bits >> 3;
	}
	
	return (bits & 0x7);
}
#undef COLUMN_RULE_WIDTH_INDEX
#undef COLUMN_RULE_WIDTH_SHIFT
#undef COLUMN_RULE_WIDTH_MASK

#define COLUMN_SPAN_INDEX 11
#define COLUMN_SPAN_SHIFT 20
#define COLUMN_SPAN_MASK 0x300000
static inline uint8_t get_column_span_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[COLUMN_SPAN_INDEX];
	bits &= COLUMN_SPAN_MASK;
	bits >>= COLUMN_SPAN_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_column_span(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[COLUMN_SPAN_INDEX];
	bits &= COLUMN_SPAN_MASK;
	bits >>= COLUMN_SPAN_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef COLUMN_SPAN_INDEX
#undef COLUMN_SPAN_SHIFT
#undef COLUMN_SPAN_MASK

#define COLUMN_WIDTH_INDEX 3
#define COLUMN_WIDTH_SHIFT 25
#define COLUMN_WIDTH_MASK 0xfe000000
static inline uint8_t get_column_width_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[COLUMN_WIDTH_INDEX];
	bits &= COLUMN_WIDTH_MASK;
	bits >>= COLUMN_WIDTH_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_column_width(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[COLUMN_WIDTH_INDEX];
	bits &= COLUMN_WIDTH_MASK;
	bits >>= COLUMN_WIDTH_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_COLUMN_WIDTH_SET) {
		*length = style->i.column_width;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef COLUMN_WIDTH_INDEX
#undef COLUMN_WIDTH_SHIFT
#undef COLUMN_WIDTH_MASK

#define CONTENT_INDEX 11
#define CONTENT_SHIFT 22
#define CONTENT_MASK 0xc00000
static inline uint8_t get_content_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[CONTENT_INDEX];
	bits &= CONTENT_MASK;
	bits >>= CONTENT_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_content(const css_computed_style *style, const
		css_computed_content_item **content_item)
{
	uint32_t bits = style->i.bits[CONTENT_INDEX];
	bits &= CONTENT_MASK;
	bits >>= CONTENT_SHIFT;
	
	/* 2bits: tt : type */
	if ((bits & 0x3) == CSS_CONTENT_SET) {
		*content_item = style->content;
	}
	
	return (bits & 0x3);
}
#undef CONTENT_INDEX
#undef CONTENT_SHIFT
#undef CONTENT_MASK

#define COUNTER_INCREMENT_INDEX 14
#define COUNTER_INCREMENT_SHIFT 18
#define COUNTER_INCREMENT_MASK 0x40000
static inline uint8_t get_counter_increment_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[COUNTER_INCREMENT_INDEX];
	bits &= COUNTER_INCREMENT_MASK;
	bits >>= COUNTER_INCREMENT_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_counter_increment(const css_computed_style *style,
		const css_computed_counter **counter_arr)
{
	uint32_t bits = style->i.bits[COUNTER_INCREMENT_INDEX];
	bits &= COUNTER_INCREMENT_MASK;
	bits >>= COUNTER_INCREMENT_SHIFT;
	
	/* 1bit: t : type */
	*counter_arr = style->counter_increment;
	
	return (bits & 0x1);
}
#undef COUNTER_INCREMENT_INDEX
#undef COUNTER_INCREMENT_SHIFT
#undef COUNTER_INCREMENT_MASK

#define COUNTER_RESET_INDEX 14
#define COUNTER_RESET_SHIFT 19
#define COUNTER_RESET_MASK 0x80000
static inline uint8_t get_counter_reset_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[COUNTER_RESET_INDEX];
	bits &= COUNTER_RESET_MASK;
	bits >>= COUNTER_RESET_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_counter_reset(const css_computed_style *style, const
		css_computed_counter **counter_arr)
{
	uint32_t bits = style->i.bits[COUNTER_RESET_INDEX];
	bits &= COUNTER_RESET_MASK;
	bits >>= COUNTER_RESET_SHIFT;
	
	/* 1bit: t : type */
	*counter_arr = style->counter_reset;
	
	return (bits & 0x1);
}
#undef COUNTER_RESET_INDEX
#undef COUNTER_RESET_SHIFT
#undef COUNTER_RESET_MASK

#define CURSOR_INDEX 9
#define CURSOR_SHIFT 27
#define CURSOR_MASK 0xf8000000
static inline uint8_t get_cursor_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[CURSOR_INDEX];
	bits &= CURSOR_MASK;
	bits >>= CURSOR_SHIFT;
	
	/* 5bits: ttttt : type */
	return (bits & 0x1f);
}
static inline uint8_t get_cursor(const css_computed_style *style, lwc_string
		***string_arr)
{
	uint32_t bits = style->i.bits[CURSOR_INDEX];
	bits &= CURSOR_MASK;
	bits >>= CURSOR_SHIFT;
	
	/* 5bits: ttttt : type */
	*string_arr = style->cursor;
	
	return (bits & 0x1f);
}
#undef CURSOR_INDEX
#undef CURSOR_SHIFT
#undef CURSOR_MASK

#define DIRECTION_INDEX 11
#define DIRECTION_SHIFT 24
#define DIRECTION_MASK 0x3000000
static inline uint8_t get_direction_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[DIRECTION_INDEX];
	bits &= DIRECTION_MASK;
	bits >>= DIRECTION_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_direction(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[DIRECTION_INDEX];
	bits &= DIRECTION_MASK;
	bits >>= DIRECTION_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef DIRECTION_INDEX
#undef DIRECTION_SHIFT
#undef DIRECTION_MASK

#define DISPLAY_INDEX 8
#define DISPLAY_SHIFT 3
#define DISPLAY_MASK 0xf8
static inline uint8_t get_display_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[DISPLAY_INDEX];
	bits &= DISPLAY_MASK;
	bits >>= DISPLAY_SHIFT;
	
	/* 5bits: ttttt : type */
	return (bits & 0x1f);
}
static inline uint8_t get_display(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[DISPLAY_INDEX];
	bits &= DISPLAY_MASK;
	bits >>= DISPLAY_SHIFT;
	
	/* 5bits: ttttt : type */
	
	return (bits & 0x1f);
}
#undef DISPLAY_INDEX
#undef DISPLAY_SHIFT
#undef DISPLAY_MASK

#define EMPTY_CELLS_INDEX 11
#define EMPTY_CELLS_SHIFT 26
#define EMPTY_CELLS_MASK 0xc000000
static inline uint8_t get_empty_cells_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[EMPTY_CELLS_INDEX];
	bits &= EMPTY_CELLS_MASK;
	bits >>= EMPTY_CELLS_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_empty_cells(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[EMPTY_CELLS_INDEX];
	bits &= EMPTY_CELLS_MASK;
	bits >>= EMPTY_CELLS_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef EMPTY_CELLS_INDEX
#undef EMPTY_CELLS_SHIFT
#undef EMPTY_CELLS_MASK

#define FILL_OPACITY_INDEX 14
#define FILL_OPACITY_SHIFT 20
#define FILL_OPACITY_MASK 0x100000
static inline uint8_t get_fill_opacity_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FILL_OPACITY_INDEX];
	bits &= FILL_OPACITY_MASK;
	bits >>= FILL_OPACITY_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_fill_opacity(const css_computed_style *style,
		css_fixed *fixed)
{
	uint32_t bits = style->i.bits[FILL_OPACITY_INDEX];
	bits &= FILL_OPACITY_MASK;
	bits >>= FILL_OPACITY_SHIFT;
	
	/* 1bit: t : type */
	if ((bits & 0x1) == CSS_FILL_OPACITY_SET) {
		*fixed = style->i.fill_opacity;
	}
	
	return (bits & 0x1);
}
#undef FILL_OPACITY_INDEX
#undef FILL_OPACITY_SHIFT
#undef FILL_OPACITY_MASK

#define FLEX_BASIS_INDEX 7
#define FLEX_BASIS_SHIFT 4
#define FLEX_BASIS_MASK 0x7f0
static inline uint8_t get_flex_basis_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FLEX_BASIS_INDEX];
	bits &= FLEX_BASIS_MASK;
	bits >>= FLEX_BASIS_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_flex_basis(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[FLEX_BASIS_INDEX];
	bits &= FLEX_BASIS_MASK;
	bits >>= FLEX_BASIS_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_FLEX_BASIS_SET) {
		*length = style->i.flex_basis;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef FLEX_BASIS_INDEX
#undef FLEX_BASIS_SHIFT
#undef FLEX_BASIS_MASK

#define FLEX_DIRECTION_INDEX 13
#define FLEX_DIRECTION_SHIFT 4
#define FLEX_DIRECTION_MASK 0x70
static inline uint8_t get_flex_direction_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FLEX_DIRECTION_INDEX];
	bits &= FLEX_DIRECTION_MASK;
	bits >>= FLEX_DIRECTION_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_flex_direction(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FLEX_DIRECTION_INDEX];
	bits &= FLEX_DIRECTION_MASK;
	bits >>= FLEX_DIRECTION_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef FLEX_DIRECTION_INDEX
#undef FLEX_DIRECTION_SHIFT
#undef FLEX_DIRECTION_MASK

#define FLEX_GROW_INDEX 14
#define FLEX_GROW_SHIFT 21
#define FLEX_GROW_MASK 0x200000
static inline uint8_t get_flex_grow_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FLEX_GROW_INDEX];
	bits &= FLEX_GROW_MASK;
	bits >>= FLEX_GROW_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_flex_grow(const css_computed_style *style, css_fixed
		*fixed)
{
	uint32_t bits = style->i.bits[FLEX_GROW_INDEX];
	bits &= FLEX_GROW_MASK;
	bits >>= FLEX_GROW_SHIFT;
	
	/* 1bit: t : type */
	if ((bits & 0x1) == CSS_FLEX_GROW_SET) {
		*fixed = style->i.flex_grow;
	}
	
	return (bits & 0x1);
}
#undef FLEX_GROW_INDEX
#undef FLEX_GROW_SHIFT
#undef FLEX_GROW_MASK

#define FLEX_SHRINK_INDEX 14
#define FLEX_SHRINK_SHIFT 22
#define FLEX_SHRINK_MASK 0x400000
static inline uint8_t get_flex_shrink_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FLEX_SHRINK_INDEX];
	bits &= FLEX_SHRINK_MASK;
	bits >>= FLEX_SHRINK_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_flex_shrink(const css_computed_style *style,
		css_fixed *fixed)
{
	uint32_t bits = style->i.bits[FLEX_SHRINK_INDEX];
	bits &= FLEX_SHRINK_MASK;
	bits >>= FLEX_SHRINK_SHIFT;
	
	/* 1bit: t : type */
	if ((bits & 0x1) == CSS_FLEX_SHRINK_SET) {
		*fixed = style->i.flex_shrink;
	}
	
	return (bits & 0x1);
}
#undef FLEX_SHRINK_INDEX
#undef FLEX_SHRINK_SHIFT
#undef FLEX_SHRINK_MASK

#define FLEX_WRAP_INDEX 11
#define FLEX_WRAP_SHIFT 28
#define FLEX_WRAP_MASK 0x30000000
static inline uint8_t get_flex_wrap_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FLEX_WRAP_INDEX];
	bits &= FLEX_WRAP_MASK;
	bits >>= FLEX_WRAP_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_flex_wrap(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FLEX_WRAP_INDEX];
	bits &= FLEX_WRAP_MASK;
	bits >>= FLEX_WRAP_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef FLEX_WRAP_INDEX
#undef FLEX_WRAP_SHIFT
#undef FLEX_WRAP_MASK

#define FLOAT_INDEX 11
#define FLOAT_SHIFT 30
#define FLOAT_MASK 0xc0000000
static inline uint8_t get_float_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FLOAT_INDEX];
	bits &= FLOAT_MASK;
	bits >>= FLOAT_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_float(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FLOAT_INDEX];
	bits &= FLOAT_MASK;
	bits >>= FLOAT_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef FLOAT_INDEX
#undef FLOAT_SHIFT
#undef FLOAT_MASK

#define FONT_FAMILY_INDEX 13
#define FONT_FAMILY_SHIFT 7
#define FONT_FAMILY_MASK 0x380
static inline uint8_t get_font_family_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FONT_FAMILY_INDEX];
	bits &= FONT_FAMILY_MASK;
	bits >>= FONT_FAMILY_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_font_family(const css_computed_style *style,
		lwc_string ***string_arr)
{
	uint32_t bits = style->i.bits[FONT_FAMILY_INDEX];
	bits &= FONT_FAMILY_MASK;
	bits >>= FONT_FAMILY_SHIFT;
	
	/* 3bits: ttt : type */
	*string_arr = style->font_family;
	
	return (bits & 0x7);
}
#undef FONT_FAMILY_INDEX
#undef FONT_FAMILY_SHIFT
#undef FONT_FAMILY_MASK

#define FONT_SIZE_INDEX 1
#define FONT_SIZE_SHIFT 23
#define FONT_SIZE_MASK 0xff800000
static inline uint8_t get_font_size_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FONT_SIZE_INDEX];
	bits &= FONT_SIZE_MASK;
	bits >>= FONT_SIZE_SHIFT;
	
	/* 9bits: uuuuutttt : unit | type */
	return (bits & 0xf);
}
static inline uint8_t get_font_size(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[FONT_SIZE_INDEX];
	bits &= FONT_SIZE_MASK;
	bits >>= FONT_SIZE_SHIFT;
	
	/* 9bits: uuuuutttt : unit | type */
	if ((bits & 0xf) == CSS_FONT_SIZE_DIMENSION) {
		*length = style->i.font_size;
		*unit = bits >> 4;
	}
	
	return (bits & 0xf);
}
#undef FONT_SIZE_INDEX
#undef FONT_SIZE_SHIFT
#undef FONT_SIZE_MASK

#define FONT_STYLE_INDEX 10
#define FONT_STYLE_SHIFT 0
#define FONT_STYLE_MASK 0x3
static inline uint8_t get_font_style_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FONT_STYLE_INDEX];
	bits &= FONT_STYLE_MASK;
	bits >>= FONT_STYLE_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_font_style(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FONT_STYLE_INDEX];
	bits &= FONT_STYLE_MASK;
	bits >>= FONT_STYLE_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef FONT_STYLE_INDEX
#undef FONT_STYLE_SHIFT
#undef FONT_STYLE_MASK

#define FONT_VARIANT_INDEX 10
#define FONT_VARIANT_SHIFT 2
#define FONT_VARIANT_MASK 0xc
static inline uint8_t get_font_variant_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FONT_VARIANT_INDEX];
	bits &= FONT_VARIANT_MASK;
	bits >>= FONT_VARIANT_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_font_variant(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FONT_VARIANT_INDEX];
	bits &= FONT_VARIANT_MASK;
	bits >>= FONT_VARIANT_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef FONT_VARIANT_INDEX
#undef FONT_VARIANT_SHIFT
#undef FONT_VARIANT_MASK

#define FONT_WEIGHT_INDEX 6
#define FONT_WEIGHT_SHIFT 0
#define FONT_WEIGHT_MASK 0xf
static inline uint8_t get_font_weight_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FONT_WEIGHT_INDEX];
	bits &= FONT_WEIGHT_MASK;
	bits >>= FONT_WEIGHT_SHIFT;
	
	/* 4bits: tttt : type */
	return (bits & 0xf);
}
static inline uint8_t get_font_weight(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[FONT_WEIGHT_INDEX];
	bits &= FONT_WEIGHT_MASK;
	bits >>= FONT_WEIGHT_SHIFT;
	
	/* 4bits: tttt : type */
	
	return (bits & 0xf);
}
#undef FONT_WEIGHT_INDEX
#undef FONT_WEIGHT_SHIFT
#undef FONT_WEIGHT_MASK

#define HEIGHT_INDEX 7
#define HEIGHT_SHIFT 11
#define HEIGHT_MASK 0x3f800
static inline uint8_t get_height_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[HEIGHT_INDEX];
	bits &= HEIGHT_MASK;
	bits >>= HEIGHT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_height(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[HEIGHT_INDEX];
	bits &= HEIGHT_MASK;
	bits >>= HEIGHT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_HEIGHT_SET) {
		*length = style->i.height;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef HEIGHT_INDEX
#undef HEIGHT_SHIFT
#undef HEIGHT_MASK

#define JUSTIFY_CONTENT_INDEX 13
#define JUSTIFY_CONTENT_SHIFT 10
#define JUSTIFY_CONTENT_MASK 0x1c00
static inline uint8_t get_justify_content_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[JUSTIFY_CONTENT_INDEX];
	bits &= JUSTIFY_CONTENT_MASK;
	bits >>= JUSTIFY_CONTENT_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_justify_content(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[JUSTIFY_CONTENT_INDEX];
	bits &= JUSTIFY_CONTENT_MASK;
	bits >>= JUSTIFY_CONTENT_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef JUSTIFY_CONTENT_INDEX
#undef JUSTIFY_CONTENT_SHIFT
#undef JUSTIFY_CONTENT_MASK

#define LEFT_INDEX 7
#define LEFT_SHIFT 18
#define LEFT_MASK 0x1fc0000
static inline uint8_t get_left_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[LEFT_INDEX];
	bits &= LEFT_MASK;
	bits >>= LEFT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_left(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[LEFT_INDEX];
	bits &= LEFT_MASK;
	bits >>= LEFT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_LEFT_SET) {
		*length = style->i.left;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef LEFT_INDEX
#undef LEFT_SHIFT
#undef LEFT_MASK

#define LETTER_SPACING_INDEX 7
#define LETTER_SPACING_SHIFT 25
#define LETTER_SPACING_MASK 0xfe000000
static inline uint8_t get_letter_spacing_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[LETTER_SPACING_INDEX];
	bits &= LETTER_SPACING_MASK;
	bits >>= LETTER_SPACING_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_letter_spacing(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[LETTER_SPACING_INDEX];
	bits &= LETTER_SPACING_MASK;
	bits >>= LETTER_SPACING_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_LETTER_SPACING_SET) {
		*length = style->i.letter_spacing;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef LETTER_SPACING_INDEX
#undef LETTER_SPACING_SHIFT
#undef LETTER_SPACING_MASK

#define LINE_HEIGHT_INDEX 6
#define LINE_HEIGHT_SHIFT 4
#define LINE_HEIGHT_MASK 0x7f0
static inline uint8_t get_line_height_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[LINE_HEIGHT_INDEX];
	bits &= LINE_HEIGHT_MASK;
	bits >>= LINE_HEIGHT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_line_height(
		const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[LINE_HEIGHT_INDEX];
	bits &= LINE_HEIGHT_MASK;
	bits >>= LINE_HEIGHT_SHIFT;

	/* 7bits: uuuuutt : units | type */
	if ((bits & 0x3) == CSS_LINE_HEIGHT_NUMBER ||
			(bits & 0x3) == CSS_LINE_HEIGHT_DIMENSION) {
		*length = style->i.line_height;
	}

	if ((bits & 0x3) == CSS_LINE_HEIGHT_DIMENSION) {
		*unit = bits >> 2;
	}

	return (bits & 0x3);
}
#undef LINE_HEIGHT_INDEX
#undef LINE_HEIGHT_SHIFT
#undef LINE_HEIGHT_MASK

#define LIST_STYLE_IMAGE_INDEX 14
#define LIST_STYLE_IMAGE_SHIFT 23
#define LIST_STYLE_IMAGE_MASK 0x800000
static inline uint8_t get_list_style_image_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[LIST_STYLE_IMAGE_INDEX];
	bits &= LIST_STYLE_IMAGE_MASK;
	bits >>= LIST_STYLE_IMAGE_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_list_style_image(const css_computed_style *style,
		lwc_string **string)
{
	uint32_t bits = style->i.bits[LIST_STYLE_IMAGE_INDEX];
	bits &= LIST_STYLE_IMAGE_MASK;
	bits >>= LIST_STYLE_IMAGE_SHIFT;
	
	/* 1bit: t : type */
	*string = style->i.list_style_image;
	
	return (bits & 0x1);
}
#undef LIST_STYLE_IMAGE_INDEX
#undef LIST_STYLE_IMAGE_SHIFT
#undef LIST_STYLE_IMAGE_MASK

#define LIST_STYLE_POSITION_INDEX 10
#define LIST_STYLE_POSITION_SHIFT 4
#define LIST_STYLE_POSITION_MASK 0x30
static inline uint8_t get_list_style_position_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[LIST_STYLE_POSITION_INDEX];
	bits &= LIST_STYLE_POSITION_MASK;
	bits >>= LIST_STYLE_POSITION_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_list_style_position(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[LIST_STYLE_POSITION_INDEX];
	bits &= LIST_STYLE_POSITION_MASK;
	bits >>= LIST_STYLE_POSITION_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef LIST_STYLE_POSITION_INDEX
#undef LIST_STYLE_POSITION_SHIFT
#undef LIST_STYLE_POSITION_MASK

#define LIST_STYLE_TYPE_INDEX 8
#define LIST_STYLE_TYPE_SHIFT 8
#define LIST_STYLE_TYPE_MASK 0x3f00
static inline uint8_t get_list_style_type_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[LIST_STYLE_TYPE_INDEX];
	bits &= LIST_STYLE_TYPE_MASK;
	bits >>= LIST_STYLE_TYPE_SHIFT;
	
	/* 6bits: tttttt : type */
	return (bits & 0x3f);
}
static inline uint8_t get_list_style_type(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[LIST_STYLE_TYPE_INDEX];
	bits &= LIST_STYLE_TYPE_MASK;
	bits >>= LIST_STYLE_TYPE_SHIFT;
	
	/* 6bits: tttttt : type */
	
	return (bits & 0x3f);
}
#undef LIST_STYLE_TYPE_INDEX
#undef LIST_STYLE_TYPE_SHIFT
#undef LIST_STYLE_TYPE_MASK

#define MARGIN_BOTTOM_INDEX 6
#define MARGIN_BOTTOM_SHIFT 11
#define MARGIN_BOTTOM_MASK 0x3f800
static inline uint8_t get_margin_bottom_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[MARGIN_BOTTOM_INDEX];
	bits &= MARGIN_BOTTOM_MASK;
	bits >>= MARGIN_BOTTOM_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_margin_bottom(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[MARGIN_BOTTOM_INDEX];
	bits &= MARGIN_BOTTOM_MASK;
	bits >>= MARGIN_BOTTOM_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_MARGIN_SET) {
		*length = style->i.margin_bottom;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef MARGIN_BOTTOM_INDEX
#undef MARGIN_BOTTOM_SHIFT
#undef MARGIN_BOTTOM_MASK

#define MARGIN_LEFT_INDEX 6
#define MARGIN_LEFT_SHIFT 18
#define MARGIN_LEFT_MASK 0x1fc0000
static inline uint8_t get_margin_left_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[MARGIN_LEFT_INDEX];
	bits &= MARGIN_LEFT_MASK;
	bits >>= MARGIN_LEFT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_margin_left(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[MARGIN_LEFT_INDEX];
	bits &= MARGIN_LEFT_MASK;
	bits >>= MARGIN_LEFT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_MARGIN_SET) {
		*length = style->i.margin_left;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef MARGIN_LEFT_INDEX
#undef MARGIN_LEFT_SHIFT
#undef MARGIN_LEFT_MASK

#define MARGIN_RIGHT_INDEX 6
#define MARGIN_RIGHT_SHIFT 25
#define MARGIN_RIGHT_MASK 0xfe000000
static inline uint8_t get_margin_right_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[MARGIN_RIGHT_INDEX];
	bits &= MARGIN_RIGHT_MASK;
	bits >>= MARGIN_RIGHT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_margin_right(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[MARGIN_RIGHT_INDEX];
	bits &= MARGIN_RIGHT_MASK;
	bits >>= MARGIN_RIGHT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_MARGIN_SET) {
		*length = style->i.margin_right;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef MARGIN_RIGHT_INDEX
#undef MARGIN_RIGHT_SHIFT
#undef MARGIN_RIGHT_MASK

#define MARGIN_TOP_INDEX 5
#define MARGIN_TOP_SHIFT 4
#define MARGIN_TOP_MASK 0x7f0
static inline uint8_t get_margin_top_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[MARGIN_TOP_INDEX];
	bits &= MARGIN_TOP_MASK;
	bits >>= MARGIN_TOP_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_margin_top(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[MARGIN_TOP_INDEX];
	bits &= MARGIN_TOP_MASK;
	bits >>= MARGIN_TOP_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_MARGIN_SET) {
		*length = style->i.margin_top;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef MARGIN_TOP_INDEX
#undef MARGIN_TOP_SHIFT
#undef MARGIN_TOP_MASK

#define MAX_HEIGHT_INDEX 5
#define MAX_HEIGHT_SHIFT 11
#define MAX_HEIGHT_MASK 0x3f800
static inline uint8_t get_max_height_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[MAX_HEIGHT_INDEX];
	bits &= MAX_HEIGHT_MASK;
	bits >>= MAX_HEIGHT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_max_height(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[MAX_HEIGHT_INDEX];
	bits &= MAX_HEIGHT_MASK;
	bits >>= MAX_HEIGHT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_MAX_HEIGHT_SET) {
		*length = style->i.max_height;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef MAX_HEIGHT_INDEX
#undef MAX_HEIGHT_SHIFT
#undef MAX_HEIGHT_MASK

#define MAX_WIDTH_INDEX 5
#define MAX_WIDTH_SHIFT 18
#define MAX_WIDTH_MASK 0x1fc0000
static inline uint8_t get_max_width_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[MAX_WIDTH_INDEX];
	bits &= MAX_WIDTH_MASK;
	bits >>= MAX_WIDTH_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_max_width(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[MAX_WIDTH_INDEX];
	bits &= MAX_WIDTH_MASK;
	bits >>= MAX_WIDTH_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_MAX_WIDTH_SET) {
		*length = style->i.max_width;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef MAX_WIDTH_INDEX
#undef MAX_WIDTH_SHIFT
#undef MAX_WIDTH_MASK

#define MIN_HEIGHT_INDEX 5
#define MIN_HEIGHT_SHIFT 25
#define MIN_HEIGHT_MASK 0xfe000000
static inline uint8_t get_min_height_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[MIN_HEIGHT_INDEX];
	bits &= MIN_HEIGHT_MASK;
	bits >>= MIN_HEIGHT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_min_height(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[MIN_HEIGHT_INDEX];
	bits &= MIN_HEIGHT_MASK;
	bits >>= MIN_HEIGHT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_MIN_HEIGHT_SET) {
		*length = style->i.min_height;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef MIN_HEIGHT_INDEX
#undef MIN_HEIGHT_SHIFT
#undef MIN_HEIGHT_MASK

#define MIN_WIDTH_INDEX 4
#define MIN_WIDTH_SHIFT 4
#define MIN_WIDTH_MASK 0x7f0
static inline uint8_t get_min_width_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[MIN_WIDTH_INDEX];
	bits &= MIN_WIDTH_MASK;
	bits >>= MIN_WIDTH_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_min_width(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[MIN_WIDTH_INDEX];
	bits &= MIN_WIDTH_MASK;
	bits >>= MIN_WIDTH_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_MIN_WIDTH_SET) {
		*length = style->i.min_width;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef MIN_WIDTH_INDEX
#undef MIN_WIDTH_SHIFT
#undef MIN_WIDTH_MASK

#define OPACITY_INDEX 14
#define OPACITY_SHIFT 24
#define OPACITY_MASK 0x1000000
static inline uint8_t get_opacity_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[OPACITY_INDEX];
	bits &= OPACITY_MASK;
	bits >>= OPACITY_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_opacity(const css_computed_style *style, css_fixed
		*fixed)
{
	uint32_t bits = style->i.bits[OPACITY_INDEX];
	bits &= OPACITY_MASK;
	bits >>= OPACITY_SHIFT;
	
	/* 1bit: t : type */
	if ((bits & 0x1) == CSS_OPACITY_SET) {
		*fixed = style->i.opacity;
	}
	
	return (bits & 0x1);
}
#undef OPACITY_INDEX
#undef OPACITY_SHIFT
#undef OPACITY_MASK

#define ORDER_INDEX 14
#define ORDER_SHIFT 25
#define ORDER_MASK 0x2000000
static inline uint8_t get_order_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[ORDER_INDEX];
	bits &= ORDER_MASK;
	bits >>= ORDER_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_order(const css_computed_style *style, int32_t
		*integer)
{
	uint32_t bits = style->i.bits[ORDER_INDEX];
	bits &= ORDER_MASK;
	bits >>= ORDER_SHIFT;
	
	/* 1bit: t : type */
	if ((bits & 0x1) == CSS_ORDER_SET) {
		*integer = style->i.order;
	}
	
	return (bits & 0x1);
}
#undef ORDER_INDEX
#undef ORDER_SHIFT
#undef ORDER_MASK

#define ORPHANS_INDEX 14
#define ORPHANS_SHIFT 26
#define ORPHANS_MASK 0x4000000
static inline uint8_t get_orphans_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[ORPHANS_INDEX];
	bits &= ORPHANS_MASK;
	bits >>= ORPHANS_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_orphans(const css_computed_style *style, int32_t
		*integer)
{
	uint32_t bits = style->i.bits[ORPHANS_INDEX];
	bits &= ORPHANS_MASK;
	bits >>= ORPHANS_SHIFT;
	
	/* 1bit: t : type */
	*integer = style->i.orphans;
	
	return (bits & 0x1);
}
#undef ORPHANS_INDEX
#undef ORPHANS_SHIFT
#undef ORPHANS_MASK

#define OUTLINE_COLOR_INDEX 10
#define OUTLINE_COLOR_SHIFT 6
#define OUTLINE_COLOR_MASK 0xc0
static inline uint8_t get_outline_color_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[OUTLINE_COLOR_INDEX];
	bits &= OUTLINE_COLOR_MASK;
	bits >>= OUTLINE_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_outline_color(const css_computed_style *style,
		css_color *color)
{
	uint32_t bits = style->i.bits[OUTLINE_COLOR_INDEX];
	bits &= OUTLINE_COLOR_MASK;
	bits >>= OUTLINE_COLOR_SHIFT;
	
	/* 2bits: tt : type */
	if ((bits & 0x3) == CSS_OUTLINE_COLOR_COLOR) {
		*color = style->i.outline_color;
	}
	
	return (bits & 0x3);
}
#undef OUTLINE_COLOR_INDEX
#undef OUTLINE_COLOR_SHIFT
#undef OUTLINE_COLOR_MASK

#define OUTLINE_STYLE_INDEX 5
#define OUTLINE_STYLE_SHIFT 0
#define OUTLINE_STYLE_MASK 0xf
static inline uint8_t get_outline_style_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[OUTLINE_STYLE_INDEX];
	bits &= OUTLINE_STYLE_MASK;
	bits >>= OUTLINE_STYLE_SHIFT;
	
	/* 4bits: tttt : type */
	return (bits & 0xf);
}
static inline uint8_t get_outline_style(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[OUTLINE_STYLE_INDEX];
	bits &= OUTLINE_STYLE_MASK;
	bits >>= OUTLINE_STYLE_SHIFT;
	
	/* 4bits: tttt : type */
	
	return (bits & 0xf);
}
#undef OUTLINE_STYLE_INDEX
#undef OUTLINE_STYLE_SHIFT
#undef OUTLINE_STYLE_MASK

#define OUTLINE_WIDTH_INDEX 1
#define OUTLINE_WIDTH_SHIFT 15
#define OUTLINE_WIDTH_MASK 0x7f8000
static inline uint8_t get_outline_width_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[OUTLINE_WIDTH_INDEX];
	bits &= OUTLINE_WIDTH_MASK;
	bits >>= OUTLINE_WIDTH_SHIFT;
	
	/* 8bits: uuuuuttt : unit | type */
	return (bits & 0x7);
}
static inline uint8_t get_outline_width(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[OUTLINE_WIDTH_INDEX];
	bits &= OUTLINE_WIDTH_MASK;
	bits >>= OUTLINE_WIDTH_SHIFT;
	
	/* 8bits: uuuuuttt : unit | type */
	if ((bits & 0x7) == CSS_OUTLINE_WIDTH_WIDTH) {
		*length = style->i.outline_width;
		*unit = bits >> 3;
	}
	
	return (bits & 0x7);
}
#undef OUTLINE_WIDTH_INDEX
#undef OUTLINE_WIDTH_SHIFT
#undef OUTLINE_WIDTH_MASK

#define OVERFLOW_X_INDEX 13
#define OVERFLOW_X_SHIFT 13
#define OVERFLOW_X_MASK 0xe000
static inline uint8_t get_overflow_x_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[OVERFLOW_X_INDEX];
	bits &= OVERFLOW_X_MASK;
	bits >>= OVERFLOW_X_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_overflow_x(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[OVERFLOW_X_INDEX];
	bits &= OVERFLOW_X_MASK;
	bits >>= OVERFLOW_X_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef OVERFLOW_X_INDEX
#undef OVERFLOW_X_SHIFT
#undef OVERFLOW_X_MASK

#define OVERFLOW_Y_INDEX 13
#define OVERFLOW_Y_SHIFT 16
#define OVERFLOW_Y_MASK 0x70000
static inline uint8_t get_overflow_y_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[OVERFLOW_Y_INDEX];
	bits &= OVERFLOW_Y_MASK;
	bits >>= OVERFLOW_Y_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_overflow_y(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[OVERFLOW_Y_INDEX];
	bits &= OVERFLOW_Y_MASK;
	bits >>= OVERFLOW_Y_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef OVERFLOW_Y_INDEX
#undef OVERFLOW_Y_SHIFT
#undef OVERFLOW_Y_MASK

#define PADDING_BOTTOM_INDEX 8
#define PADDING_BOTTOM_SHIFT 14
#define PADDING_BOTTOM_MASK 0xfc000
static inline uint8_t get_padding_bottom_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[PADDING_BOTTOM_INDEX];
	bits &= PADDING_BOTTOM_MASK;
	bits >>= PADDING_BOTTOM_SHIFT;
	
	/* 6bits: uuuuut : unit | type */
	return (bits & 0x1);
}
static inline uint8_t get_padding_bottom(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[PADDING_BOTTOM_INDEX];
	bits &= PADDING_BOTTOM_MASK;
	bits >>= PADDING_BOTTOM_SHIFT;
	
	/* 6bits: uuuuut : unit | type */
	if ((bits & 0x1) == CSS_PADDING_SET) {
		*length = style->i.padding_bottom;
		*unit = bits >> 1;
	}
	
	return (bits & 0x1);
}
#undef PADDING_BOTTOM_INDEX
#undef PADDING_BOTTOM_SHIFT
#undef PADDING_BOTTOM_MASK

#define PADDING_LEFT_INDEX 8
#define PADDING_LEFT_SHIFT 20
#define PADDING_LEFT_MASK 0x3f00000
static inline uint8_t get_padding_left_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[PADDING_LEFT_INDEX];
	bits &= PADDING_LEFT_MASK;
	bits >>= PADDING_LEFT_SHIFT;
	
	/* 6bits: uuuuut : unit | type */
	return (bits & 0x1);
}
static inline uint8_t get_padding_left(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[PADDING_LEFT_INDEX];
	bits &= PADDING_LEFT_MASK;
	bits >>= PADDING_LEFT_SHIFT;
	
	/* 6bits: uuuuut : unit | type */
	if ((bits & 0x1) == CSS_PADDING_SET) {
		*length = style->i.padding_left;
		*unit = bits >> 1;
	}
	
	return (bits & 0x1);
}
#undef PADDING_LEFT_INDEX
#undef PADDING_LEFT_SHIFT
#undef PADDING_LEFT_MASK

#define PADDING_RIGHT_INDEX 8
#define PADDING_RIGHT_SHIFT 26
#define PADDING_RIGHT_MASK 0xfc000000
static inline uint8_t get_padding_right_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[PADDING_RIGHT_INDEX];
	bits &= PADDING_RIGHT_MASK;
	bits >>= PADDING_RIGHT_SHIFT;
	
	/* 6bits: uuuuut : unit | type */
	return (bits & 0x1);
}
static inline uint8_t get_padding_right(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[PADDING_RIGHT_INDEX];
	bits &= PADDING_RIGHT_MASK;
	bits >>= PADDING_RIGHT_SHIFT;
	
	/* 6bits: uuuuut : unit | type */
	if ((bits & 0x1) == CSS_PADDING_SET) {
		*length = style->i.padding_right;
		*unit = bits >> 1;
	}
	
	return (bits & 0x1);
}
#undef PADDING_RIGHT_INDEX
#undef PADDING_RIGHT_SHIFT
#undef PADDING_RIGHT_MASK

#define PADDING_TOP_INDEX 3
#define PADDING_TOP_SHIFT 5
#define PADDING_TOP_MASK 0x7e0
static inline uint8_t get_padding_top_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[PADDING_TOP_INDEX];
	bits &= PADDING_TOP_MASK;
	bits >>= PADDING_TOP_SHIFT;
	
	/* 6bits: uuuuut : unit | type */
	return (bits & 0x1);
}
static inline uint8_t get_padding_top(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[PADDING_TOP_INDEX];
	bits &= PADDING_TOP_MASK;
	bits >>= PADDING_TOP_SHIFT;
	
	/* 6bits: uuuuut : unit | type */
	if ((bits & 0x1) == CSS_PADDING_SET) {
		*length = style->i.padding_top;
		*unit = bits >> 1;
	}
	
	return (bits & 0x1);
}
#undef PADDING_TOP_INDEX
#undef PADDING_TOP_SHIFT
#undef PADDING_TOP_MASK

#define PAGE_BREAK_AFTER_INDEX 13
#define PAGE_BREAK_AFTER_SHIFT 19
#define PAGE_BREAK_AFTER_MASK 0x380000
static inline uint8_t get_page_break_after_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[PAGE_BREAK_AFTER_INDEX];
	bits &= PAGE_BREAK_AFTER_MASK;
	bits >>= PAGE_BREAK_AFTER_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_page_break_after(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[PAGE_BREAK_AFTER_INDEX];
	bits &= PAGE_BREAK_AFTER_MASK;
	bits >>= PAGE_BREAK_AFTER_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef PAGE_BREAK_AFTER_INDEX
#undef PAGE_BREAK_AFTER_SHIFT
#undef PAGE_BREAK_AFTER_MASK

#define PAGE_BREAK_BEFORE_INDEX 13
#define PAGE_BREAK_BEFORE_SHIFT 22
#define PAGE_BREAK_BEFORE_MASK 0x1c00000
static inline uint8_t get_page_break_before_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[PAGE_BREAK_BEFORE_INDEX];
	bits &= PAGE_BREAK_BEFORE_MASK;
	bits >>= PAGE_BREAK_BEFORE_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_page_break_before(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[PAGE_BREAK_BEFORE_INDEX];
	bits &= PAGE_BREAK_BEFORE_MASK;
	bits >>= PAGE_BREAK_BEFORE_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef PAGE_BREAK_BEFORE_INDEX
#undef PAGE_BREAK_BEFORE_SHIFT
#undef PAGE_BREAK_BEFORE_MASK

#define PAGE_BREAK_INSIDE_INDEX 10
#define PAGE_BREAK_INSIDE_SHIFT 8
#define PAGE_BREAK_INSIDE_MASK 0x300
static inline uint8_t get_page_break_inside_bits(const css_computed_style
		*style)
{
	uint32_t bits = style->i.bits[PAGE_BREAK_INSIDE_INDEX];
	bits &= PAGE_BREAK_INSIDE_MASK;
	bits >>= PAGE_BREAK_INSIDE_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_page_break_inside(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[PAGE_BREAK_INSIDE_INDEX];
	bits &= PAGE_BREAK_INSIDE_MASK;
	bits >>= PAGE_BREAK_INSIDE_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef PAGE_BREAK_INSIDE_INDEX
#undef PAGE_BREAK_INSIDE_SHIFT
#undef PAGE_BREAK_INSIDE_MASK

#define POSITION_INDEX 13
#define POSITION_SHIFT 25
#define POSITION_MASK 0xe000000
static inline uint8_t get_position_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[POSITION_INDEX];
	bits &= POSITION_MASK;
	bits >>= POSITION_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_position(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[POSITION_INDEX];
	bits &= POSITION_MASK;
	bits >>= POSITION_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef POSITION_INDEX
#undef POSITION_SHIFT
#undef POSITION_MASK

#define QUOTES_INDEX 14
#define QUOTES_SHIFT 27
#define QUOTES_MASK 0x8000000
static inline uint8_t get_quotes_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[QUOTES_INDEX];
	bits &= QUOTES_MASK;
	bits >>= QUOTES_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_quotes(const css_computed_style *style, lwc_string
		***string_arr)
{
	uint32_t bits = style->i.bits[QUOTES_INDEX];
	bits &= QUOTES_MASK;
	bits >>= QUOTES_SHIFT;
	
	/* 1bit: t : type */
	*string_arr = style->quotes;
	
	return (bits & 0x1);
}
#undef QUOTES_INDEX
#undef QUOTES_SHIFT
#undef QUOTES_MASK

#define RIGHT_INDEX 4
#define RIGHT_SHIFT 11
#define RIGHT_MASK 0x3f800
static inline uint8_t get_right_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[RIGHT_INDEX];
	bits &= RIGHT_MASK;
	bits >>= RIGHT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_right(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[RIGHT_INDEX];
	bits &= RIGHT_MASK;
	bits >>= RIGHT_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_RIGHT_SET) {
		*length = style->i.right;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef RIGHT_INDEX
#undef RIGHT_SHIFT
#undef RIGHT_MASK

#define STROKE_OPACITY_INDEX 13
#define STROKE_OPACITY_SHIFT 0
#define STROKE_OPACITY_MASK 0x1
static inline uint8_t get_stroke_opacity_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[STROKE_OPACITY_INDEX];
	bits &= STROKE_OPACITY_MASK;
	bits >>= STROKE_OPACITY_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_stroke_opacity(const css_computed_style *style,
		css_fixed *fixed)
{
	uint32_t bits = style->i.bits[STROKE_OPACITY_INDEX];
	bits &= STROKE_OPACITY_MASK;
	bits >>= STROKE_OPACITY_SHIFT;
	
	/* 1bit: t : type */
	if ((bits & 0x1) == CSS_STROKE_OPACITY_SET) {
		*fixed = style->i.stroke_opacity;
	}
	
	return (bits & 0x1);
}
#undef STROKE_OPACITY_INDEX
#undef STROKE_OPACITY_SHIFT
#undef STROKE_OPACITY_MASK

#define TABLE_LAYOUT_INDEX 10
#define TABLE_LAYOUT_SHIFT 10
#define TABLE_LAYOUT_MASK 0xc00
static inline uint8_t get_table_layout_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[TABLE_LAYOUT_INDEX];
	bits &= TABLE_LAYOUT_MASK;
	bits >>= TABLE_LAYOUT_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_table_layout(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[TABLE_LAYOUT_INDEX];
	bits &= TABLE_LAYOUT_MASK;
	bits >>= TABLE_LAYOUT_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef TABLE_LAYOUT_INDEX
#undef TABLE_LAYOUT_SHIFT
#undef TABLE_LAYOUT_MASK

#define TEXT_ALIGN_INDEX 4
#define TEXT_ALIGN_SHIFT 0
#define TEXT_ALIGN_MASK 0xf
static inline uint8_t get_text_align_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[TEXT_ALIGN_INDEX];
	bits &= TEXT_ALIGN_MASK;
	bits >>= TEXT_ALIGN_SHIFT;
	
	/* 4bits: tttt : type */
	return (bits & 0xf);
}
static inline uint8_t get_text_align(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[TEXT_ALIGN_INDEX];
	bits &= TEXT_ALIGN_MASK;
	bits >>= TEXT_ALIGN_SHIFT;
	
	/* 4bits: tttt : type */
	
	return (bits & 0xf);
}
#undef TEXT_ALIGN_INDEX
#undef TEXT_ALIGN_SHIFT
#undef TEXT_ALIGN_MASK

#define TEXT_DECORATION_INDEX 3
#define TEXT_DECORATION_SHIFT 0
#define TEXT_DECORATION_MASK 0x1f
static inline uint8_t get_text_decoration_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[TEXT_DECORATION_INDEX];
	bits &= TEXT_DECORATION_MASK;
	bits >>= TEXT_DECORATION_SHIFT;
	
	/* 5bits: ttttt : type */
	return (bits & 0x1f);
}
static inline uint8_t get_text_decoration(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[TEXT_DECORATION_INDEX];
	bits &= TEXT_DECORATION_MASK;
	bits >>= TEXT_DECORATION_SHIFT;
	
	/* 5bits: ttttt : type */
	
	return (bits & 0x1f);
}
#undef TEXT_DECORATION_INDEX
#undef TEXT_DECORATION_SHIFT
#undef TEXT_DECORATION_MASK

#define TEXT_INDENT_INDEX 2
#define TEXT_INDENT_SHIFT 0
#define TEXT_INDENT_MASK 0x3f
static inline uint8_t get_text_indent_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[TEXT_INDENT_INDEX];
	bits &= TEXT_INDENT_MASK;
	bits >>= TEXT_INDENT_SHIFT;
	
	/* 6bits: uuuuut : unit | type */
	return (bits & 0x1);
}
static inline uint8_t get_text_indent(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[TEXT_INDENT_INDEX];
	bits &= TEXT_INDENT_MASK;
	bits >>= TEXT_INDENT_SHIFT;
	
	/* 6bits: uuuuut : unit | type */
	if ((bits & 0x1) == CSS_TEXT_INDENT_SET) {
		*length = style->i.text_indent;
		*unit = bits >> 1;
	}
	
	return (bits & 0x1);
}
#undef TEXT_INDENT_INDEX
#undef TEXT_INDENT_SHIFT
#undef TEXT_INDENT_MASK

#define TEXT_TRANSFORM_INDEX 9
#define TEXT_TRANSFORM_SHIFT 0
#define TEXT_TRANSFORM_MASK 0x7
static inline uint8_t get_text_transform_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[TEXT_TRANSFORM_INDEX];
	bits &= TEXT_TRANSFORM_MASK;
	bits >>= TEXT_TRANSFORM_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_text_transform(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[TEXT_TRANSFORM_INDEX];
	bits &= TEXT_TRANSFORM_MASK;
	bits >>= TEXT_TRANSFORM_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef TEXT_TRANSFORM_INDEX
#undef TEXT_TRANSFORM_SHIFT
#undef TEXT_TRANSFORM_MASK

#define TOP_INDEX 4
#define TOP_SHIFT 18
#define TOP_MASK 0x1fc0000
static inline uint8_t get_top_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[TOP_INDEX];
	bits &= TOP_MASK;
	bits >>= TOP_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_top(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[TOP_INDEX];
	bits &= TOP_MASK;
	bits >>= TOP_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_TOP_SET) {
		*length = style->i.top;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef TOP_INDEX
#undef TOP_SHIFT
#undef TOP_MASK

#define UNICODE_BIDI_INDEX 10
#define UNICODE_BIDI_SHIFT 12
#define UNICODE_BIDI_MASK 0x3000
static inline uint8_t get_unicode_bidi_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[UNICODE_BIDI_INDEX];
	bits &= UNICODE_BIDI_MASK;
	bits >>= UNICODE_BIDI_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_unicode_bidi(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[UNICODE_BIDI_INDEX];
	bits &= UNICODE_BIDI_MASK;
	bits >>= UNICODE_BIDI_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef UNICODE_BIDI_INDEX
#undef UNICODE_BIDI_SHIFT
#undef UNICODE_BIDI_MASK

#define VERTICAL_ALIGN_INDEX 12
#define VERTICAL_ALIGN_SHIFT 1
#define VERTICAL_ALIGN_MASK 0x3fe
static inline uint8_t get_vertical_align_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[VERTICAL_ALIGN_INDEX];
	bits &= VERTICAL_ALIGN_MASK;
	bits >>= VERTICAL_ALIGN_SHIFT;
	
	/* 9bits: uuuuutttt : unit | type */
	return (bits & 0xf);
}
static inline uint8_t get_vertical_align(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[VERTICAL_ALIGN_INDEX];
	bits &= VERTICAL_ALIGN_MASK;
	bits >>= VERTICAL_ALIGN_SHIFT;
	
	/* 9bits: uuuuutttt : unit | type */
	if ((bits & 0xf) == CSS_VERTICAL_ALIGN_SET) {
		*length = style->i.vertical_align;
		*unit = bits >> 4;
	}
	
	return (bits & 0xf);
}
#undef VERTICAL_ALIGN_INDEX
#undef VERTICAL_ALIGN_SHIFT
#undef VERTICAL_ALIGN_MASK

#define VISIBILITY_INDEX 10
#define VISIBILITY_SHIFT 14
#define VISIBILITY_MASK 0xc000
static inline uint8_t get_visibility_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[VISIBILITY_INDEX];
	bits &= VISIBILITY_MASK;
	bits >>= VISIBILITY_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_visibility(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[VISIBILITY_INDEX];
	bits &= VISIBILITY_MASK;
	bits >>= VISIBILITY_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef VISIBILITY_INDEX
#undef VISIBILITY_SHIFT
#undef VISIBILITY_MASK

#define WHITE_SPACE_INDEX 8
#define WHITE_SPACE_SHIFT 0
#define WHITE_SPACE_MASK 0x7
static inline uint8_t get_white_space_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[WHITE_SPACE_INDEX];
	bits &= WHITE_SPACE_MASK;
	bits >>= WHITE_SPACE_SHIFT;
	
	/* 3bits: ttt : type */
	return (bits & 0x7);
}
static inline uint8_t get_white_space(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[WHITE_SPACE_INDEX];
	bits &= WHITE_SPACE_MASK;
	bits >>= WHITE_SPACE_SHIFT;
	
	/* 3bits: ttt : type */
	
	return (bits & 0x7);
}
#undef WHITE_SPACE_INDEX
#undef WHITE_SPACE_SHIFT
#undef WHITE_SPACE_MASK

#define WIDOWS_INDEX 12
#define WIDOWS_SHIFT 0
#define WIDOWS_MASK 0x1
static inline uint8_t get_widows_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[WIDOWS_INDEX];
	bits &= WIDOWS_MASK;
	bits >>= WIDOWS_SHIFT;
	
	/* 1bit: t : type */
	return (bits & 0x1);
}
static inline uint8_t get_widows(const css_computed_style *style, int32_t
		*integer)
{
	uint32_t bits = style->i.bits[WIDOWS_INDEX];
	bits &= WIDOWS_MASK;
	bits >>= WIDOWS_SHIFT;
	
	/* 1bit: t : type */
	*integer = style->i.widows;
	
	return (bits & 0x1);
}
#undef WIDOWS_INDEX
#undef WIDOWS_SHIFT
#undef WIDOWS_MASK

#define WIDTH_INDEX 4
#define WIDTH_SHIFT 25
#define WIDTH_MASK 0xfe000000
static inline uint8_t get_width_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[WIDTH_INDEX];
	bits &= WIDTH_MASK;
	bits >>= WIDTH_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_width(const css_computed_style *style, css_fixed
		*length, css_unit *unit)
{
	uint32_t bits = style->i.bits[WIDTH_INDEX];
	bits &= WIDTH_MASK;
	bits >>= WIDTH_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_WIDTH_SET) {
		*length = style->i.width;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef WIDTH_INDEX
#undef WIDTH_SHIFT
#undef WIDTH_MASK

#define WORD_SPACING_INDEX 1
#define WORD_SPACING_SHIFT 0
#define WORD_SPACING_MASK 0x7f
static inline uint8_t get_word_spacing_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[WORD_SPACING_INDEX];
	bits &= WORD_SPACING_MASK;
	bits >>= WORD_SPACING_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	return (bits & 0x3);
}
static inline uint8_t get_word_spacing(const css_computed_style *style,
		css_fixed *length, css_unit *unit)
{
	uint32_t bits = style->i.bits[WORD_SPACING_INDEX];
	bits &= WORD_SPACING_MASK;
	bits >>= WORD_SPACING_SHIFT;
	
	/* 7bits: uuuuutt : unit | type */
	if ((bits & 0x3) == CSS_WORD_SPACING_SET) {
		*length = style->i.word_spacing;
		*unit = bits >> 2;
	}
	
	return (bits & 0x3);
}
#undef WORD_SPACING_INDEX
#undef WORD_SPACING_SHIFT
#undef WORD_SPACING_MASK

#define WRITING_MODE_INDEX 10
#define WRITING_MODE_SHIFT 16
#define WRITING_MODE_MASK 0x30000
static inline uint8_t get_writing_mode_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[WRITING_MODE_INDEX];
	bits &= WRITING_MODE_MASK;
	bits >>= WRITING_MODE_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_writing_mode(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[WRITING_MODE_INDEX];
	bits &= WRITING_MODE_MASK;
	bits >>= WRITING_MODE_SHIFT;
	
	/* 2bits: tt : type */
	
	return (bits & 0x3);
}
#undef WRITING_MODE_INDEX
#undef WRITING_MODE_SHIFT
#undef WRITING_MODE_MASK

#define Z_INDEX_INDEX 10
#define Z_INDEX_SHIFT 18
#define Z_INDEX_MASK 0xc0000
static inline uint8_t get_z_index_bits(const css_computed_style *style)
{
	uint32_t bits = style->i.bits[Z_INDEX_INDEX];
	bits &= Z_INDEX_MASK;
	bits >>= Z_INDEX_SHIFT;
	
	/* 2bits: tt : type */
	return (bits & 0x3);
}
static inline uint8_t get_z_index(const css_computed_style *style, int32_t
		*integer)
{
	uint32_t bits = style->i.bits[Z_INDEX_INDEX];
	bits &= Z_INDEX_MASK;
	bits >>= Z_INDEX_SHIFT;
	
	/* 2bits: tt : type */
	*integer = style->i.z_index;
	
	return (bits & 0x3);
}
#undef Z_INDEX_INDEX
#undef Z_INDEX_SHIFT
#undef Z_INDEX_MASK
