/**
 * NetSurf CSS wrapper implementation. See css.h.
 *
 * Follows libcss test/select.c: stylesheet params, select handler vtable,
 * select-then-compose walk, UA default hints. Unsupported selectors (sibling
 * combinators, dynamic pseudo-classes, ...) report no-match, which is legal:
 * those rules simply don't apply.
 */

#include "css.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <_xeneva.h>

#include <libcss/libcss.h>
#include <libcss/stylesheet.h>
#include <libcss/select.h>
#include <libcss/computed.h>
#include <libcss/font_face.h>
#include <libcss/hint.h>
#include <libwapcaplet/libwapcaplet.h>

#define NS_CSS_BASE_PX 14

static css_select_ctx* s_select = NULL;
static css_stylesheet* s_uaSheet = NULL;
static css_stylesheet* s_authorSheet = NULL;
static css_media s_media;
static css_unit_ctx s_unitCtx;

static lwc_string* s_attrClass = NULL;
static lwc_string* s_attrId = NULL;

/* UA reader defaults. This is a compact HTML4/CSS2.1 sheet: enough for
 * real pages to lay out as documents rather than as a pile of words. */
static const char kUaCss[] =
    "html{display:block;color:#202124;background-color:#ffffff}"
    "body{display:block;margin:8px;color:#202124;background-color:#ffffff}"
    "head,script,style,title,meta,link,base,template,param{display:none}"
    "noscript{display:block}"
    "div,p,section,article,header,footer,nav,main,aside,figure,figcaption,"
    "blockquote,pre,form,fieldset,address,dl,ul,ol,li,h1,h2,h3,h4,h5,h6,"
    "table,thead,tbody,tfoot,tr,hr,center,details,summary{display:block}"
    "li{display:list-item;margin:2px 0}"
    "table{display:table;border-collapse:separate;border-spacing:2px}"
    "thead{display:table-header-group}tbody{display:table-row-group}"
    "tfoot{display:table-footer-group}tr{display:table-row}"
    "td,th{display:table-cell;padding:1px;vertical-align:top}"
    "span,a,b,i,em,strong,code,small,sub,sup,label,abbr,cite,q,u,s,"
    "strike,font,big,tt,time,mark{display:inline}"
    "center{text-align:center}"
    "a{color:#1a0dab;text-decoration:underline}"
    "a:visited{color:#551a8b}"
    "h1{font-size:28px;font-weight:bold;margin:11px 0}"
    "h2{font-size:22px;font-weight:bold;margin:11px 0}"
    "h3{font-size:18px;font-weight:bold;margin:10px 0}"
    "h4{font-size:16px;font-weight:bold;margin:10px 0}"
    "h5{font-size:14px;font-weight:bold;margin:10px 0}"
    "h6{font-size:13px;font-weight:bold;margin:10px 0}"
    "p{margin:11px 0}"
    "blockquote{margin:11px 40px}"
    "ul,ol{margin:11px 0;padding-left:36px}ul{list-style-type:disc}ol{list-style-type:decimal}"
    "dl{margin:11px 0}dt{font-weight:bold}dd{margin-left:36px}"
    "pre{white-space:pre;margin:11px 0;font-family:monospace}"
    "code,tt,kbd,samp{font-family:monospace}"
    "hr{border:0;border-top:1px solid #c8c8c8;margin:9px 0;height:0}"
    "img{display:inline-block}"
    "input,button,select,textarea{display:inline-block}"
    "input[type=hidden]{display:none}"
    "strong,b{font-weight:bold}"
    "i,em,cite,var{font-style:italic}"
    "small{font-size:11px;color:#5f6368}"
    "sub,sup{font-size:11px}"
    "u,ins{text-decoration:underline}s,strike,del{text-decoration:line-through}"
    "mark{background-color:#fff2a8}"
    "fieldset{border:1px solid #b8b8b8;margin:0 2px;padding:6px 10px}"
    "legend{padding:0 4px;font-weight:bold}"
    "button{background-color:#e9e9e9;border:1px solid #8a8a8a;padding:2px 8px}"
    "input{border:1px solid #8a8a8a;background-color:#ffffff;padding:1px 3px}"
    "textarea{border:1px solid #8a8a8a;background-color:#ffffff;padding:2px}"
    "select{border:1px solid #8a8a8a;background-color:#ffffff;padding:1px}"
    "table[border],table[border] td,table[border] th{border:1px solid #b0b0b0}"
    "th{font-weight:bold;text-align:center}";

static css_error ns_resolve_url(void* pw, const char* base, lwc_string* rel, lwc_string** abs) {
    (void)pw;
    (void)base;
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

/* System fonts degrade to fixed reader values. */
static css_error ns_resolve_font(void* pw, lwc_string* name, css_system_font* system_font) {
    (void)pw;
    (void)name;
    if (!system_font)
        return CSS_BADPARM;
    system_font->style = CSS_FONT_STYLE_NORMAL;
    system_font->variant = CSS_FONT_VARIANT_NORMAL;
    system_font->weight = CSS_FONT_WEIGHT_NORMAL;
    system_font->size.size = NS_CSS_BASE_PX * (1 << CSS_RADIX_POINT);
    system_font->size.unit = CSS_UNIT_PX;
    system_font->line_height.size = (NS_CSS_BASE_PX + 5) * (1 << CSS_RADIX_POINT);
    system_font->line_height.unit = CSS_UNIT_PX;
    return CSS_OK;
}

static int ns_make_sheet(const char* data, size_t len, bool inlineStyle, css_stylesheet** out) {
    css_stylesheet_params params;
    css_stylesheet* sheet = NULL;

    memset(&params, 0, sizeof(params));
    params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
    params.level = CSS_LEVEL_21;
    params.charset = "UTF-8";
    params.url = "xeneva";
    params.title = "xeneva";
    params.allow_quirks = false;
    params.inline_style = inlineStyle;
    params.resolve = ns_resolve_url;
    params.resolve_pw = NULL;
    params.import = NULL;
    params.import_pw = NULL;
    params.color = NULL;
    params.color_pw = NULL;
    params.font = ns_resolve_font;
    params.font_pw = NULL;

    if (css_stylesheet_create(&params, &sheet) != CSS_OK)
        return -1;
    _KePrint("[css] sheet created\n");
    if (len > 0) {
        /* NEEDDATA is the normal "streaming" return, not an error:
         * data_done() finalises the sheet. */
        css_error err = css_stylesheet_append_data(sheet, (const uint8_t*)data, len);
        if (err != CSS_OK && err != CSS_NEEDDATA) {
            css_stylesheet_destroy(sheet);
            return -1;
        }
    }
    if (css_stylesheet_data_done(sheet) != CSS_OK) {
        css_stylesheet_destroy(sheet);
        return -1;
    }
    _KePrint("[css] sheet done\n");
    *out = sheet;
    return 0;
}

int NsCssBegin(void) {
    NsCssEnd();
    memset(&s_media, 0, sizeof(s_media));
    s_media.type = CSS_MEDIA_SCREEN;
    memset(&s_unitCtx, 0, sizeof(s_unitCtx));
    s_unitCtx.font_size_default = NS_CSS_BASE_PX * (1 << CSS_RADIX_POINT);

    if (css_select_ctx_create(&s_select) != CSS_OK)
        return -1;
    _KePrint("[css] ctx ok\n");
    if (ns_make_sheet(kUaCss, sizeof(kUaCss) - 1, false, &s_uaSheet) != 0)
        return -1;
    _KePrint("[css] ua ok\n");
    if (css_select_ctx_append_sheet(s_select, s_uaSheet, CSS_ORIGIN_UA, "screen") != CSS_OK)
        return -1;
    if (lwc_intern_string("class", 5, &s_attrClass) != lwc_error_ok)
        return -1;
    if (lwc_intern_string("id", 2, &s_attrId) != lwc_error_ok)
        return -1;
    return 0;
}

void NsCssSetViewport(int width, int height) {
    if (width <= 0)
        width = 640;
    if (height <= 0)
        height = 420;
    s_media.width = (css_fixed)((int64_t)width * (1 << CSS_RADIX_POINT));
    s_media.height = (css_fixed)((int64_t)height * (1 << CSS_RADIX_POINT));
    s_media.orientation = width >= height ? CSS_MEDIA_ORIENTATION_LANDSCAPE
                                          : CSS_MEDIA_ORIENTATION_PORTRAIT;
    s_media.aspect_ratio = height > 0
        ? (css_fixed)(((int64_t)width * (1 << CSS_RADIX_POINT)) / height)
        : 0;
}

int NsCssAddSheet(const char* data, size_t len) {
    css_stylesheet* sheet = NULL;
    if (!s_select || !data || len == 0)
        return -1;
    if (s_authorSheet) {
        css_select_ctx_remove_sheet(s_select, s_authorSheet);
        css_stylesheet_destroy(s_authorSheet);
        s_authorSheet = NULL;
    }
    if (ns_make_sheet(data, len, false, &sheet) != 0)
        return -1;
    if (css_select_ctx_append_sheet(s_select, sheet, CSS_ORIGIN_AUTHOR, "screen") != CSS_OK) {
        css_stylesheet_destroy(sheet);
        return -1;
    }
    s_authorSheet = sheet;
    return 0;
}

void NsCssEnd(void) {
    if (s_select) {
        if (s_authorSheet) {
            css_select_ctx_remove_sheet(s_select, s_authorSheet);
            css_stylesheet_destroy(s_authorSheet);
            s_authorSheet = NULL;
        }
        if (s_uaSheet) {
            css_select_ctx_remove_sheet(s_select, s_uaSheet);
            css_stylesheet_destroy(s_uaSheet);
            s_uaSheet = NULL;
        }
        css_select_ctx_destroy(s_select);
        s_select = NULL;
    }
    if (s_attrClass) {
        lwc_string_unref(s_attrClass);
        s_attrClass = NULL;
    }
    if (s_attrId) {
        lwc_string_unref(s_attrId);
        s_attrId = NULL;
    }
}

/* ---- nodes ---- */

NsCssNode* NsCssNodeCreate(NsCssNode* parent, const char* tag) {
    NsCssNode* n = (NsCssNode*)malloc(sizeof(NsCssNode));
    if (!n)
        return NULL;
    memset(n, 0, sizeof(*n));
    n->parent = parent;
    strncpy(n->tag, tag ? tag : "div", NS_CSS_TAG_LEN);
    n->tag[NS_CSS_TAG_LEN] = 0;
    if (lwc_intern_string(n->tag, strlen(n->tag), (lwc_string**)&n->lwcName) != lwc_error_ok) {
        free(n);
        return NULL;
    }
    return n;
}

static char* ns_strdup_n(const char* s, size_t len) {
    char* out;
    if (!s)
        return NULL;
    out = (char*)malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, s, len);
    out[len] = 0;
    return out;
}

