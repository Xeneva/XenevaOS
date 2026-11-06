/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1access_keyr99@gmail.com>
 */

#ifndef dom_html_area_element_h_
#define dom_html_area_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

#include <dom/html/html_form_element.h>
#include <dom/html/html_document.h>

typedef struct dom_html_area_element dom_html_area_element;

dom_exception dom_html_area_element_get_access_key(
	dom_html_area_element *area, dom_string **access_key);

dom_exception dom_html_area_element_set_access_key(
	dom_html_area_element *area, dom_string *access_key);

dom_exception dom_html_area_element_get_alt(
	dom_html_area_element *area, dom_string **alt);

dom_exception dom_html_area_element_set_alt(
	dom_html_area_element *area, dom_string *alt);

dom_exception dom_html_area_element_get_coords(
	dom_html_area_element *area, dom_string **coords);

dom_exception dom_html_area_element_set_coords(
	dom_html_area_element *area, dom_string *coords);

dom_exception dom_html_area_element_get_href(
	dom_html_area_element *area, dom_string **href);

dom_exception dom_html_area_element_set_href(
	dom_html_area_element *area, dom_string *href);

dom_exception dom_html_area_element_get_no_href(
	dom_html_area_element *ele, bool *no_href);

dom_exception dom_html_area_element_set_no_href(
	dom_html_area_element *ele, bool no_href);

dom_exception dom_html_area_element_get_shape(
	dom_html_area_element *area, dom_string **shape);

dom_exception dom_html_area_element_set_shape(
	dom_html_area_element *area, dom_string *shape);

dom_exception dom_html_area_element_get_tab_index(
	dom_html_area_element *area, int32_t *tab_index);

dom_exception dom_html_area_element_set_tab_index(
	dom_html_area_element *area, uint32_t tab_index);

dom_exception dom_html_area_element_get_target(
	dom_html_area_element *area, dom_string **target);

dom_exception dom_html_area_element_set_target(
	dom_html_area_element *area, dom_string *target);

#endif
