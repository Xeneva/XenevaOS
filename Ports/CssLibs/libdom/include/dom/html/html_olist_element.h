/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */
#ifndef dom_html_olist_element_h_
#define dom_html_olist_element_h_

#include <stdbool.h>
#include <dom/inttypes.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

typedef struct dom_html_olist_element dom_html_olist_element;

dom_exception dom_html_olist_element_get_compact(
	dom_html_olist_element *ele, bool *compact);

dom_exception dom_html_olist_element_set_compact(
	dom_html_olist_element *ele, bool compact);

dom_exception dom_html_olist_element_get_start(
	dom_html_olist_element *ele, dom_long *start);

dom_exception dom_html_olist_element_set_start(
	dom_html_olist_element *ele, dom_long start);

dom_exception dom_html_olist_element_get_type(
	dom_html_olist_element *ele, dom_string **type);

dom_exception dom_html_olist_element_set_type(
	dom_html_olist_element *ele, dom_string *type);

#endif