NsCssNode* NsCssNodeCreateText(NsCssNode* parent, const char* text, size_t len) {
    NsCssNode* n;
    if (!text)
        return NULL;
    n = (NsCssNode*)malloc(sizeof(NsCssNode));
    if (!n)
        return NULL;
    memset(n, 0, sizeof(*n));
    n->parent = parent;
    n->is_text = true;
    strcpy(n->tag, "#text");
    n->text = ns_strdup_n(text, len);
    if (!n->text) {
        free(n);
        return NULL;
    }
    n->text_len = len;
    return n;
}

void NsCssNodeAppendChild(NsCssNode* parent, NsCssNode* child) {
    if (!parent || !child)
        return;
    child->parent = parent;
    child->prev = parent->last_child;
    child->next = NULL;
    if (parent->last_child)
        parent->last_child->next = child;
    else
        parent->first_child = child;
    parent->last_child = child;
}

void NsCssNodeAddAttribute(NsCssNode* n, const char* name, const char* value) {
    uint32_t i;
    size_t nlen;
    if (!n || n->is_text || !name || !name[0] || !value)
        return;
    nlen = strlen(name);
    if (nlen > NS_CSS_ATTR_NAME_LEN)
        nlen = NS_CSS_ATTR_NAME_LEN;
    for (i = 0; i < n->n_attrs; i++) {
        if (strlen(n->attrs[i].name) == nlen && !strncasecmp(n->attrs[i].name, name, nlen)) {
            free(n->attrs[i].value);
            n->attrs[i].value = ns_strdup_n(value, strlen(value));
            return;
        }
    }
    if (n->n_attrs >= NS_CSS_MAX_ATTRS)
        return;
    n->attrs[n->n_attrs].name = ns_strdup_n(name, nlen);
    n->attrs[n->n_attrs].value = ns_strdup_n(value, strlen(value));
    if (!n->attrs[n->n_attrs].name || !n->attrs[n->n_attrs].value) {
        free(n->attrs[n->n_attrs].name);
        free(n->attrs[n->n_attrs].value);
        return;
    }
    n->n_attrs++;
}

const char* NsCssNodeAttr(NsCssNode* n, const char* name) {
    uint32_t i;
    size_t nlen;
    if (!n || n->is_text || !name)
        return NULL;
    nlen = strlen(name);
    for (i = 0; i < n->n_attrs; i++) {
        if (strlen(n->attrs[i].name) == nlen && !strncasecmp(n->attrs[i].name, name, nlen))
            return n->attrs[i].value;
    }
    return NULL;
}


void NsCssNodeAddClass(NsCssNode* n, const char* cls) {
    if (!n || !cls || !cls[0] || n->n_cls >= NS_CSS_MAX_CLASSES)
        return;
    strncpy(n->cls[n->n_cls], cls, NS_CSS_CLASS_LEN);
    n->cls[n->n_cls][NS_CSS_CLASS_LEN] = 0;
    if (lwc_intern_string(n->cls[n->n_cls],
                          strlen(n->cls[n->n_cls]),
                          (lwc_string**)&n->lwcCls[n->n_cls]) == lwc_error_ok)
        n->n_cls++;
}

void NsCssNodeSetId(NsCssNode* n, const char* id) {
    if (!n || !id || !id[0])
        return;
    strncpy(n->id, id, NS_CSS_ID_LEN);
    n->id[NS_CSS_ID_LEN] = 0;
    lwc_intern_string(n->id, strlen(n->id), (lwc_string**)&n->lwcId);
}

void NsCssNodeSetLink(NsCssNode* n, bool link) {
    if (n)
        n->is_link = link;
}

int NsCssNodeSetInline(NsCssNode* n, const char* style) {
    css_stylesheet* sheet = NULL;
    if (!n)
        return -1;
    /* setAttribute("style","") clears an inline display:none. Leaving
     * the old sheet in place kept the element hidden. */
    if (!style || !style[0]) {
        if (n->inlineSheet) {
            css_stylesheet_destroy((css_stylesheet*)n->inlineSheet);
            n->inlineSheet = NULL;
        }
        return 0;
    }
    if (ns_make_sheet(style, strlen(style), true, &sheet) != 0)
        return -1;
    if (n->inlineSheet)
        css_stylesheet_destroy((css_stylesheet*)n->inlineSheet);
    n->inlineSheet = sheet;
    return 0;
}

void NsCssNodeDestroy(NsCssNode* n) {
    uint32_t i;
    if (!n)
        return;
    if (n->selectResults) {
        css_select_results_destroy((css_select_results*)n->selectResults);
        n->selectResults = NULL;
    }
    if (n->inlineSheet) {
        css_stylesheet_destroy((css_stylesheet*)n->inlineSheet);
        n->inlineSheet = NULL;
    }
    for (i = 0; i < n->n_attrs; i++) {
        free(n->attrs[i].name);
        free(n->attrs[i].value);
        n->attrs[i].name = NULL;
        n->attrs[i].value = NULL;
    }
    free(n->text);
    n->text = NULL;
    if (n->lwcName) {
        lwc_string_unref((lwc_string*)n->lwcName);
        n->lwcName = NULL;
    }
    for (i = 0; i < n->n_cls; i++) {
        if (n->lwcCls[i]) {
            lwc_string_unref((lwc_string*)n->lwcCls[i]);
            n->lwcCls[i] = NULL;
        }
    }
    if (n->lwcId) {
        lwc_string_unref((lwc_string*)n->lwcId);
        n->lwcId = NULL;
    }
    free(n);
}

