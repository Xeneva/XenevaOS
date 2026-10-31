/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */

#ifndef dom_html_anchor_element_h_
#define dom_html_anchor_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

typedef struct dom_html_anchor_element dom_html_anchor_element;


dom_exception dom_html_anchor_element_get_access_key(
	dom_html_anchor_element *anchor, dom_string **access_key);

dom_exception dom_html_anchor_element_set_access_key(
	dom_html_anchor_element *anchor, dom_string *access_key);

dom_exception dom_html_anchor_element_get_charset(
	dom_html_anchor_element *anchor, dom_string **charset);

dom_exception dom_html_anchor_element_set_charset(
	dom_html_anchor_element *anchor, dom_string *charset);

dom_exception dom_html_anchor_element_get_coords(
	dom_html_anchor_element *anchor, dom_string **coords);

dom_exception dom_html_anchor_element_set_coords(
	dom_html_anchor_element *anchor, dom_string *coords);

dom_exception dom_html_anchor_element_get_href(
	dom_html_anchor_element *anchor, dom_string **href);

dom_exception dom_html_anchor_element_set_href(
	dom_html_anchor_element *anchor, dom_string *href);

dom_exception dom_html_anchor_element_get_hreflang(
	dom_html_anchor_element *anchor, dom_string **hreflang);

dom_exception dom_html_anchor_element_set_hreflang(
	dom_html_anchor_element *anchor, dom_string *hreflang);

dom_exception dom_html_anchor_element_get_name(
	dom_html_anchor_element *anchor, dom_string **name);

dom_exception dom_html_anchor_element_set_name(
	dom_html_anchor_element *anchor, dom_string *name);

dom_exception dom_html_anchor_element_get_rel(
	dom_html_anchor_element *anchor, dom_string **rel);

dom_exception dom_html_anchor_element_set_rel(
	dom_html_anchor_element *anchor, dom_string *rel);

dom_exception dom_html_anchor_element_get_rev(
	dom_html_anchor_element *anchor, dom_string **rev);

dom_exception dom_html_anchor_element_set_rev(
	dom_html_anchor_element *anchor, dom_string *rev);

dom_exception dom_html_anchor_element_get_shape(
	dom_html_anchor_element *anchor, dom_string **shape);

dom_exception dom_html_anchor_element_set_shape(
	dom_html_anchor_element *anchor, dom_string *shape);

dom_exception dom_html_anchor_element_get_target(
	dom_html_anchor_element *anchor, dom_string **target);

dom_exception dom_html_anchor_element_set_target(
	dom_html_anchor_element *anchor, dom_string *target);

dom_exception dom_html_anchor_element_get_type(
	dom_html_anchor_element *anchor, dom_string **type);

dom_exception dom_html_anchor_element_set_type(
	dom_html_anchor_element *anchor, dom_string *type);

dom_exception dom_html_anchor_element_get_tab_index(
	dom_html_anchor_element *anchor, int32_t *tab_index);

dom_exception dom_html_anchor_element_set_tab_index(
	dom_html_anchor_element *anchor, uint32_t tab_index);

dom_exception dom_html_anchor_element_blur(dom_html_anchor_element *ele);
dom_exception dom_html_anchor_element_focus(dom_html_anchor_element *ele);

#endif
