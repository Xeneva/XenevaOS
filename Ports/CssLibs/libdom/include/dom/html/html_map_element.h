/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */
#ifndef dom_html_map_element_h_
#define dom_html_map_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

#include <dom/html/html_collection.h>

typedef struct dom_html_map_element dom_html_map_element;

dom_exception dom_html_map_element_get_name(
	dom_html_map_element *ele, dom_string **name);

dom_exception dom_html_map_element_set_name(
	dom_html_map_element *ele, dom_string *name);

dom_exception dom_html_map_element_get_areas(
	dom_html_map_element *ele, dom_html_collection **areas);

#endif