void NsCssNodeDestroyTree(NsCssNode* n) {
    NsCssNode* child;
    if (!n)
        return;
    child = n->first_child;
    while (child) {
        NsCssNode* next = child->next;
        NsCssNodeDestroyTree(child);
        child = next;
    }
    NsCssNodeDestroy(n);
}

/* ---- select handler ---- */

static css_error ns_node_name(void* pw, void* node, css_qname* qname) {
    NsCssNode* n = (NsCssNode*)node;
    (void)pw;
    qname->ns = NULL;
    qname->name = lwc_string_ref((lwc_string*)n->lwcName);
    return CSS_OK;
}

static css_error ns_node_classes(void* pw, void* node, lwc_string*** classes, uint32_t* n_classes) {
    NsCssNode* n = (NsCssNode*)node;
    uint32_t i;
    (void)pw;
    *classes = (lwc_string**)n->lwcCls;
    *n_classes = n->n_cls;
    for (i = 0; i < *n_classes; i++)
        (*classes)[i] = lwc_string_ref((lwc_string*)n->lwcCls[i]);
    return CSS_OK;
}

static css_error ns_node_id(void* pw, void* node, lwc_string** id) {
    NsCssNode* n = (NsCssNode*)node;
    (void)pw;
    if (n->lwcId)
        *id = lwc_string_ref((lwc_string*)n->lwcId);
    else
        *id = NULL;
    return CSS_OK;
}

static css_error ns_named_ancestor(void* pw, void* node, const css_qname* qname, void** ancestor) {
    NsCssNode* n = ((NsCssNode*)node)->parent;
    (void)pw;
    for (; n; n = n->parent) {
        bool match = false;
        if (lwc_string_caseless_isequal(qname->name, (lwc_string*)n->lwcName, &match) != lwc_error_ok)
            return CSS_BADPARM;
        if (match)
            break;
    }
    *ancestor = n;
    return CSS_OK;
}

static css_error ns_named_parent(void* pw, void* node, const css_qname* qname, void** parent) {
    NsCssNode* n = ((NsCssNode*)node)->parent;
    bool match = false;
    (void)pw;
    if (!n) {
        *parent = NULL;
        return CSS_OK;
    }
    if (lwc_string_caseless_isequal(qname->name, (lwc_string*)n->lwcName, &match) != lwc_error_ok)
        return CSS_BADPARM;
    *parent = match ? n : NULL;
    return CSS_OK;
}

static css_error ns_no_node(void* pw, void* node, const css_qname* qname, void** out) {
    (void)pw;
    (void)node;
    (void)qname;
    *out = NULL;
    return CSS_OK;
}

static css_error ns_parent_node(void* pw, void* node, void** parent) {
    (void)pw;
    *parent = ((NsCssNode*)node)->parent;
    return CSS_OK;
}

/* Case-insensitive tag compare against an interned qname. */
static bool ns_qname_is_tag(const css_qname* qname, NsCssNode* n) {
    bool match = false;
    if (!qname || !n || n->is_text || !n->lwcName)
        return false;
    if (lwc_string_caseless_isequal(qname->name, (lwc_string*)n->lwcName, &match) != lwc_error_ok)
        return false;
    return match && qname->ns == NULL;
}

static css_error ns_named_sibling_node(void* pw, void* node, const css_qname* qname, void** sibling) {
    NsCssNode* n = ((NsCssNode*)node)->prev;
    (void)pw;
    while (n) {
        if (!n->is_text && ns_qname_is_tag(qname, n))
            break;
        n = n->prev;
    }
    *sibling = n;
    return CSS_OK;
}

static css_error ns_named_generic_sibling_node(
    void* pw, void* node, const css_qname* qname, void** sibling) {
    /* Generic sibling combinator scans every preceding element; the
     * candidate still has to match the qname. */
    return ns_named_sibling_node(pw, node, qname, sibling);
}

static css_error ns_sibling_node(void* pw, void* node, void** sibling) {
    NsCssNode* n = ((NsCssNode*)node)->prev;
    (void)pw;
    while (n && n->is_text)
        n = n->prev;
    *sibling = n;
    return CSS_OK;
}

static const char* ns_attr_value(NsCssNode* n, const css_qname* qname) {
    if (!n || n->is_text || !qname || qname->ns != NULL || !qname->name)
        return NULL;
    return NsCssNodeAttr(n, lwc_string_data(qname->name));
}

static bool ns_ascii_eq(const char* a, const char* b, bool ci) {
    if (!a || !b)
        return false;
    return ci ? (strcasecmp(a, b) == 0) : (strcmp(a, b) == 0);
}

static css_error ns_node_has_attribute(
    void* pw, void* node, const css_qname* qname, bool* match) {
    (void)pw;
    *match = ns_attr_value((NsCssNode*)node, qname) != NULL;
    return CSS_OK;
}

static css_error ns_node_has_attribute_equal(
    void* pw, void* node, const css_qname* qname, lwc_string* value, bool* match) {
    const char* attr;
    (void)pw;
    attr = ns_attr_value((NsCssNode*)node, qname);
    *match = attr && ns_ascii_eq(attr, lwc_string_data(value), false);
    return CSS_OK;
}

static css_error ns_node_has_attribute_dashmatch(
    void* pw, void* node, const css_qname* qname, lwc_string* value, bool* match) {
    const char* attr;
    const char* val;
    size_t vlen;
    (void)pw;
    attr = ns_attr_value((NsCssNode*)node, qname);
    val = lwc_string_data(value);
    vlen = lwc_string_length(value);
    *match = false;
    if (attr && val) {
        if (!strncmp(attr, val, vlen) && (attr[vlen] == 0 || attr[vlen] == '-'))
            *match = true;
    }
    return CSS_OK;
}

static css_error ns_node_has_attribute_includes(
    void* pw, void* node, const css_qname* qname, lwc_string* value, bool* match) {
    const char* attr;
    const char* val;
    size_t vlen;
    (void)pw;
    attr = ns_attr_value((NsCssNode*)node, qname);
    val = lwc_string_data(value);
    vlen = lwc_string_length(value);
    *match = false;
    if (!attr || !val || vlen == 0)
        return CSS_OK;
    while (*attr) {
        const char* start;
        size_t len;
        while (*attr == ' ' || *attr == '\t' || *attr == '\n' || *attr == '\r' || *attr == '\f')
            attr++;
        start = attr;
        while (*attr && !(*attr == ' ' || *attr == '\t' || *attr == '\n' || *attr == '\r' ||
                          *attr == '\f'))
            attr++;
        len = (size_t)(attr - start);
        if (len == vlen && !strncmp(start, val, vlen)) {
            *match = true;
            break;
        }
    }
    return CSS_OK;
}

static css_error ns_node_has_attribute_prefix(
    void* pw, void* node, const css_qname* qname, lwc_string* value, bool* match) {
    const char* attr;
    const char* val;
    size_t vlen;
    (void)pw;
    attr = ns_attr_value((NsCssNode*)node, qname);
    val = lwc_string_data(value);
    vlen = lwc_string_length(value);
    *match = attr && val && vlen > 0 && !strncmp(attr, val, vlen);
    return CSS_OK;
}

static css_error ns_node_has_attribute_suffix(
    void* pw, void* node, const css_qname* qname, lwc_string* value, bool* match) {
    const char* attr;
    const char* val;
    size_t alen;
    size_t vlen;
    (void)pw;
    attr = ns_attr_value((NsCssNode*)node, qname);
    val = lwc_string_data(value);
    vlen = lwc_string_length(value);
    *match = false;
    if (attr && val && vlen > 0) {
        alen = strlen(attr);
        if (alen >= vlen && !strcmp(attr + alen - vlen, val))
            *match = true;
    }
    return CSS_OK;
}

