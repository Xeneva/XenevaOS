/**
 * libhubbub/libdom -> NsCssNode bridge.  See dom.h.
 */

#include "dom.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <_xeneva.h>

#include <dom/dom.h>
/* The libdom source tree keeps the hubbub binding outside include/dom;
 * quote it by path so <hubbub/errors.h> still resolves to libhubbub. */
#include "../../Ports/CssLibs/libdom/bindings/hubbub/parser.h"

static char* ns_dup(const char* s, size_t len) {
    char* out = (char*)malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, s, len);
    out[len] = 0;
    return out;
}

static void ns_lower(char* s) {
    if (!s)
        return;
    for (; *s; s++) {
        if (*s >= 'A' && *s <= 'Z')
            *s = (char)(*s + 32);
    }
}

static void ns_copy_dom_string(dom_string* str, char* out, size_t outsz) {
    size_t len;
    if (!out || outsz == 0)
        return;
    out[0] = 0;
    if (!str)
        return;
    len = dom_string_byte_length(str);
    if (len >= outsz)
        len = outsz - 1;
    memcpy(out, dom_string_data(str), len);
    out[len] = 0;
}

static NsCssNode* ns_build_node(dom_node* dn, NsCssNode* parent);

static void ns_copy_attributes(dom_node* dn, NsCssNode* n) {
    dom_namednodemap* map = NULL;
    uint32_t len = 0;
    uint32_t i;
    if (dom_node_get_attributes(dn, &map) != DOM_NO_ERR || !map)
        return;
    dom_namednodemap_get_length(map, &len);
    for (i = 0; i < len; i++) {
        dom_node* attrnode = NULL;
        dom_string* name = NULL;
        dom_string* value = NULL;
        char aname[NS_CSS_ATTR_NAME_LEN + 1];
        char aval[2048];
        if (dom_namednodemap_item(map, i, &attrnode) != DOM_NO_ERR || !attrnode)
            continue;
        if (dom_attr_get_name((dom_attr*)attrnode, &name) == DOM_NO_ERR && name) {
            ns_copy_dom_string(name, aname, sizeof(aname));
            dom_string_unref(name);
            ns_lower(aname);
            if (dom_attr_get_value((dom_attr*)attrnode, &value) == DOM_NO_ERR && value) {
                ns_copy_dom_string(value, aval, sizeof(aval));
                dom_string_unref(value);
            } else {
                aval[0] = 0;
            }
            if (aname[0])
                NsCssNodeAddAttribute(n, aname, aval);
            if (!strcmp(aname, "class")) {
                char* p = aval;
                while (*p) {
                    char* start;
                    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f')
                        p++;
                    start = p;
                    while (*p && !(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f'))
                        p++;
                    if (p > start) {
                        char* tok = ns_dup(start, (size_t)(p - start));
                        if (tok) {
                            NsCssNodeAddClass(n, tok);
                            free(tok);
                        }
                    }
                }
            } else if (!strcmp(aname, "id")) {
                NsCssNodeSetId(n, aval);
            } else if (!strcmp(aname, "style")) {
                NsCssNodeSetInline(n, aval);
            } else if (!strcmp(aname, "href") && !strcmp(n->tag, "a")) {
                NsCssNodeSetLink(n, true);
            }
        }
        dom_node_unref(attrnode);
    }
    dom_namednodemap_unref(map);
}

static NsCssNode* ns_build_element(dom_node* dn, NsCssNode* parent) {
    dom_string* tag = NULL;
    char name[NS_CSS_TAG_LEN + 1];
    NsCssNode* n;
    dom_node* child = NULL;

    if (dom_element_get_tag_name((dom_element*)dn, &tag) != DOM_NO_ERR || !tag)
        return NULL;
    ns_copy_dom_string(tag, name, sizeof(name));
    dom_string_unref(tag);
    ns_lower(name);
    if (!name[0])
        strcpy(name, "div");

    n = NsCssNodeCreate(parent, name);
    if (!n)
        return NULL;
    ns_copy_attributes(dn, n);
    /* Ancestors must be selected before children so inheritance
     * composes correctly. */
    NsCssSelect(n);

    if (dom_node_get_first_child(dn, &child) == DOM_NO_ERR && child) {
        while (child) {
            dom_node* next = NULL;
            ns_build_node(child, n);
            dom_node_get_next_sibling(child, &next);
            dom_node_unref(child);
            child = next;
        }
    }
    return n;
}

/* A few pages (Google's language chooser is the common one) render a
 * script our bitmap font does not cover.  Keep the link useful by using
 * the English language name carried in its hl= parameter. */
static const char* ns_language_name(const char* id) {
    static const struct {
        const char* id;
        const char* name;
    } kLang[] = {{"hi", "Hindi"},     {"bn", "Bengali"}, {"te", "Telugu"},
                 {"mr", "Marathi"},   {"ta", "Tamil"},   {"gu", "Gujarati"},
                 {"kn", "Kannada"},   {"ml", "Malayalam"}, {"pa", "Punjabi"},
                 {"ur", "Urdu"},      {"ar", "Arabic"},  {"ru", "Russian"},
                 {"ja", "Japanese"},  {"ko", "Korean"},  {"zh-cn", "Chinese"},
                 {"zh-tw", "Chinese"}, {"de", "German"}, {"fr", "French"},
                 {"es", "Spanish"},   {"pt", "Portuguese"}, {"it", "Italian"},
                 {"nl", "Dutch"},     {"pl", "Polish"},  {NULL, NULL}};
    int i;
    if (!id)
        return NULL;
    for (i = 0; kLang[i].id; i++) {
        size_t n = strlen(kLang[i].id);
        if (!strncmp(id, kLang[i].id, n) &&
            (id[n] == 0 || id[n] == '&' || id[n] == '#'))
            return kLang[i].name;
    }
    return NULL;
}

static const char* ns_text_latin_fallback(NsCssNode* parent, const char* text, size_t len) {
    const char* href;
    const char* hl;
    size_t i;
    int has_ascii = 0;
    int has_high = 0;
    if (!parent || parent->is_text || strcmp(parent->tag, "a") || !text || len == 0)
        return NULL;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 0x80)
            has_high = 1;
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            has_ascii = 1;
    }
    if (!has_high || has_ascii)
        return NULL;
    href = NsCssNodeAttr(parent, "href");
    if (!href)
        return NULL;
    hl = strstr(href, "hl=");
    if (!hl)
        return NULL;
    return ns_language_name(hl + 3);
}

