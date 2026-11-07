/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */
#ifndef dom_html_base_font_element_h_
#define dom_html_base_font_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

typedef struct dom_html_base_font_element dom_html_base_font_element;

dom_exception dom_html_base_font_element_get_size(
	dom_html_base_font_element *element, int32_t *size);

dom_exception dom_html_base_font_element_set_size(
	dom_html_base_font_element *element, uint32_t size);

dom_exception dom_html_base_font_element_get_color(
			dom_html_base_font_element *element, dom_string **color);

dom_exception dom_html_base_font_element_set_color(
			dom_html_base_font_element *element, dom_string *color);

dom_exception dom_html_base_font_element_get_face(
			dom_html_base_font_element *element, dom_string **face);

dom_exception dom_html_base_font_element_set_face(
			dom_html_base_font_element *element, dom_string *face);
#endif