static css_error ns_node_has_attribute_substring(
    void* pw, void* node, const css_qname* qname, lwc_string* value, bool* match) {
    const char* attr;
    const char* val;
    (void)pw;
    attr = ns_attr_value((NsCssNode*)node, qname);
    val = lwc_string_data(value);
    *match = attr && val && val[0] && strstr(attr, val) != NULL;
    return CSS_OK;
}

static css_error ns_no_match_bool(void* pw, void* node, bool* match) {
    (void)pw;
    (void)node;
    *match = false;
    return CSS_OK;
}

static css_error ns_no_hints(void* pw, void* node, uint32_t* nhints, css_hint** hints) {
    (void)pw;
    (void)node;
    *nhints = 0;
    *hints = NULL;
    return CSS_OK;
}

static css_error ns_node_has_name(void* pw, void* node, const css_qname* qname, bool* match) {
    NsCssNode* n = (NsCssNode*)node;
    (void)pw;
    if (lwc_string_caseless_isequal(qname->name, (lwc_string*)n->lwcName, match) != lwc_error_ok)
        return CSS_BADPARM;
    return CSS_OK;
}

static css_error ns_node_has_class(void* pw, void* node, lwc_string* name, bool* match) {
    NsCssNode* n = (NsCssNode*)node;
    uint32_t i;
    (void)pw;
    *match = false;
    for (i = 0; i < n->n_cls; i++) {
        bool eq = false;
        if (lwc_string_isequal(name, (lwc_string*)n->lwcCls[i], &eq) != lwc_error_ok)
            return CSS_BADPARM;
        if (eq) {
            *match = true;
            break;
        }
    }
    return CSS_OK;
}

static css_error ns_node_has_id(void* pw, void* node, lwc_string* name, bool* match) {
    NsCssNode* n = (NsCssNode*)node;
    (void)pw;
    *match = false;
    if (n->lwcId) {
        if (lwc_string_isequal(name, (lwc_string*)n->lwcId, match) != lwc_error_ok)
            return CSS_BADPARM;
    }
    return CSS_OK;
}

static css_error ns_node_is_root(void* pw, void* node, bool* match) {
    (void)pw;
    *match = (((NsCssNode*)node)->parent == NULL);
    return CSS_OK;
}

static css_error ns_node_is_empty(void* pw, void* node, bool* match) {
    (void)pw;
    *match = ((NsCssNode*)node)->first_child == NULL;
    return CSS_OK;
}

static css_error ns_node_is_link(void* pw, void* node, bool* match) {
    (void)pw;
    *match = ((NsCssNode*)node)->is_link;
    return CSS_OK;
}

static css_error ns_node_count_siblings(void* pw, void* node, bool same_name, bool after, int32_t* count) {
    NsCssNode* n = (NsCssNode*)node;
    int32_t c = 0;
    (void)pw;
    n = after ? n->next : n->prev;
    for (; n; n = after ? n->next : n->prev) {
        if (n->is_text)
            continue;
        if (!same_name || !strcasecmp(n->tag, ((NsCssNode*)node)->tag))
            c++;
    }
    *count = c;
    return CSS_OK;
}

static css_error ns_node_is_lang(void* pw, void* node, lwc_string* lang, bool* match) {
    NsCssNode* n = (NsCssNode*)node;
    const char* want = lwc_string_data(lang);
    size_t wlen = lwc_string_length(lang);
    (void)pw;
    *match = false;
    if (!want || wlen == 0)
        return CSS_OK;
    for (; n; n = n->parent) {
        const char* have;
        if (n->is_text)
            continue;
        have = NsCssNodeAttr(n, "lang");
        if (!have)
            have = NsCssNodeAttr(n, "xml:lang");
        if (have) {
            if (!strncasecmp(have, want, wlen) && (have[wlen] == 0 || have[wlen] == '-'))
                *match = true;
            break;
        }
    }
    return CSS_OK;
}

static css_error ns_ua_default(void* pw, uint32_t property, css_hint* hint) {
    (void)pw;
    if (property == CSS_PROP_COLOR) {
        /* Unresolved var() drops the author color. White here painted
         * the whole page the same color as the background. */
        hint->data.color = 0xff202124u;
        hint->status = CSS_COLOR_COLOR;
    } else if (property == CSS_PROP_FONT_FAMILY) {
        hint->data.strings = NULL;
        hint->status = CSS_FONT_FAMILY_SANS_SERIF;
    } else if (property == CSS_PROP_QUOTES) {
        hint->data.strings = NULL;
        hint->status = CSS_QUOTES_NONE;
    } else if (property == CSS_PROP_VOICE_FAMILY) {
        hint->data.strings = NULL;
        hint->status = 0;
    } else {
        return CSS_INVALID;
    }
    return CSS_OK;
}

static css_error ns_set_node_data(void* pw, void* node, void* libcss_node_data) {
    (void)pw;
    (void)node;
    (void)libcss_node_data;
    return CSS_OK;
}

static css_error ns_get_node_data(void* pw, void* node, void** libcss_node_data) {
    (void)pw;
    (void)node;
    *libcss_node_data = NULL;
    return CSS_OK;
}

static css_select_handler s_handler = {
    CSS_SELECT_HANDLER_VERSION_1,
    ns_node_name,
    ns_node_classes,
    ns_node_id,
    ns_named_ancestor,
    ns_named_parent,
    ns_named_sibling_node,
    ns_named_generic_sibling_node,
    ns_parent_node,
    ns_sibling_node,
    ns_node_has_name,
    ns_node_has_class,
    ns_node_has_id,
    ns_node_has_attribute,
    ns_node_has_attribute_equal,
    ns_node_has_attribute_dashmatch,
    ns_node_has_attribute_includes,
    ns_node_has_attribute_prefix,
    ns_node_has_attribute_suffix,
    ns_node_has_attribute_substring,
    ns_node_is_root,
    ns_node_count_siblings,
    ns_node_is_empty,
    ns_node_is_link,
    ns_no_match_bool, /* visited */
    ns_no_match_bool, /* hover */
    ns_no_match_bool, /* active */
    ns_no_match_bool, /* focus */
    ns_no_match_bool, /* enabled */
    ns_no_match_bool, /* disabled */
    ns_no_match_bool, /* checked */
    ns_no_match_bool, /* target */
    ns_node_is_lang,
    ns_no_hints,
    ns_ua_default,
    ns_set_node_data,
    ns_get_node_data,
};

NsCssNode* NsCssNodeById(NsCssNode* n, const char* id) {
    NsCssNode* c;
    NsCssNode* found;
    if (!n || !id || !id[0])
        return NULL;
    if (!n->is_text && n->id[0] && !strcmp(n->id, id))
        return n;
    for (c = n->first_child; c; c = c->next) {
        found = NsCssNodeById(c, id);
        if (found)
            return found;
    }
    return NULL;
}

void NsCssReselectTree(NsCssNode* n) {
    NsCssNode* c;
    if (!n)
        return;
    if (!n->is_text)
        NsCssSelect(n);
    for (c = n->first_child; c; c = c->next)
        NsCssReselectTree(c);
}

int NsCssSelect(NsCssNode* n) {
    css_select_results* sr = NULL;
    if (!n || !s_select)
        return -1;
    if (n->parent == NULL)
        s_unitCtx.root_style = NULL;
    if (css_select_style(s_select,
                         n,
                         &s_unitCtx,
                         &s_media,
                         (const css_stylesheet*)n->inlineSheet,
                         &s_handler,
                         NULL,
                         &sr) != CSS_OK)
        return -1;
    if (n->parent != NULL && n->parent->selectResults != NULL) {
        css_computed_style* composed = NULL;
        css_computed_style* parentStyle =
            ((css_select_results*)n->parent->selectResults)->styles[CSS_PSEUDO_ELEMENT_NONE];
        if (css_computed_style_compose(parentStyle,
                                       sr->styles[CSS_PSEUDO_ELEMENT_NONE],
                                       &s_unitCtx,
                                       &composed) == CSS_OK) {
            css_computed_style_destroy(sr->styles[CSS_PSEUDO_ELEMENT_NONE]);
            sr->styles[CSS_PSEUDO_ELEMENT_NONE] = composed;
        }
    }
    if (n->selectResults)
        css_select_results_destroy((css_select_results*)n->selectResults);
    n->selectResults = sr;
    if (n->parent == NULL)
        s_unitCtx.root_style = sr->styles[CSS_PSEUDO_ELEMENT_NONE];
    return 0;
}

