# This file is part of LibCSS.
# Licensed under the MIT License,
# http://www.opensource.org/licenses/mit-license.php
# Copyright 2017 Lucas Neves <lcneves@gmail.com>

overrides = {
    'get': {},
    'set': {},
    'properties': {}
}

overrides['get']['clip'] = '''\
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
}'''

overrides['set']['clip'] = '''\
static inline css_error set_clip(
		css_computed_style *style, uint8_t type,
		css_computed_clip_rect *rect)
{
	uint32_t *bits;

	bits = &style->i.bits[CLIP_INDEX];

	/*
	26bits: tt tttr rrrr bbbb blll llTR BLyy:
	units: top | right | bottom | left
	opcodes: top | right | bottom | left | type
	*/
	*bits = (*bits & ~CLIP_MASK) |
			((type & 0x3) << CLIP_SHIFT);

	if (type == CSS_CLIP_RECT) {
		*bits |= (((rect->top_auto ? 0x20 : 0) |
				(rect->right_auto ? 0x10 : 0) |
				(rect->bottom_auto ? 0x8 : 0) |
				(rect->left_auto ? 0x4 : 0)) << CLIP_SHIFT);

		*bits |= (((rect->tunit << 5) | rect->runit)
				<< (CLIP_SHIFT + 16));

		*bits |= (((rect->bunit << 5) | rect->lunit)
				<< (CLIP_SHIFT + 6));

		style->i.clip_a = rect->top;
		style->i.clip_b = rect->right;
		style->i.clip_c = rect->bottom;
		style->i.clip_d = rect->left;
	}

	return CSS_OK;
}'''

overrides['get']['line_height'] = '''\
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
}'''

overrides['set']['content'] = '''\
static inline css_error set_content(
		css_computed_style *style, uint8_t type,
		css_computed_content_item *content)
{
	uint32_t *bits;
	css_computed_content_item *oldcontent;
	css_computed_content_item *c;

	/* 2bits: type */
	bits = &style->i.bits[CONTENT_INDEX];
	oldcontent = style->content;

	*bits = (*bits & ~CONTENT_MASK) |
			((type & 0x3) << CONTENT_SHIFT);

	for (c = content; c != NULL &&
			c->type != CSS_COMPUTED_CONTENT_NONE; c++) {
		switch (c->type) {
		case CSS_COMPUTED_CONTENT_STRING:
			c->data.string = lwc_string_ref(c->data.string);
			break;
		case CSS_COMPUTED_CONTENT_URI:
			c->data.uri = lwc_string_ref(c->data.uri);
			break;
		case CSS_COMPUTED_CONTENT_ATTR:
			c->data.attr = lwc_string_ref(c->data.attr);
			break;
		case CSS_COMPUTED_CONTENT_COUNTER:
			c->data.counter.name =
				lwc_string_ref(c->data.counter.name);
			break;
		case CSS_COMPUTED_CONTENT_COUNTERS:
			c->data.counters.name =
				lwc_string_ref(c->data.counters.name);
			c->data.counters.sep =
				lwc_string_ref(c->data.counters.sep);
			break;
		default:
			break;
		}
	}

	style->content = content;

	/* Free existing array */
	if (oldcontent != NULL) {
		for (c = oldcontent;
				c->type != CSS_COMPUTED_CONTENT_NONE; c++) {
			switch (c->type) {
			case CSS_COMPUTED_CONTENT_STRING:
				lwc_string_unref(c->data.string);
				break;
			case CSS_COMPUTED_CONTENT_URI:
				lwc_string_unref(c->data.uri);
				break;
			case CSS_COMPUTED_CONTENT_ATTR:
				lwc_string_unref(c->data.attr);
				break;
			case CSS_COMPUTED_CONTENT_COUNTER:
				lwc_string_unref(c->data.counter.name);
				break;
			case CSS_COMPUTED_CONTENT_COUNTERS:
				lwc_string_unref(c->data.counters.name);
				lwc_string_unref(c->data.counters.sep);
				break;
			default:
				break;
			}
		}

		if (oldcontent != content)
			free(oldcontent);
	}

	return CSS_OK;
}'''
