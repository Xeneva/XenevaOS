/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 */

/** \file
 * This is the top-level header file for libdom. The intention of this is
 * to allow client applications to simply include this file and get access
 * to all the libdom API.
 */

#ifndef dom_dom_h_
#define dom_dom_h_

/* Base library headers */
#include <dom/inttypes.h>
#include <dom/functypes.h>

/* DOM core headers */
#include <dom/core/attr.h>
#include <dom/core/characterdata.h>
#include <dom/core/document.h>
#include <dom/core/document_type.h>
#include <dom/core/element.h>
#include <dom/core/exceptions.h>
#include <dom/core/implementation.h>
#include <dom/core/namednodemap.h>
#include <dom/core/node.h>
#include <dom/core/cdatasection.h>
#include <dom/core/doc_fragment.h>
#include <dom/core/entity_ref.h>
#include <dom/core/nodelist.h>
#include <dom/core/tokenlist.h>
#include <dom/core/string.h>
#include <dom/core/text.h>
#include <dom/core/pi.h>
#include <dom/core/typeinfo.h>
#include <dom/core/comment.h>

/* DOM HTML headers */
#include <dom/html/html_collection.h>
#include <dom/html/html_document.h>
#include <dom/html/html_element.h>
#include <dom/html/html_html_element.h>
#include <dom/html/html_head_element.h>
#include <dom/html/html_link_element.h>
#include <dom/html/html_title_element.h>
#include <dom/html/html_body_element.h>
#include <dom/html/html_meta_element.h>
#include <dom/html/html_form_element.h>
#include <dom/html/html_input_element.h>
#include <dom/html/html_button_element.h>
#include <dom/html/html_canvas_element.h>
#include <dom/html/html_text_area_element.h>
#include <dom/html/html_opt_group_element.h>
#include <dom/html/html_option_element.h>
#include <dom/html/html_select_element.h>
#include <dom/html/html_options_collection.h>
#include <dom/html/html_hr_element.h>
#include <dom/html/html_dlist_element.h>
#include <dom/html/html_directory_element.h>
#include <dom/html/html_menu_element.h>
#include <dom/html/html_fieldset_element.h>
#include <dom/html/html_legend_element.h>
#include <dom/html/html_div_element.h>
#include <dom/html/html_paragraph_element.h>
#include <dom/html/html_heading_element.h>
#include <dom/html/html_quote_element.h>
#include <dom/html/html_pre_element.h>
#include <dom/html/html_br_element.h>
#include <dom/html/html_label_element.h>
#include <dom/html/html_ulist_element.h>
#include <dom/html/html_olist_element.h>
#include <dom/html/html_li_element.h>
#include <dom/html/html_font_element.h>
#include <dom/html/html_mod_element.h>
#include <dom/html/html_anchor_element.h>
#include <dom/html/html_basefont_element.h>
#include <dom/html/html_image_element.h>
#include <dom/html/html_object_element.h>
#include <dom/html/html_param_element.h>
#include <dom/html/html_applet_element.h>
#include <dom/html/html_area_element.h>
#include <dom/html/html_map_element.h>
#include <dom/html/html_script_element.h>
#include <dom/html/html_tablecaption_element.h>
#include <dom/html/html_tablecell_element.h>
#include <dom/html/html_tablecol_element.h>
#include <dom/html/html_tablesection_element.h>
#include <dom/html/html_table_element.h>
#include <dom/html/html_tablerow_element.h>
#include <dom/html/html_base_element.h>
#include <dom/html/html_style_element.h>
#include <dom/html/html_frameset_element.h>
#include <dom/html/html_frame_element.h>
#include <dom/html/html_iframe_element.h>
#include <dom/html/html_isindex_element.h>

/* DOM Events header */
#include <dom/events/events.h>

typedef enum dom_namespace {
	DOM_NAMESPACE_NULL    = 0,
	DOM_NAMESPACE_HTML    = 1,
	DOM_NAMESPACE_MATHML  = 2,
	DOM_NAMESPACE_SVG     = 3,
	DOM_NAMESPACE_XLINK   = 4,
	DOM_NAMESPACE_XML     = 5,
	DOM_NAMESPACE_XMLNS   = 6,

	DOM_NAMESPACE_COUNT   = 7
} dom_namespace;

/* Note, these are not valid until at least one function related to DOM
 * namespaces has been called such as the creation of a Document.
 */
extern dom_string *dom_namespaces[DOM_NAMESPACE_COUNT];

/* Optional client-callable namespace cleanup function */
extern dom_exception dom_namespace_finalise(void);

#endif