/* ---- computed value readers ---- */

static css_computed_style* ns_style(NsCssNode* n) {
    if (!n || !n->selectResults)
        return NULL;
    return ((css_select_results*)n->selectResults)->styles[CSS_PSEUDO_ELEMENT_NONE];
}

uint32_t NsCssNodeColor(NsCssNode* n) {
    css_computed_style* st = ns_style(n);
    if (!st)
        return 0xffffffffu;
    {
        css_color color = 0;
        uint8_t type = css_computed_color(st, &color);
        if (type == CSS_COLOR_COLOR)
            return (uint32_t)color;
    }
    /* A missing computed color used to be white. On the white page that
     * made every glyph invisible except the green HTTPS note. */
    return 0xff202124u;
}

static int ns_len_px(css_fixed length, css_unit unit, int basePx) {
    /* css_fixed has CSS_RADIX_POINT fractional bits. */
    if (unit == CSS_UNIT_PX) {
        return (int)(length >> CSS_RADIX_POINT);
    } else if (unit == CSS_UNIT_PT) {
        return (int)((length >> CSS_RADIX_POINT) * 4 / 3);
    } else if (unit == CSS_UNIT_PC) {
        return (int)((length >> CSS_RADIX_POINT) * 16);
    } else if (unit == CSS_UNIT_EM || unit == CSS_UNIT_REM || unit == CSS_UNIT_EX) {
        return (int)(length >> CSS_RADIX_POINT) * basePx;
    }
    return -1;
}

int NsCssNodeFontPx(NsCssNode* n) {
    css_computed_style* st = ns_style(n);
    if (!st)
        return NS_CSS_BASE_PX;
    {
        css_fixed length = 0;
        css_unit unit = CSS_UNIT_PX;
        uint8_t type = css_computed_font_size(st, &length, &unit);
        switch (type) {
        case CSS_FONT_SIZE_XX_SMALL:
            return 9;
        case CSS_FONT_SIZE_X_SMALL:
            return 10;
        case CSS_FONT_SIZE_SMALL:
            return 12;
        case CSS_FONT_SIZE_MEDIUM:
            return NS_CSS_BASE_PX;
        case CSS_FONT_SIZE_LARGE:
            return 17;
        case CSS_FONT_SIZE_X_LARGE:
            return 20;
        case CSS_FONT_SIZE_XX_LARGE:
            return 24;
        case CSS_FONT_SIZE_LARGER:
            return NS_CSS_BASE_PX + 2;
        case CSS_FONT_SIZE_SMALLER:
            return NS_CSS_BASE_PX - 2;
        case CSS_FONT_SIZE_DIMENSION: {
            int px = ns_len_px(length, unit, NS_CSS_BASE_PX);
            if (px < 8)
                px = 8;
            if (px > 28)
                px = 28;
            if (px > 0)
                return px;
            break;
        }
        default:
            break;
        }
    }
    return NS_CSS_BASE_PX;
}

static int ns_margin_px(css_computed_style* st, int which) {
    css_fixed length = 0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type;
    if (which == 0)
        type = css_computed_margin_top(st, &length, &unit);
    else
        type = css_computed_margin_bottom(st, &length, &unit);
    if (type != CSS_MARGIN_SET)
        return 0;
    {
        int px = ns_len_px(length, unit, NS_CSS_BASE_PX);
        if (px < 0)
            px = 0;
        return px;
    }
}

int NsCssNodeBlock(NsCssNode* n) {
    css_computed_style* st = ns_style(n);
    uint8_t disp;
    uint8_t parentDisp;
    if (!n)
        return 0;
    if (n->parent) {
        css_computed_style* ps = ns_style(n->parent);
        if (ps && st) {
            uint8_t fl = css_computed_float(st);
            if (fl == CSS_FLOAT_LEFT || fl == CSS_FLOAT_RIGHT)
                return 0;
            parentDisp = css_computed_display(ps, n->parent->parent == NULL);
            if (parentDisp == CSS_DISPLAY_FLEX || parentDisp == CSS_DISPLAY_INLINE_FLEX ||
                parentDisp == CSS_DISPLAY_TABLE_ROW)
                return 0;
        }
    }
    if (!st)
        return 0;
    disp = css_computed_display(st, n->parent == NULL);
    switch (disp) {
    case CSS_DISPLAY_INLINE:
    case CSS_DISPLAY_INLINE_BLOCK:
    case CSS_DISPLAY_INLINE_FLEX:
    case CSS_DISPLAY_INLINE_GRID:
    case CSS_DISPLAY_INLINE_TABLE:
    case CSS_DISPLAY_TABLE_CELL:
    case CSS_DISPLAY_RUN_IN:
    case CSS_DISPLAY_TABLE_COLUMN:
        return 0;
    default:
        return 1;
    }
}

int NsCssNodeCenter(NsCssNode* n) {
    css_computed_style* st = ns_style(n);
    uint8_t a;
    if (!st)
        return 0;
    a = css_computed_text_align(st);
    return a == CSS_TEXT_ALIGN_CENTER || a == CSS_TEXT_ALIGN_LIBCSS_CENTER;
}

int NsCssNodeSkip(NsCssNode* n) {
    css_computed_style* st = ns_style(n);
    uint8_t disp;
    uint8_t vis;
    if (!st)
        return 0;
    /* Google ships body{display:none} until its scripts run. Hiding the
     * root blanks the whole page. */
    if (!strcmp(n->tag, "html") || !strcmp(n->tag, "body"))
        return 0;
    disp = css_computed_display(st, n->parent == NULL);
    if (disp == CSS_DISPLAY_NONE)
        return 1;
    vis = css_computed_visibility(st);
    if (vis == CSS_VISIBILITY_HIDDEN || vis == CSS_VISIBILITY_COLLAPSE)
        return 1;
    return 0;
}

uint32_t NsCssNodeBackground(NsCssNode* n) {
    css_computed_style* st = ns_style(n);
    css_color color = 0;
    uint8_t type;
    if (!st)
        return 0;
    type = css_computed_background_color(st, &color);
    if (type != CSS_BACKGROUND_COLOR_COLOR)
        return 0;
    /* libcss packs A in the top byte. Fully transparent is not a fill. */
    if (((uint32_t)color >> 24) == 0)
        return 0;
    return (uint32_t)color;
}

int NsCssNodeMarginLines(NsCssNode* n) {
    css_computed_style* st = ns_style(n);
    int lines;
    if (!st)
        return 0;
    lines = (ns_margin_px(st, 0) + ns_margin_px(st, 1));
    {
        css_fixed length = 0;
        css_unit unit = CSS_UNIT_PX;
        int px;
        if (css_computed_padding_top(st, &length, &unit) == CSS_PADDING_SET) {
            px = ns_len_px(length, unit, NS_CSS_BASE_PX);
            if (px > 0)
                lines += px;
        }
        length = 0;
        if (css_computed_padding_bottom(st, &length, &unit) == CSS_PADDING_SET) {
            px = ns_len_px(length, unit, NS_CSS_BASE_PX);
            if (px > 0)
                lines += px;
        }
    }
    lines /= 14;
    if (lines < 0)
        lines = 0;
    if (lines > 2)
        lines = 2;
    return lines;
}

static int ns_set_px(uint8_t type, css_fixed length, css_unit unit) {
    int px;
    if (type != CSS_MARGIN_SET && type != CSS_PADDING_SET && type != CSS_BORDER_WIDTH_WIDTH)
        return 0;
    px = ns_len_px(length, unit, NS_CSS_BASE_PX);
    return px > 0 ? px : 0;
}