static NsCssNode* ns_build_node(dom_node* dn, NsCssNode* parent) {
    dom_node_type type;
    dom_string* value = NULL;
    NsCssNode* n = NULL;

    if (!dn)
        return NULL;
    if (dom_node_get_node_type(dn, &type) != DOM_NO_ERR)
        return NULL;
    if (type == DOM_ELEMENT_NODE) {
        n = ns_build_element(dn, parent);
        if (n && parent)
            NsCssNodeAppendChild(parent, n);
        return n;
    }
    if (type == DOM_TEXT_NODE) {
        if (dom_node_get_node_value(dn, &value) == DOM_NO_ERR && value) {
            size_t len = dom_string_byte_length(value);
            if (len > 0) {
                const char* fallback = ns_text_latin_fallback(parent, dom_string_data(value), len);
                if (fallback)
                    n = NsCssNodeCreateText(parent, fallback, strlen(fallback));
                else
                    n = NsCssNodeCreateText(parent, dom_string_data(value), len);
                if (n && parent)
                    NsCssNodeAppendChild(parent, n);
            }
            dom_string_unref(value);
        }
        return n;
    }
    /* Comments, doctype and PI nodes carry no layout. */
    return NULL;
}

NsCssNode* NsDomParse(const char* html, size_t len) {
    dom_hubbub_parser_params params;
    dom_hubbub_parser* parser = NULL;
    dom_document* doc = NULL;
    dom_hubbub_error err;
    dom_node* root = NULL;
    NsCssNode* out = NULL;

    if (!html || len == 0)
        return NULL;
    memset(&params, 0, sizeof(params));
    params.enc = "UTF-8";
    params.fix_enc = true;
    params.enable_script = false;
    params.script = NULL;
    params.msg = NULL;
    params.ctx = NULL;
    params.daf = NULL;

    err = dom_hubbub_parser_create(&params, &parser, &doc);
    if (err != DOM_HUBBUB_OK || !doc) {
        _KePrint("[dom] parser create failed %d\n", (int)err);
        return NULL;
    }
    err = dom_hubbub_parser_parse_chunk(parser, (const uint8_t*)html, len);
    if (err != DOM_HUBBUB_OK)
        _KePrint("[dom] parse chunk %d\n", (int)err);
    dom_hubbub_parser_completed(parser);
    dom_hubbub_parser_destroy(parser);

    if (dom_document_get_document_element(doc, &root) == DOM_NO_ERR && root) {
        out = ns_build_element(root, NULL);
        dom_node_unref(root);
    } else {
        _KePrint("[dom] no document element\n");
    }
    dom_node_unref((dom_node*)doc);
    if (out)
        _KePrint("[dom] parsed %s\n", out->tag);
    return out;
}

const char* NsDomAttr(NsCssNode* n, const char* name) {
    return NsCssNodeAttr(n, name);
}

int NsDomIsText(NsCssNode* n) {
    return n && n->is_text;
}

const char* NsDomTag(NsCssNode* n) {
    return n ? n->tag : "";
}
