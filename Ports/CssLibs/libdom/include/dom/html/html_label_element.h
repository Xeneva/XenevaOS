/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 */

#ifndef dom_html_label_element_h_
#define dom_html_label_element_h_

#include <stdbool.h>

#include <dom/core/exceptions.h>

#include <dom/html/html_form_element.h>

typedef struct dom_html_label_element dom_html_label_element;

dom_exception dom_html_label_element_get_access_key(
		dom_html_label_element *ele, dom_string **access_key);
dom_exception dom_html_label_element_set_access_key(
		dom_html_label_element *ele, dom_string *access_key);

dom_exception dom_html_label_element_get_html_for(
		dom_html_label_element *ele, dom_string **html_for);
dom_exception dom_html_label_element_set_html_for(
		dom_html_label_element *ele, dom_string *html_for);

dom_exception dom_html_label_element_get_form(
		dom_html_label_element *ele, dom_html_form_element **form);

#endif