void NsCssNodeUsed(NsCssNode* n, NsCssUsed* out) {
    css_computed_style* st;
    memset(out, 0, sizeof(*out));
    out->line = 19;
    out->list = 1;
    out->opacity = 255;
    st = ns_style(n);
    if (!st)
        return;
    {
        uint8_t w = css_computed_font_weight(st);
        uint8_t s = css_computed_font_style(st);
        uint8_t d = css_computed_text_decoration(st);
        if (w == CSS_FONT_WEIGHT_BOLD || w == CSS_FONT_WEIGHT_BOLDER || w >= CSS_FONT_WEIGHT_600)
            out->decor |= NS_CSS_BOLD;
        if (s == CSS_FONT_STYLE_ITALIC || s == CSS_FONT_STYLE_OBLIQUE)
            out->decor |= NS_CSS_ITALIC;
        if (d & CSS_TEXT_DECORATION_UNDERLINE)
            out->decor |= NS_CSS_UNDER;
        if (d & CSS_TEXT_DECORATION_LINE_THROUGH)
            out->decor |= NS_CSS_STRIKE;
        if (d & CSS_TEXT_DECORATION_OVERLINE)
            out->decor |= NS_CSS_OVER;
    }
    {
        uint8_t a = css_computed_text_align(st);
        if (a == CSS_TEXT_ALIGN_CENTER || a == CSS_TEXT_ALIGN_LIBCSS_CENTER)
            out->align = 1;
        else if (a == CSS_TEXT_ALIGN_RIGHT || a == CSS_TEXT_ALIGN_LIBCSS_RIGHT)
            out->align = 2;
    }
    {
        css_fixed length = 0;
        css_unit unit = CSS_UNIT_PX;
        int px = 0;
        px += ns_set_px(css_computed_margin_left(st, &length, &unit), length, unit);
        length = 0;
        px += ns_set_px(css_computed_padding_left(st, &length, &unit), length, unit);
        if (px > 120)
            px = 120;
        out->indent = (uint8_t)px;
    }
    {
        css_fixed length = 0;
        css_unit unit = CSS_UNIT_PX;
        uint8_t t = css_computed_line_height(st, &length, &unit);
        int px = 19;
        if (t == CSS_LINE_HEIGHT_DIMENSION)
            px = ns_len_px(length, unit, NS_CSS_BASE_PX);
        else if (t == CSS_LINE_HEIGHT_NUMBER)
            px = (int)((length >> CSS_RADIX_POINT) * NS_CSS_BASE_PX);
        if (px < 14)
            px = 14;
        if (px > 36)
            px = 36;
        out->line = (uint8_t)px;
    }
    {
        uint8_t t = css_computed_list_style_type(st);
        if (t == CSS_LIST_STYLE_TYPE_NONE)
            out->list = 0;
        else if (t == CSS_LIST_STYLE_TYPE_CIRCLE)
            out->list = 2;
        else if (t == CSS_LIST_STYLE_TYPE_SQUARE)
            out->list = 3;
        else if (t == CSS_LIST_STYLE_TYPE_DECIMAL || t == CSS_LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO)
            out->list = 4;
        else if (t == CSS_LIST_STYLE_TYPE_LOWER_ALPHA || t == CSS_LIST_STYLE_TYPE_LOWER_LATIN ||
                 t == CSS_LIST_STYLE_TYPE_UPPER_ALPHA || t == CSS_LIST_STYLE_TYPE_UPPER_LATIN)
            out->list = 5;
        else
            out->list = 1;
    }
    {
        uint8_t t = css_computed_text_transform(st);
        if (t == CSS_TEXT_TRANSFORM_UPPERCASE)
            out->upper = 1;
        else if (t == CSS_TEXT_TRANSFORM_LOWERCASE)
            out->upper = 2;
    }
    {
        uint8_t t = css_computed_white_space(st);
        if (t == CSS_WHITE_SPACE_PRE || t == CSS_WHITE_SPACE_PRE_WRAP)
            out->pre = 1;
        if (t == CSS_WHITE_SPACE_NOWRAP || t == CSS_WHITE_SPACE_PRE)
            out->nowrap = 1;
    }
    {
        css_fixed length = 0;
        css_unit unit = CSS_UNIT_PX;
        if (css_computed_width(st, &length, &unit) == CSS_WIDTH_SET) {
            int px = ns_len_px(length, unit, NS_CSS_BASE_PX);
            if (px > 0 && px < 2000)
                out->width = (uint16_t)px;
        }
    }
    {
        uint8_t bs = css_computed_border_bottom_style(st);
        css_color color = 0;
        if (bs >= CSS_BORDER_STYLE_DOTTED &&
            css_computed_border_bottom_color(st, &color) == CSS_BORDER_COLOR_COLOR)
            out->border = (uint32_t)color;
    }
    {
        css_fixed op = 0;
        if (css_computed_opacity(st, &op) == CSS_OPACITY_SET) {
            int a = (int)((op * 255) >> CSS_RADIX_POINT);
            if (a < 0)
                a = 0;
            if (a > 255)
                a = 255;
            out->opacity = (uint8_t)a;
        }
    }
}

/* ---- fully resolved box metrics for the layout engine ---- */

static int ns_resolve_len(
    css_fixed length, css_unit unit, int base, int em, int rem, int vw, int vh) {
    int64_t v = (int64_t)length;
    int64_t rad = (int64_t)(1 << CSS_RADIX_POINT);
    switch (unit) {
    case CSS_UNIT_PX:
        return (int)(v / rad);
    case CSS_UNIT_EM:
        return (int)(v * em / rad);
    case CSS_UNIT_REM:
        return (int)(v * rem / rad);
    case CSS_UNIT_EX:
    case CSS_UNIT_CH:
        return (int)(v * em / (2 * rad));
    case CSS_UNIT_LH:
        return (int)(v * (em + 5) / rad);
    case CSS_UNIT_VW:
        return (int)(v * vw / (100 * rad));
    case CSS_UNIT_VH:
        return (int)(v * vh / (100 * rad));
    case CSS_UNIT_VI:
        return (int)(v * vw / (100 * rad));
    case CSS_UNIT_VB:
        return (int)(v * vh / (100 * rad));
    case CSS_UNIT_VMIN:
        return (int)(v * (vw < vh ? vw : vh) / (100 * rad));
    case CSS_UNIT_VMAX:
        return (int)(v * (vw > vh ? vw : vh) / (100 * rad));
    case CSS_UNIT_IN:
        return (int)(v * 96 / rad);
    case CSS_UNIT_CM:
        return (int)(v * 3779 / (100 * rad));
    case CSS_UNIT_MM:
        return (int)(v * 378 / (100 * rad));
    case CSS_UNIT_Q:
        return (int)(v * 378 / (400 * rad));
    case CSS_UNIT_PT:
        return (int)(v * 4 / (3 * rad));
    case CSS_UNIT_PC:
        return (int)(v * 16 / rad);
    case CSS_UNIT_PCT:
        return (int)(v * base / (100 * rad));
    default:
        return 0;
    }
}

static int ns_side_margin(css_computed_style* st, int side, int base, int em, int rem, int vw, int vh) {
    css_fixed len = 0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type;
    switch (side) {
    case 0: type = css_computed_margin_top(st, &len, &unit); break;
    case 1: type = css_computed_margin_right(st, &len, &unit); break;
    case 2: type = css_computed_margin_bottom(st, &len, &unit); break;
    default: type = css_computed_margin_left(st, &len, &unit); break;
    }
    if (type != CSS_MARGIN_SET)
        return 0;
    return ns_resolve_len(len, unit, base, em, rem, vw, vh);
}

static int ns_side_padding(css_computed_style* st, int side, int base, int em, int rem, int vw, int vh) {
    css_fixed len = 0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type;
    switch (side) {
    case 0: type = css_computed_padding_top(st, &len, &unit); break;
    case 1: type = css_computed_padding_right(st, &len, &unit); break;
    case 2: type = css_computed_padding_bottom(st, &len, &unit); break;
    default: type = css_computed_padding_left(st, &len, &unit); break;
    }
    if (type != CSS_PADDING_SET)
        return 0;
    {
        int px = ns_resolve_len(len, unit, base, em, rem, vw, vh);
        return px > 0 ? px : 0;
    }
}

