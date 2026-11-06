/**
 * DOM parsing for the XenevaOS NetSurf renderer.
 *
 * libhubbub builds a real HTML5 tree in libdom (implied html/head/body,
 * entity expansion, foster parenting, malformed-tag recovery).  We copy
 * that tree into NsCssNode so the existing libcss select client can run
 * over attributes, classes, siblings and text nodes.
 */

#ifndef NS_DOM_H
#define NS_DOM_H

#include <stddef.h>
#include "css.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parse an HTML document and return the <html> subtree.  Styles are
 * selected as the tree is built, so the caller must have called
 * NsCssBegin()/NsCssAddSheet() first.  Returns NULL on failure. */
NsCssNode* NsDomParse(const char* html, size_t len);

/* Convenience helpers used by the layout engine. */
const char* NsDomAttr(NsCssNode* n, const char* name);
int NsDomIsText(NsCssNode* n);
const char* NsDomTag(NsCssNode* n);

#ifdef __cplusplus
}
#endif

#endif
