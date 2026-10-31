/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku.com>
 */

#ifndef dom_html_base_element_h_
#define dom_html_base_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

typedef struct dom_html_base_element dom_html_base_element;

dom_exception dom_html_base_element_get_href(
		        dom_html_base_element *element, dom_string **href);

dom_exception dom_html_base_element_set_href(
		        dom_html_base_element *element, dom_string *href);

dom_exception dom_html_base_element_get_target(
		        dom_html_base_element *element, dom_string **target);

dom_exception dom_html_base_element_set_target(
		        dom_html_base_element *element, dom_string *target);

#endif