static int ns_side_border_width(css_computed_style* st, int side, int em, int rem, int vw, int vh) {
    css_fixed len = 0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type;
    switch (side) {
    case 0: type = css_computed_border_top_width(st, &len, &unit); break;
    case 1: type = css_computed_border_right_width(st, &len, &unit); break;
    case 2: type = css_computed_border_bottom_width(st, &len, &unit); break;
    default: type = css_computed_border_left_width(st, &len, &unit); break;
    }
    switch (type) {
    case CSS_BORDER_WIDTH_THIN: return 1;
    case CSS_BORDER_WIDTH_MEDIUM: return 3;
    case CSS_BORDER_WIDTH_THICK: return 5;
    case CSS_BORDER_WIDTH_WIDTH: {
        int px = ns_resolve_len(len, unit, em, em, rem, vw, vh);
        return px > 0 ? px : 0;
    }
    default:
        return 0;
    }
}

static uint8_t ns_side_border_style(css_computed_style* st, int side) {
    uint8_t type;
    switch (side) {
    case 0: type = css_computed_border_top_style(st); break;
    case 1: type = css_computed_border_right_style(st); break;
    case 2: type = css_computed_border_bottom_style(st); break;
    default: type = css_computed_border_left_style(st); break;
    }
    if (type == CSS_BORDER_STYLE_NONE || type == CSS_BORDER_STYLE_HIDDEN ||
        type == CSS_BORDER_STYLE_INHERIT)
        return 0;
    return type;
}

static uint32_t ns_side_border_color(css_computed_style* st, int side, uint32_t current) {
    css_color color = 0;
    uint8_t type;
    switch (side) {
    case 0: type = css_computed_border_top_color(st, &color); break;
    case 1: type = css_computed_border_right_color(st, &color); break;
    case 2: type = css_computed_border_bottom_color(st, &color); break;
    default: type = css_computed_border_left_color(st, &color); break;
    }
    if (type == CSS_BORDER_COLOR_COLOR)
        return (uint32_t)color;
    if (type == CSS_BORDER_COLOR_CURRENT_COLOR)
        return current;
    return current;
}

