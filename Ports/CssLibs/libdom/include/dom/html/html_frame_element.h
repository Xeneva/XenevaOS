/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */
#ifndef dom_html_frame_element_h_
#define dom_html_frame_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

#include <dom/html/html_document.h>
typedef struct dom_html_frame_element dom_html_frame_element;

dom_exception dom_html_frame_element_get_frame_border(
	dom_html_frame_element *element, dom_string **frame_border);

dom_exception dom_html_frame_element_set_frame_border(
	dom_html_frame_element *element, dom_string *frame_border);

dom_exception dom_html_frame_element_set_long_desc(
	dom_html_frame_element *ele, dom_string *long_desc);

dom_exception dom_html_frame_element_get_long_desc(
	dom_html_frame_element *ele, dom_string **long_desc);

dom_exception dom_html_frame_element_get_margin_height(
	dom_html_frame_element *element, dom_string **margin_height);

dom_exception dom_html_frame_element_set_margin_height(
	dom_html_frame_element *element, dom_string *margin_height);

dom_exception dom_html_frame_element_get_margin_width(
	dom_html_frame_element *element, dom_string **margin_width);

dom_exception dom_html_frame_element_set_margin_width(
	dom_html_frame_element *element, dom_string *margin_width);

dom_exception dom_html_frame_element_get_name(
	dom_html_frame_element *element, dom_string **name);

dom_exception dom_html_frame_element_set_name(
	dom_html_frame_element *element, dom_string *name);

dom_exception dom_html_frame_element_get_scrolling(
	dom_html_frame_element *element, dom_string **scrolling);

dom_exception dom_html_frame_element_set_scrolling(
	dom_html_frame_element *element, dom_string *scrolling);

dom_exception dom_html_frame_element_get_src(
	dom_html_frame_element *element, dom_string **src);

dom_exception dom_html_frame_element_set_src(
	dom_html_frame_element *element, dom_string *src);

dom_exception dom_html_frame_element_set_no_resize(dom_html_frame_element *ele,
		                                bool no_resize);

dom_exception dom_html_frame_element_get_no_resize(dom_html_frame_element *ele,
		                                bool *no_resize);

dom_exception dom_html_frame_element_get_content_document(
		dom_html_frame_element *ele,
		dom_document **content_document);

#endif
