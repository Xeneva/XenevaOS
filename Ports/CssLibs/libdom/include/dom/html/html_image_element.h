/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */

#ifndef dom_html_image_element_h_
#define dom_html_image_element_h_

#include <stdbool.h>
#include <dom/inttypes.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

typedef struct dom_html_image_element dom_html_image_element;


dom_exception dom_html_image_element_get_name(
	dom_html_image_element *image, dom_string **name);

dom_exception dom_html_image_element_set_name(
	dom_html_image_element *image, dom_string *name);

dom_exception dom_html_image_element_get_align(
	dom_html_image_element *image, dom_string **align);

dom_exception dom_html_image_element_set_align(
	dom_html_image_element *image, dom_string *align);

dom_exception dom_html_image_element_get_alt(
	dom_html_image_element *image, dom_string **alt);

dom_exception dom_html_image_element_set_alt(
	dom_html_image_element *image, dom_string *alt);

dom_exception dom_html_image_element_get_border(
	dom_html_image_element *image, dom_string **border);

dom_exception dom_html_image_element_set_border(
	dom_html_image_element *image, dom_string *border);

dom_exception dom_html_image_element_get_long_desc(
	dom_html_image_element *image, dom_string **long_desc);

dom_exception dom_html_image_element_set_long_desc(
	dom_html_image_element *image, dom_string *long_desc);

dom_exception dom_html_image_element_get_src(
	dom_html_image_element *image, dom_string **src);

dom_exception dom_html_image_element_set_src(
	dom_html_image_element *image, dom_string *src);

dom_exception dom_html_image_element_get_use_map(
	dom_html_image_element *image, dom_string **use_map);

dom_exception dom_html_image_element_set_use_map(
	dom_html_image_element *image, dom_string *use_map);

dom_exception dom_html_image_element_get_height(
	dom_html_image_element *image, dom_ulong *height);

dom_exception dom_html_image_element_set_height(
	dom_html_image_element *image, dom_ulong height);

dom_exception dom_html_image_element_get_hspace(
	dom_html_image_element *image, dom_ulong *hspace);

dom_exception dom_html_image_element_set_hspace(
	dom_html_image_element *image, dom_ulong hspace);

dom_exception dom_html_image_element_get_vspace(
	dom_html_image_element *image, dom_ulong *vspace);

dom_exception dom_html_image_element_set_vspace(
	dom_html_image_element *image, dom_ulong vspace);

dom_exception dom_html_image_element_get_width(
	dom_html_image_element *image, dom_ulong *width);

dom_exception dom_html_image_element_set_width(
	dom_html_image_element *image, dom_ulong width);

dom_exception dom_html_image_element_get_is_map(
	dom_html_image_element *ele, bool *is_map);

dom_exception dom_html_image_element_set_is_map(
	dom_html_image_element *ele, bool is_map);

#endif