void NsCssNodeBoxStyle(NsCssNode* n, int base_width, int base_height,
                       int viewport_w, int viewport_h, NsCssBoxStyle* out) {
    css_computed_style* st;
    int font_px;
    int rem_px;
    uint32_t color;
    int i;
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->display = NS_DISPLAY_INLINE;
    out->opacity = 255;
    out->font_px = NS_CSS_BASE_PX;
    out->line_px = NS_CSS_BASE_PX + 5;
    out->color = 0xff202124u;
    if (!n)
        return;
    st = ns_style(n);
    if (!st)
        return;

    font_px = NsCssNodeFontPx(n);
    rem_px = NS_CSS_BASE_PX;
    color = NsCssNodeColor(n);
    out->font_px = font_px;
    out->color = color;
    out->background = NsCssNodeBackground(n);

    {
        uint8_t disp;
        uint8_t pos = css_computed_position(st);
        /* CSS blockifies floated inline boxes, but that makes a floated
         * inline-block (Google's Sign in button, for example) paint a
         * block-width background from its own content edge.  We do not
         * implement float placement yet, so keep the authored display and
         * let the inline-block path size it.  Positioned boxes still need
         * the blockified display for out-of-flow layout. */
        if (pos == CSS_POSITION_ABSOLUTE || pos == CSS_POSITION_FIXED || n->parent == NULL)
            disp = css_computed_display(st, n->parent == NULL);
        else
            disp = css_computed_display_static(st);
        switch (disp) {
        case CSS_DISPLAY_NONE: out->display = NS_DISPLAY_NONE; break;
        case CSS_DISPLAY_BLOCK:
        case CSS_DISPLAY_GRID:
        case CSS_DISPLAY_TABLE:
        case CSS_DISPLAY_TABLE_CAPTION:
        case CSS_DISPLAY_TABLE_FOOTER_GROUP:
        case CSS_DISPLAY_TABLE_HEADER_GROUP:
        case CSS_DISPLAY_TABLE_ROW_GROUP:
            out->display = NS_DISPLAY_BLOCK;
            break;
        case CSS_DISPLAY_FLEX:
            out->display = NS_DISPLAY_BLOCK;
            out->flex_container = 1;
            break;
        case CSS_DISPLAY_LIST_ITEM:
            out->display = NS_DISPLAY_LIST_ITEM;
            break;
        case CSS_DISPLAY_INLINE_BLOCK:
        case CSS_DISPLAY_INLINE_GRID:
        case CSS_DISPLAY_INLINE_TABLE:
            out->display = NS_DISPLAY_INLINE_BLOCK;
            break;
        case CSS_DISPLAY_INLINE_FLEX:
            out->display = NS_DISPLAY_INLINE_BLOCK;
            out->flex_container = 1;
            break;
        case CSS_DISPLAY_TABLE_ROW:
            out->display = NS_DISPLAY_TABLE_ROW;
            break;
        case CSS_DISPLAY_TABLE_CELL:
            out->display = NS_DISPLAY_TABLE_CELL;
            break;
        default:
            out->display = NS_DISPLAY_INLINE;
            break;
        }
        if (out->flex_container) {
            uint8_t d = css_computed_flex_direction(st);
            uint8_t j = css_computed_justify_content(st);
            uint8_t a = css_computed_align_items(st);
            uint8_t w = css_computed_flex_wrap(st);
            if (d == CSS_FLEX_DIRECTION_COLUMN || d == CSS_FLEX_DIRECTION_COLUMN_REVERSE)
                out->flex_direction = 1;
            out->flex_wrap = w == CSS_FLEX_WRAP_WRAP || w == CSS_FLEX_WRAP_WRAP_REVERSE;
            if (j == CSS_JUSTIFY_CONTENT_CENTER)
                out->justify_content = 1;
            else if (j == CSS_JUSTIFY_CONTENT_FLEX_END)
                out->justify_content = 2;
            else if (j == CSS_JUSTIFY_CONTENT_SPACE_BETWEEN)
                out->justify_content = 3;
            else if (j == CSS_JUSTIFY_CONTENT_SPACE_AROUND)
                out->justify_content = 4;
            else if (j == CSS_JUSTIFY_CONTENT_SPACE_EVENLY)
                out->justify_content = 5;
            if (a == CSS_ALIGN_ITEMS_CENTER)
                out->align_items = 1;
            else if (a == CSS_ALIGN_ITEMS_FLEX_END)
                out->align_items = 2;
            else if (a == CSS_ALIGN_ITEMS_FLEX_START)
                out->align_items = 3;
        }
        {
            uint8_t as = css_computed_align_self(st);
            if (as == CSS_ALIGN_SELF_CENTER)
                out->align_self = 1;
            else if (as == CSS_ALIGN_SELF_FLEX_END)
                out->align_self = 2;
            else if (as == CSS_ALIGN_SELF_FLEX_START)
                out->align_self = 3;
            else if (as == CSS_ALIGN_SELF_STRETCH)
                out->align_self = 4;
        }
        {
            css_fixed len = 0;
            css_unit unit = CSS_UNIT_PX;
            uint8_t fb = css_computed_flex_basis(st, &len, &unit);
            if (fb == CSS_FLEX_BASIS_SET) {
                out->flex_basis = ns_resolve_len(len, unit, base_width, font_px, rem_px,
                                                 viewport_w, viewport_h);
                out->flex_basis_auto = 0;
            } else {
                out->flex_basis = 0;
                out->flex_basis_auto = 1;
            }
            {
                css_fixed num = 0;
                css_computed_flex_grow(st, &num);
                out->flex_grow = (int)(num >> CSS_RADIX_POINT);
                css_computed_flex_shrink(st, &num);
                out->flex_shrink = (int)(num >> CSS_RADIX_POINT);
            }
        }
    }

    out->box_sizing = css_computed_box_sizing(st) == CSS_BOX_SIZING_BORDER_BOX ? 1 : 0;
    {
        css_fixed len = 0;
        css_unit unit = CSS_UNIT_PX;
        uint8_t type = css_computed_width(st, &len, &unit);
        if (type == CSS_WIDTH_SET) {
            out->width = ns_resolve_len(len, unit, base_width, font_px, rem_px, viewport_w, viewport_h);
            out->width_auto = 0;
        } else {
            out->width = 0;
            out->width_auto = 1;
        }
    }
    {
        css_fixed len = 0;
        css_unit unit = CSS_UNIT_PX;
        uint8_t type = css_computed_height(st, &len, &unit);
        if (type == CSS_HEIGHT_SET && !(unit == CSS_UNIT_PCT && base_height <= 0)) {
            out->height = ns_resolve_len(len, unit, base_height, font_px, rem_px, viewport_w, viewport_h);
            out->height_auto = 0;
        } else {
            out->height = 0;
            out->height_auto = 1;
        }
    }
    {
        css_fixed len = 0;
        css_unit unit = CSS_UNIT_PX;
        if (css_computed_min_width(st, &len, &unit) == CSS_MIN_WIDTH_SET)
            out->min_width = ns_resolve_len(len, unit, base_width, font_px, rem_px, viewport_w, viewport_h);
        len = 0;
        unit = CSS_UNIT_PX;
        if (css_computed_max_width(st, &len, &unit) == CSS_MAX_WIDTH_SET)
            out->max_width = ns_resolve_len(len, unit, base_width, font_px, rem_px, viewport_w, viewport_h);
        len = 0;
        unit = CSS_UNIT_PX;
        if (css_computed_min_height(st, &len, &unit) == CSS_MIN_HEIGHT_SET)
            out->min_height = ns_resolve_len(len, unit, base_height, font_px, rem_px, viewport_w, viewport_h);
        len = 0;
        unit = CSS_UNIT_PX;
        if (css_computed_max_height(st, &len, &unit) == CSS_MAX_HEIGHT_SET)
            out->max_height = ns_resolve_len(len, unit, base_height, font_px, rem_px, viewport_w, viewport_h);
    }
    for (i = 0; i < 4; i++) {
        out->margin[i] = ns_side_margin(st, i, base_width, font_px, rem_px, viewport_w, viewport_h);
        out->padding[i] = ns_side_padding(st, i, base_width, font_px, rem_px, viewport_w, viewport_h);
        out->border_style[i] = ns_side_border_style(st, i);
        out->border_width[i] = out->border_style[i]
                                   ? ns_side_border_width(st, i, font_px, rem_px, viewport_w, viewport_h)
                                   : 0;
        out->border_color[i] = ns_side_border_color(st, i, color);
    }

    {
        css_fixed len = 0;
        css_unit unit = CSS_UNIT_PX;
        uint8_t lh = css_computed_line_height(st, &len, &unit);
        int px = font_px + 5;
        if (lh == CSS_LINE_HEIGHT_NUMBER)
            px = (int)(((int64_t)len * font_px) / (1 << CSS_RADIX_POINT));
        else if (lh == CSS_LINE_HEIGHT_DIMENSION)
            px = ns_resolve_len(len, unit, font_px, font_px, rem_px, viewport_w, viewport_h);
        if (px < font_px + 2)
            px = font_px + 2;
        if (px > 80)
            px = 80;
        out->line_px = px;
    }

    {
        uint8_t a = css_computed_text_align(st);
        if (a == CSS_TEXT_ALIGN_CENTER || a == CSS_TEXT_ALIGN_LIBCSS_CENTER)
            out->text_align = 1;
        else if (a == CSS_TEXT_ALIGN_RIGHT || a == CSS_TEXT_ALIGN_LIBCSS_RIGHT)
            out->text_align = 2;
    }
    {
        uint8_t ws = css_computed_white_space(st);
        out->white_space_pre = (ws == CSS_WHITE_SPACE_PRE || ws == CSS_WHITE_SPACE_PRE_WRAP ||
                                ws == CSS_WHITE_SPACE_PRE_LINE);
        out->white_space_nowrap = (ws == CSS_WHITE_SPACE_NOWRAP || ws == CSS_WHITE_SPACE_PRE);
    }
    {
        uint8_t lt = css_computed_list_style_type(st);
        if (lt == CSS_LIST_STYLE_TYPE_NONE)
            out->list_type = 0;
        else if (lt == CSS_LIST_STYLE_TYPE_CIRCLE)
            out->list_type = 2;
        else if (lt == CSS_LIST_STYLE_TYPE_SQUARE)
            out->list_type = 3;
        else if (lt == CSS_LIST_STYLE_TYPE_DECIMAL || lt == CSS_LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO)
            out->list_type = 4;
        else
            out->list_type = 1;
    }
    {
        uint8_t w = css_computed_font_weight(st);
        uint8_t s = css_computed_font_style(st);
        uint8_t d = css_computed_text_decoration(st);
        if (w == CSS_FONT_WEIGHT_BOLD || w == CSS_FONT_WEIGHT_BOLDER || w >= CSS_FONT_WEIGHT_600)
            out->decor |= NS_CSS_BOLD;
        if (s == CSS_FONT_STYLE_ITALIC || s == CSS_FONT_STYLE_OBLIQUE)
            out->decor |= NS_CSS_ITALIC;
        if (d & CSS_TEXT_DECORATION_UNDERLINE)
            out->decor |= NS_CSS_UNDER;
        if (d & CSS_TEXT_DECORATION_LINE_THROUGH)
            out->decor |= NS_CSS_STRIKE;
        if (d & CSS_TEXT_DECORATION_OVERLINE)
            out->decor |= NS_CSS_OVER;
    }
    {
        uint8_t pos = css_computed_position(st);
        if (pos == CSS_POSITION_ABSOLUTE)
            out->position = NS_POS_ABSOLUTE;
        else if (pos == CSS_POSITION_FIXED)
            out->position = NS_POS_FIXED;
        else if (pos == CSS_POSITION_RELATIVE)
            out->position = NS_POS_RELATIVE;
        out->top_auto = out->right_auto = out->bottom_auto = out->left_auto = 1;
        {
            css_fixed len = 0;
            css_unit unit = CSS_UNIT_PX;
            uint8_t t = css_computed_top(st, &len, &unit);
            if (t == CSS_TOP_SET) {
                out->top = ns_resolve_len(len, unit, base_height, font_px, rem_px,
                                          viewport_w, viewport_h);
                out->top_auto = 0;
            }
            len = 0;
            unit = CSS_UNIT_PX;
            t = css_computed_right(st, &len, &unit);
            if (t == CSS_RIGHT_SET) {
                out->right = ns_resolve_len(len, unit, base_width, font_px, rem_px,
                                            viewport_w, viewport_h);
                out->right_auto = 0;
            }
            len = 0;
            unit = CSS_UNIT_PX;
            t = css_computed_bottom(st, &len, &unit);
            if (t == CSS_BOTTOM_SET) {
                out->bottom = ns_resolve_len(len, unit, base_height, font_px, rem_px,
                                             viewport_w, viewport_h);
                out->bottom_auto = 0;
            }
            len = 0;
            unit = CSS_UNIT_PX;
            t = css_computed_left(st, &len, &unit);
            if (t == CSS_LEFT_SET) {
                out->left = ns_resolve_len(len, unit, base_width, font_px, rem_px,
                                           viewport_w, viewport_h);
                out->left_auto = 0;
            }
            t = css_computed_z_index(st, (int32_t*)&out->z_index);
            out->z_index_auto = (t != CSS_Z_INDEX_SET);
        }
        {
            uint8_t fl = css_computed_float(st);
            if (fl == CSS_FLOAT_LEFT)
                out->float_side = 1;
            else if (fl == CSS_FLOAT_RIGHT)
                out->float_side = 2;
        }
    }
    {
        css_fixed op = 0;
        if (css_computed_opacity(st, &op) == CSS_OPACITY_SET) {
            int a = (int)(((int64_t)op * 255) / (1 << CSS_RADIX_POINT));
            if (a < 0)
                a = 0;
            if (a > 255)
                a = 255;
            out->opacity = a;
        }
    }
}
