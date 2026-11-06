/**
 * Block/inline layout and painting.  See layout.h.
 *
 * Scope: CSS 2.1 block formatting, inline text runs, line wrapping,
 * margins/padding/borders/backgrounds, links, lists, and simple form
 * controls.  Tables/floats/positioning degrade to document flow rather
 * than disappearing.
 */

#include "layout.h"
#include "dom.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <_xeneva.h>
#include <sys/_kefile.h>

#define NS_BOX_BLOCK 1
#define NS_BOX_LINE 2
#define NS_BOX_RUN 3
#define NS_BOX_CONTROL 4
#define NS_BOX_IMAGE 5
#define NS_BOX_INLINE_BLOCK 6

#define NS_LAYOUT_MAX_LINKS 640
#define NS_LAYOUT_MAX_DEPTH 64
#define NS_PLAIN_MAX (256 * 1024)

typedef struct NsLayoutBox NsLayoutBox;
struct NsLayoutBox {
    NsLayoutBox* first_child;
    NsLayoutBox* next;
    NsCssNode* node;
    uint8_t kind;
    int x, y, w, h;    /* border box (block) / run rect */
    int cx, cy, cw, ch; /* content box (block) */
    NsCssBoxStyle sty;
    int baseline;
    uint32_t color;
    uint32_t bg;
    uint32_t border_color;
    int font_px;
    uint8_t decor;
    const char* href;
    const char* text;
    size_t text_len;
    const char* label;
    const uint32_t* pixels;
    int img_w, img_h;
    int src_w, src_h;
    int margin[4];     /* top, right, bottom, left */
    int offset_x, offset_y;
    uint8_t position;
    uint8_t framed;
    uint8_t inline_svg;
    int z_index;
    struct NsLayoutBox* abs_next;
};

typedef struct NsTextPool {
    struct NsTextPool* next;
    char data[1];
} NsTextPool;

typedef struct {
    uint8_t kind; /* 0 text, 1 br, 2 control, 3 image */
    NsCssNode* node;
    const char* text;
    size_t text_len;
    const char* label;
    const char* href;
    int font_px;
    int line_px;
    int width;
    int height;
    uint32_t color;
    uint32_t bg;
    uint32_t border_color;
    uint8_t decor;
    uint8_t pre;
    uint8_t nowrap;
    const uint32_t* pixels;
    int img_w, img_h;
    int src_w, src_h;
    int margin[4];
    int offset_x, offset_y;
    uint8_t position;
    uint8_t framed;
    uint8_t inline_svg;
} NsInlineItem;

typedef struct {
    NsInlineItem* items;
    int n;
    int cap;
} NsInlineList;

static NsCssNode* s_root_node;
static NsLayoutBox* s_root_box;
static NsLayoutBox* s_abs_head;
static NsLayoutBox* s_abs_tail;
static NsImageLookupFn s_img_lookup;
static NsInlineSvgLookupFn s_svg_lookup;
static int s_collect_base_w = 640;
static int s_viewport_w = 640;
static int s_viewport_h = 420;
static int s_page_h;
static int s_content_h;
static uint32_t s_page_bg = 0xffffffffu;
static int s_active;
static int s_blocks, s_lines, s_runs, s_controls;
static NsTextPool* s_pool;
static char s_plain[NS_PLAIN_MAX + 1];

static struct {
    int x, y, w, h;
    const char* href;
} s_links[NS_LAYOUT_MAX_LINKS];
static int s_nlinks;

/* Interactive form controls in page coordinates.  Links and controls are
 * kept separately because controls need the DOM node (for value, name and
 * form submission) rather than a URL. */
#define NS_LAYOUT_MAX_CONTROLS 128
static struct {
    int x, y, w, h;
    NsCssNode* node;
} s_formctrls[NS_LAYOUT_MAX_CONTROLS];
static int s_nformctrls;
static NsCssNode* s_edit_node;
static char s_edit_text[512];
static int s_edit_cursor;
static int s_edit_active;

/* Six font buckets keep the per-font glyph atlas cost bounded. */
static const int kFontPx[6] = {11, 14, 17, 20, 24, 28};
static ChFont* s_fonts[6];

void NsLayoutSetImageLookup(NsImageLookupFn fn) {
    s_img_lookup = fn;
}

void NsLayoutSetInlineSvgLookup(NsInlineSvgLookupFn fn) {
    s_svg_lookup = fn;
}

static int ns_isspace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static char* ns_dup(const char* s, size_t len) {
    char* out = (char*)malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, s, len);
    out[len] = 0;
    return out;
}

static const char* ns_pool_add(const char* s, size_t len) {
    NsTextPool* p = (NsTextPool*)malloc(sizeof(NsTextPool) + len);
    if (!p)
        return NULL;
    p->next = s_pool;
    s_pool = p;
    memcpy(p->data, s, len);
    p->data[len] = 0;
    return p->data;
}

static NsLayoutBox* ns_box_new(int kind) {
    NsLayoutBox* b = (NsLayoutBox*)calloc(1, sizeof(NsLayoutBox));
    if (b) {
        b->kind = (uint8_t)kind;
        if (kind == NS_BOX_BLOCK)
            s_blocks++;
        else if (kind == NS_BOX_LINE)
            s_lines++;
        else if (kind == NS_BOX_RUN)
            s_runs++;
        else
            s_controls++;
    }
    return b;
}

static void ns_box_free(NsLayoutBox* b) {
    while (b) {
        NsLayoutBox* next = b->next;
        ns_box_free(b->first_child);
        free(b);
        b = next;
    }
}

static void ns_append_box(NsLayoutBox* parent, NsLayoutBox* child) {
    NsLayoutBox* tail;
    if (!parent || !child)
        return;
    if (!parent->first_child) {
        parent->first_child = child;
        return;
    }
    tail = parent->first_child;
    while (tail->next)
        tail = tail->next;
    tail->next = child;
}

static ChFont* ns_font_for_px(int px);

/* stbtt stores the baseline: ascent is above it, descent is below it
 * (stbDescent is negative). A line that puts the baseline at
 * line_h-2 leaves two pixels for the tail of y, p, g and q. */
static int ns_font_ascent(int font_px) {
    ChFont* f = ns_font_for_px(font_px);
    int a;
    if (!f || f->stbScale <= 0.f)
        return font_px > 0 ? (font_px * 3) / 4 : 8;
    a = (int)(f->stbAscent * f->stbScale + 0.5f);
    if (a < 1)
        a = 1;
    return a;
}

static int ns_font_descent(int font_px) {
    ChFont* f = ns_font_for_px(font_px);
    int d;
    if (!f || f->stbScale <= 0.f)
        return font_px > 0 ? font_px / 4 : 2;
    d = (int)(-f->stbDescent * f->stbScale + 0.5f);
    if (d < 0)
        d = 0;
    return d;
}

/* Baseline inside a box whose top is `top`. Half the leftover line gap
 * sits above the ascender, the rest below the descender. */
static int ns_baseline_y(int top, int box_h, int font_px) {
    int ascent = ns_font_ascent(font_px);
    int descent = ns_font_descent(font_px);
    int em = ascent + descent;
    int lead;
    if (box_h < em)
        box_h = em;
    lead = box_h - em;
    return top + lead / 2 + ascent;
}

static ChFont* ns_font_for_px(int px) {
    int i;
    int best = 1;
    int bestd = 10000;
    for (i = 0; i < 6; i++) {
        int d = kFontPx[i] - px;
        if (d < 0)
            d = -d;
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    if (!s_fonts[best]) {
        ChFont* f = ChInitialiseFont((char*)CALIBRI);
        if (!f)
            return NULL;
        /* ChFontSetSize takes points; convert the bucket's pixel size. */
        ChFontSetSize(f, (kFontPx[best] * 3 + 3) / 4);
        s_fonts[best] = f;
    }
    return s_fonts[best];
}

static int ns_char_width(int font_px, char c) {
    ChFont* f = ns_font_for_px(font_px);
    int64_t w;
    if (!f)
        return 7;
    w = ChFontGetWidthChar(f, c);
    if (w < 0)
        return 0;
    if (w > 200)
        return 200;
    return (int)w;
}

static int ns_text_width(int font_px, const char* text, size_t len) {
    int w = 0;
    size_t i;
    if (!text)
        return 0;
    for (i = 0; i < len; i++)
        w += ns_char_width(font_px, text[i]);
    return w;
}

/* ---- text sanitising (UTF-8 -> ASCII-ish for the Chitralekha atlas) ---- */

static size_t ns_utf8_decode(const unsigned char* s, size_t len, unsigned* cp) {
    unsigned c = s[0];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0 && len >= 2) {
        *cp = ((unsigned)(c & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    }
    if ((c & 0xF0) == 0xE0 && len >= 3) {
        *cp = ((unsigned)(c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0 && len >= 4) {
        *cp = ((unsigned)(c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
              ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}

static void ns_put(char* out, size_t cap, size_t* o, char c) {
    if (*o + 1 < cap)
        out[(*o)++] = c;
}

static void ns_put_cp(char* out, size_t cap, size_t* o, unsigned cp) {
    switch (cp) {
    case 0x00A0: ns_put(out, cap, o, ' '); break;
    case 0x00A9: ns_put(out, cap, o, '('); ns_put(out, cap, o, 'c'); ns_put(out, cap, o, ')'); break;
    case 0x00AE: ns_put(out, cap, o, '('); ns_put(out, cap, o, 'R'); ns_put(out, cap, o, ')'); break;
    case 0x2010:
    case 0x2011:
    case 0x2012:
    case 0x2013:
    case 0x2014:
    case 0x2212: ns_put(out, cap, o, '-'); break;
    case 0x2018:
    case 0x2019: ns_put(out, cap, o, '\''); break;
    case 0x201C:
    case 0x201D: ns_put(out, cap, o, '"'); break;
    case 0x2022:
    case 0x00B7: ns_put(out, cap, o, '*'); break;
    case 0x2026:
        ns_put(out, cap, o, '.');
        ns_put(out, cap, o, '.');
        ns_put(out, cap, o, '.');
        break;
    case 0x2122:
        ns_put(out, cap, o, '(');
        ns_put(out, cap, o, 'T');
        ns_put(out, cap, o, 'M');
        ns_put(out, cap, o, ')');
        break;
    case 0x2190: ns_put(out, cap, o, '<'); break;
    case 0x2192: ns_put(out, cap, o, '>'); break;
    case 0x2264: ns_put(out, cap, o, '<'); ns_put(out, cap, o, '='); break;
    case 0x2265: ns_put(out, cap, o, '>'); ns_put(out, cap, o, '='); break;
    default:
        if (cp < 0x80)
            ns_put(out, cap, o, (char)cp);
        else
            ns_put(out, cap, o, '?');
        break;
    }
}

static char* ns_clean_text(const char* src, size_t len, int pre, size_t* outlen) {
    char* out;
    size_t i = 0;
    size_t o = 0;
    size_t cap = len * 4 + 16;
    int last_space = 0;
    if (!src || len == 0)
        return NULL;
    out = (char*)malloc(cap);
    if (!out)
        return NULL;
    while (i < len) {
        unsigned cp;
        size_t n = ns_utf8_decode((const unsigned char*)src + i, len - i, &cp);
        i += n;
        if (cp == '\r')
            continue;
        if (cp == '\n') {
            if (pre) {
                ns_put(out, cap, &o, '\n');
                last_space = 0;
            } else if (!last_space) {
                ns_put(out, cap, &o, ' ');
                last_space = 1;
            }
            continue;
        }
        if (cp == '\t') {
            int k;
            if (pre) {
                for (k = 0; k < 4; k++)
                    ns_put(out, cap, &o, ' ');
            } else if (!last_space) {
                ns_put(out, cap, &o, ' ');
                last_space = 1;
            }
            continue;
        }
        if (cp == ' ') {
            if (pre) {
                ns_put(out, cap, &o, ' ');
            } else if (!last_space) {
                ns_put(out, cap, &o, ' ');
                last_space = 1;
            }
            continue;
        }
        ns_put_cp(out, cap, &o, cp);
        last_space = 0;
    }
    if (o == 0) {
        free(out);
        return NULL;
    }
    out[o] = 0;
    if (outlen)
        *outlen = o;
    return out;
}

static void ns_plain_content(NsCssNode* n, char* out, size_t cap, size_t* o) {
    NsCssNode* c;
    if (!n || !out || !o || *o + 1 >= cap)
        return;
    if (n->is_text) {
        size_t clen = 0;
        char* clean = ns_clean_text(n->text, n->text_len, 0, &clen);
        if (clean) {
            size_t i;
            for (i = 0; i < clen && *o + 1 < cap; i++)
                out[(*o)++] = clean[i];
            free(clean);
        }
        return;
    }
    for (c = n->first_child; c; c = c->next) {
        if (!c->is_text) {
            if (!strcmp(c->tag, "br")) {
                if (*o > 0 && out[*o - 1] != ' ' && *o + 1 < cap)
                    out[(*o)++] = ' ';
                continue;
            }
            if (!strcmp(c->tag, "script") || !strcmp(c->tag, "style"))
                continue;
        }
        ns_plain_content(c, out, cap, o);
    }
    out[*o] = 0;
}

static NsCssNode* ns_find_svg(NsCssNode* n, int depth) {
    NsCssNode* c;
    if (!n || depth > 4)
        return NULL;
    for (c = n->first_child; c; c = c->next) {
        if (c->is_text)
            continue;
        if (!strcmp(c->tag, "svg"))
            return c;
        {
            NsCssNode* found = ns_find_svg(c, depth + 1);
            if (found)
                return found;
        }
    }
    return NULL;
}

static const char* ns_href_for(NsCssNode* n) {
    for (; n; n = n->parent) {
        if (!n->is_text && !strcmp(n->tag, "a")) {
            const char* href = NsCssNodeAttr(n, "href");
            if (href && href[0])
                return href;
        }
    }
    return NULL;
}

/* An atomic inline-block can be a wrapper around the real anchor
 * (the apps grid is a div around <a><svg>).  Ancestor walk misses that. */
static const char* ns_href_down(NsCssNode* n, int depth) {
    NsCssNode* c;
    if (!n || depth > 4)
        return NULL;
    for (c = n->first_child; c; c = c->next) {
        const char* href;
        if (c->is_text)
            continue;
        if (!strcmp(c->tag, "a")) {
            href = NsCssNodeAttr(c, "href");
            if (href && href[0])
                return href;
        }
        href = ns_href_down(c, depth + 1);
        if (href)
            return href;
    }
    return NULL;
}

static const char* ns_href_box(NsCssNode* n) {
    const char* href = ns_href_for(n);
    if (href)
        return href;
    return ns_href_down(n, 0);
}

static int ns_attr_int(NsCssNode* n, const char* name) {
    const char* s = NsCssNodeAttr(n, name);
    int v = 0;
    if (!s)
        return 0;
    while (*s == ' ' || *s == '\t')
        s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

/* Used size of a replaced box. A CSS length wins on that axis. An HTML
 * width or height attribute is a used size only while that axis is
 * still auto and the other axis did not come from CSS. One definite
 * axis takes its ratio from both attributes when they exist, otherwise
 * from the bitmap. Both CSS axes set are left as specified. */
static void ns_replaced_used(int width_auto, int css_w, int height_auto, int css_h, int aw, int ah,
                             int iw, int ih, int* ow, int* oh) {
    int w = 0;
    int h = 0;
    int spec_w = 0;
    int spec_h = 0;
    if (!width_auto) {
        w = css_w > 0 ? css_w : 0;
        spec_w = 1;
    }
    if (!height_auto) {
        h = css_h > 0 ? css_h : 0;
        spec_h = 1;
    }
    if (spec_w && !spec_h) {
        if (aw > 0 && ah > 0)
            h = (int)(((int64_t)w * ah) / aw);
        else if (iw > 0 && ih > 0)
            h = (int)(((int64_t)w * ih) / iw);
    } else if (spec_h && !spec_w) {
        if (aw > 0 && ah > 0)
            w = (int)(((int64_t)h * aw) / ah);
        else if (iw > 0 && ih > 0)
            w = (int)(((int64_t)h * iw) / ih);
    } else if (!spec_w && !spec_h) {
        if (aw > 0)
            w = aw;
        else if (iw > 0)
            w = iw;
        if (ah > 0)
            h = ah;
        else if (ih > 0)
            h = ih;
        if (w > 0 && h <= 0 && iw > 0 && ih > 0)
            h = (int)(((int64_t)w * ih) / iw);
        else if (h > 0 && w <= 0 && iw > 0 && ih > 0)
            w = (int)(((int64_t)h * iw) / ih);
    }
    if (!spec_w && w <= 0 && iw > 0)
        w = iw;
    if (!spec_h && h <= 0 && ih > 0)
        h = ih;
    *ow = w;
    *oh = h;
}

static int ns_skip_node(NsCssNode* n) {
    NsCssBoxStyle bs;
    if (!n || n->is_text)
        return 0;
    if (!strcmp(n->tag, "script") || !strcmp(n->tag, "style") || !strcmp(n->tag, "head") ||
        !strcmp(n->tag, "title") || !strcmp(n->tag, "meta") || !strcmp(n->tag, "link") ||
        !strcmp(n->tag, "base") || !strcmp(n->tag, "template"))
        return 1;
    if (!strcmp(n->tag, "iframe") || !strcmp(n->tag, "canvas") ||
        !strcmp(n->tag, "object") || !strcmp(n->tag, "embed") || !strcmp(n->tag, "video") ||
        !strcmp(n->tag, "audio"))
        return 1;
    /* visibility:hidden/collapse and display:none both remove a subtree
     * from layout.  The Zerodha mega-menu is hidden with visibility, not
     * display:none; flattening it into the nav line painted it over the
     * hero. */
    if (NsCssNodeSkip(n) && strcmp(n->tag, "html") && strcmp(n->tag, "body"))
        return 1;
    NsCssNodeBoxStyle(n, s_collect_base_w, s_viewport_h, s_viewport_w, s_viewport_h, &bs);
    if (bs.display == NS_DISPLAY_NONE && strcmp(n->tag, "html") && strcmp(n->tag, "body"))
        return 1;
    return 0;
}

static int ns_is_block(NsCssNode* n) {
    NsCssBoxStyle bs;
    if (!n || n->is_text || ns_skip_node(n))
        return 0;
    NsCssNodeBoxStyle(n, 0, 0, s_viewport_w, s_viewport_h, &bs);
    if (bs.display == NS_DISPLAY_BLOCK || bs.display == NS_DISPLAY_LIST_ITEM ||
        bs.display == NS_DISPLAY_TABLE_ROW || bs.display == NS_DISPLAY_TABLE_CELL)
        return 1;
    return 0;
}

static int ns_list_push(NsInlineList* list, NsInlineItem* item) {
    if (list->n >= list->cap) {
        int ncap = list->cap ? list->cap * 2 : 64;
        NsInlineItem* ni = (NsInlineItem*)realloc(list->items, (size_t)ncap * sizeof(NsInlineItem));
        if (!ni)
            return -1;
        list->items = ni;
        list->cap = ncap;
    }
    list->items[list->n++] = *item;
    return 0;
}

static void ns_collect_inline(NsCssNode* n, NsCssNode* parent, NsInlineList* list);

static void ns_add_control(NsInlineItem* it, const char* label, int w, int h, uint32_t bg,
                           uint32_t border, int font_px, int line_px, uint32_t color,
                           uint8_t decor, const char* href) {
    memset(it, 0, sizeof(*it));
    it->kind = 2;
    it->label = label;
    it->width = w;
    it->height = h;
    it->bg = bg;
    it->border_color = border;
    it->font_px = font_px;
    it->line_px = line_px;
    it->color = color;
    it->decor = decor;
    it->href = href;
    it->nowrap = 0;
}

static void ns_collect_children(NsCssNode* n, NsInlineList* list) {
    NsCssNode* c;
    for (c = n->first_child; c; c = c->next)
        ns_collect_inline(c, n, list);
}

static void ns_collect_inline(NsCssNode* n, NsCssNode* parent, NsInlineList* list) {
    NsCssBoxStyle bs;
    NsInlineItem it;
    NsCssNode* styleNode;
    if (!n || ns_skip_node(n))
        return;
    if (n->is_text) {
        size_t clen = 0;
        char* clean;
        styleNode = parent ? parent : n->parent;
        if (!styleNode)
            return;
        NsCssNodeBoxStyle(styleNode, s_collect_base_w, s_viewport_h, s_viewport_w, s_viewport_h, &bs);
        if (bs.display == NS_DISPLAY_NONE)
            return;
        clean = ns_clean_text(n->text, n->text_len, bs.white_space_pre, &clen);
        if (!clean)
            return;
        memset(&it, 0, sizeof(it));
        it.kind = 0;
        it.node = n;
        it.text = ns_pool_add(clean, clen);
        it.text_len = clen;
        it.href = ns_href_for(n);
        it.font_px = bs.font_px;
        it.line_px = bs.line_px;
        it.color = bs.color;
        it.decor = bs.decor;
        it.pre = bs.white_space_pre;
        it.nowrap = bs.white_space_nowrap;
        free(clean);
        if (it.text)
            ns_list_push(list, &it);
        return;
    }

    NsCssNodeBoxStyle(n, s_collect_base_w, s_viewport_h, s_viewport_w, s_viewport_h, &bs);
    if (bs.display == NS_DISPLAY_NONE)
        return;
    /* Out-of-flow inline boxes are handled by the block's positioned
     * child pass; flattening them here would put them back in the line. */
    if (bs.position == NS_POS_ABSOLUTE || bs.position == NS_POS_FIXED)
        return;

    if (!strcmp(n->tag, "br")) {
        memset(&it, 0, sizeof(it));
        it.kind = 1;
        it.font_px = bs.font_px;
        it.line_px = bs.line_px;
        it.color = bs.color;
        it.decor = bs.decor;
        it.href = ns_href_for(n);
        ns_list_push(list, &it);
        return;
    }

    if (!strcmp(n->tag, "svg")) {
        const uint32_t* pixels = NULL;
        int bw = 0, bh = 0, iw = 0, ih = 0;
        int aw, ah, w, h;
        if (s_svg_lookup)
            pixels = s_svg_lookup(n, &bw, &bh, &iw, &ih);
        if (!pixels || bw <= 0 || bh <= 0)
            return;
        if (iw <= 0)
            iw = bw;
        if (ih <= 0)
            ih = bh;
        aw = ns_attr_int(n, "width");
        ah = ns_attr_int(n, "height");
        ns_replaced_used(bs.width_auto, bs.width, bs.height_auto, bs.height, aw, ah, iw, ih, &w, &h);
        if (w < 1)
            w = 24;
        if (h < 1)
            h = 24;
        if (bs.max_width > 0 && w > bs.max_width) {
            int64_t nh = (int64_t)h * bs.max_width / w;
            w = bs.max_width;
            h = nh > 0 ? (int)nh : 1;
        }
        if (bs.max_height > 0 && h > bs.max_height) {
            int64_t nw = (int64_t)w * bs.max_height / h;
            h = bs.max_height;
            w = nw > 0 ? (int)nw : 1;
        }
        if (w < 1)
            w = 1;
        if (h < 1)
            h = 1;
        ns_add_control(&it, NULL, w, h, 0, 0, bs.font_px, bs.line_px, bs.color, bs.decor,
                       ns_href_for(n));
        it.kind = 3;
        it.framed = 0;
        it.inline_svg = 1;
        it.pixels = pixels;
        it.img_w = w;
        it.img_h = h;
        it.src_w = bw;
        it.src_h = bh;
        it.position = bs.position;
        if (bs.position == NS_POS_RELATIVE) {
            it.offset_x = !bs.left_auto ? bs.left : (!bs.right_auto ? -bs.right : 0);
            it.offset_y = !bs.top_auto ? bs.top : (!bs.bottom_auto ? -bs.bottom : 0);
        }
        ns_list_push(list, &it);
        return;
    }

    if (!strcmp(n->tag, "img")) {
        const char* alt = NsCssNodeAttr(n, "alt");
        const char* src = NsCssNodeAttr(n, "src");
        const char* srcset = NsCssNodeAttr(n, "srcset");
        int iw = 0, ih = 0;   /* intrinsic (CSS natural) size */
        int bw = 0, bh = 0;   /* decoded bitmap size */
        int w = 0, h = 0;
        const uint32_t* pixels = NULL;
        char* label = NULL;
        char* clean = NULL;
        if (src && src[0] && s_img_lookup)
            pixels = s_img_lookup(src, &bw, &bh, &iw, &ih);
        if (!pixels && srcset && srcset[0] && s_img_lookup) {
            char first[512];
            size_t z = 0;
            while (srcset[z] && srcset[z] != ' ' && srcset[z] != '\t' && srcset[z] != ',' &&
                   z + 1 < sizeof(first)) {
                first[z] = srcset[z];
                z++;
            }
            first[z] = 0;
            if (z)
                pixels = s_img_lookup(first, &bw, &bh, &iw, &ih);
        }
        if (iw <= 0)
            iw = bw;
        if (ih <= 0)
            ih = bh;
        if (bw <= 0)
            bw = iw;
        if (bh <= 0)
            bh = ih;
        /* height:auto must follow the width. An HTML height attribute
         * used to count as a specified height, so a width:100% image
         * kept that attribute's pixel height and the bitmap was drawn
         * squashed. The type inside the product screenshots lost its tops. */
        ns_replaced_used(bs.width_auto, bs.width, bs.height_auto, bs.height, ns_attr_int(n, "width"),
                         ns_attr_int(n, "height"), iw, ih, &w, &h);
        if (w < 1)
            w = 28;
        if (h < 1)
            h = 22;
        /* Replaced elements honour max-width/max-height while preserving
         * the intrinsic aspect ratio.  Zerodha's landing image is sized
         * entirely by img{max-width:100%} + max-height:45vh. */
        {
            int maxw = bs.max_width > 0 ? bs.max_width : 0;
            int maxh = bs.max_height > 0 ? bs.max_height : 0;
            if (maxw > 0 && w > maxw) {
                int64_t nh = (int64_t)h * maxw / w;
                w = maxw;
                h = nh > 0 ? (int)nh : 1;
            }
            if (maxh > 0 && h > maxh) {
                int64_t nw = (int64_t)w * maxh / h;
                h = maxh;
                w = nw > 0 ? (int)nw : 1;
            }
        }
        /* Cap both axes together. Clamping one side used to break the ratio. */
        if (w > 1600 && w > 0) {
            int64_t nh = ((int64_t)h * 1600) / w;
            w = 1600;
            if (nh > 0)
                h = (int)nh;
        }
        if (h > 1200 && h > 0) {
            int64_t nw = ((int64_t)w * 1200) / h;
            h = 1200;
            if (nw > 0)
                w = (int)nw;
        }
        if (pixels) {
            label = NULL;
        } else if (alt && alt[0]) {
            clean = ns_clean_text(alt, strlen(alt), 0, NULL);
            label = clean ? (char*)ns_pool_add(clean, strlen(clean)) : NULL;
            free(clean);
        } else {
            label = (char*)ns_pool_add("[image]", 7);
        }
        {
            int ml = bs.margin[3] > 0 ? bs.margin[3] : 0;
            int mr = bs.margin[1] > 0 ? bs.margin[1] : 0;
            int mt = bs.margin[0] > 0 ? bs.margin[0] : 0;
            int mb = bs.margin[2] > 0 ? bs.margin[2] : 0;
            int ow = w + ml + mr;
            int oh = h + mt + mb;
            ns_add_control(&it, label, ow, oh,
                           pixels ? 0 : 0xfff0f0f0u,
                           pixels ? 0 : 0xffb0b0b0u,
                           bs.font_px, bs.line_px, bs.color, bs.decor, ns_href_for(n));
            it.kind = 3;
            it.framed = pixels ? 0 : 1;
            it.pixels = pixels;
            it.img_w = w;
            it.img_h = h;
            it.src_w = bw;
            it.src_h = bh;
            it.margin[0] = mt;
            it.margin[1] = mr;
            it.margin[2] = mb;
            it.margin[3] = ml;
            it.position = bs.position;
            if (bs.position == NS_POS_RELATIVE) {
                it.offset_x = !bs.left_auto ? bs.left : (!bs.right_auto ? -bs.right : 0);
                it.offset_y = !bs.top_auto ? bs.top : (!bs.bottom_auto ? -bs.bottom : 0);
            }
            /* An absolutely positioned image is out of flow.  Keep it
             * off-screen when the page uses the usual -9999px skip-link
             * pattern; the block layer handles positioned boxes. */
            if ((bs.position == NS_POS_ABSOLUTE || bs.position == NS_POS_FIXED) &&
                !bs.left_auto && bs.left < -1000)
                return;
        }
        ns_list_push(list, &it);
        return;
    }

    if (!strcmp(n->tag, "input")) {
        const char* type = NsCssNodeAttr(n, "type");
        const char* value = NsCssNodeAttr(n, "value");
        const char* ph = NsCssNodeAttr(n, "placeholder");
        char tbuf[24];
        int w = 120;
        int h = bs.line_px + 6;
        char lbuf[256];
        lbuf[0] = 0;
        if (h < 20)
            h = 20;
        if (type) {
            size_t tl = strlen(type);
            if (tl >= sizeof(tbuf))
                tl = sizeof(tbuf) - 1;
            memcpy(tbuf, type, tl);
            tbuf[tl] = 0;
            for (size_t z = 0; z < tl; z++)
                if (tbuf[z] >= 'A' && tbuf[z] <= 'Z')
                    tbuf[z] = (char)(tbuf[z] + 32);
        } else {
            strcpy(tbuf, "text");
        }
        if (!strcmp(tbuf, "hidden"))
            return;
        if (!strcmp(tbuf, "checkbox") || !strcmp(tbuf, "radio")) {
            w = 14;
            h = 14;
            snprintf(lbuf, sizeof(lbuf), "%s", NsCssNodeAttr(n, "checked") ? "x" : "");
        } else if (!strcmp(tbuf, "submit") || !strcmp(tbuf, "button") || !strcmp(tbuf, "reset")) {
            const char* lab = value && value[0] ? value : (!strcmp(tbuf, "reset") ? "Reset" : "Submit");
            int captcha = n->id[0] && !strcmp(n->id, "ns-captcha");
            snprintf(lbuf, sizeof(lbuf), "%s", lab);
            /* The check is one row the size of the usual checkbox widget,
             * so the words and the square are the same click. */
            if (captcha) {
                w = 304;
                h = 74;
            } else {
                w = ns_text_width(bs.font_px, lbuf, strlen(lbuf)) + 18;
                h = bs.line_px + 6;
                if (h < 22)
                    h = 22;
            }
        } else {
            snprintf(lbuf, sizeof(lbuf), "%s", value && value[0] ? value : (ph ? ph : ""));
            if (!bs.width_auto && bs.width > 0)
                w = bs.width;
        }
        ns_add_control(&it, ns_pool_add(lbuf, strlen(lbuf)), w, h,
                       (n->id[0] && !strcmp(n->id, "ns-captcha"))
                           ? 0xffffffffu
                           : ((!strcmp(tbuf, "submit") || !strcmp(tbuf, "button") ||
                               !strcmp(tbuf, "reset"))
                                  ? 0xffe8e8e8u
                                  : 0xffffffffu),
                       (n->id[0] && !strcmp(n->id, "ns-captcha")) ? 0xffd3d3d3u : 0xff8a8a8au,
                       bs.font_px, bs.line_px, bs.color, bs.decor, NULL);
        it.node = n;
        ns_list_push(list, &it);
        return;
    }

    if (!strcmp(n->tag, "button")) {
        char lbuf[256];
        size_t o = 0;
        lbuf[0] = 0;
        ns_plain_content(n, lbuf, sizeof(lbuf), &o);
        if (!lbuf[0]) {
            NsCssNode* svg = ns_find_svg(n, 0);
            if (svg && s_svg_lookup) {
                int bw = 0, bh = 0, iw = 0, ih = 0;
                const uint32_t* px = s_svg_lookup(svg, &bw, &bh, &iw, &ih);
                if (px && bw > 0 && bh > 0) {
                    int w = ns_attr_int(svg, "width");
                    int h = ns_attr_int(svg, "height");
                    if (w <= 0)
                        w = 24;
                    if (h <= 0)
                        h = 24;
                    ns_add_control(&it, NULL, w, h, 0, 0, bs.font_px, bs.line_px, bs.color,
                                   bs.decor, NULL);
                    it.kind = 3;
                    it.framed = 0;
                    it.inline_svg = 1;
                    it.node = n;
                    it.pixels = px;
                    it.img_w = w;
                    it.img_h = h;
                    it.src_w = bw;
                    it.src_h = bh;
                    ns_list_push(list, &it);
                    return;
                }
            }
            strcpy(lbuf, "Button");
        }
        ns_add_control(&it, ns_pool_add(lbuf, strlen(lbuf)),
                       ns_text_width(bs.font_px, lbuf, strlen(lbuf)) + 18,
                       bs.line_px + 8 < 24 ? 24 : bs.line_px + 8, 0xffe8e8e8u, 0xff8a8a8au,
                       bs.font_px, bs.line_px, bs.color, bs.decor, NULL);
        it.node = n;
        ns_list_push(list, &it);
        return;
    }

    if (!strcmp(n->tag, "textarea")) {
        char lbuf[512];
        size_t o = 0;
        lbuf[0] = 0;
        ns_plain_content(n, lbuf, sizeof(lbuf), &o);
        ns_add_control(&it, ns_pool_add(lbuf, strlen(lbuf)), 300, 60, 0xffffffffu, 0xff8a8a8au,
                       bs.font_px, bs.line_px, bs.color, bs.decor, NULL);
        it.node = n;
        ns_list_push(list, &it);
        return;
    }

    if (!strcmp(n->tag, "select")) {
        char lbuf[128];
        NsCssNode* c;
        size_t o = 0;
        lbuf[0] = 0;
        for (c = n->first_child; c; c = c->next) {
            if (!c->is_text && !strcmp(c->tag, "option")) {
                ns_plain_content(c, lbuf, sizeof(lbuf), &o);
                break;
            }
        }
        ns_add_control(&it, ns_pool_add(lbuf, strlen(lbuf)), 100, 22, 0xffffffffu, 0xff8a8a8au,
                       bs.font_px, bs.line_px, bs.color, bs.decor, NULL);
        it.node = n;
        ns_list_push(list, &it);
        return;
    }

    /* A non-flex inline-block is an atomic inline box: keep its padding,
     * background and border instead of dropping them into the parent's
     * text line.  This is what makes .button and the desktop nav links
     * occupy the same geometry as a real browser. */
    if (bs.display == NS_DISPLAY_INLINE_BLOCK && !bs.flex_container) {
        char lbuf[512];
        size_t o = 0;
        int pl = bs.padding[3] > 0 ? bs.padding[3] : 0;
        int pr = bs.padding[1] > 0 ? bs.padding[1] : 0;
        int pt = bs.padding[0] > 0 ? bs.padding[0] : 0;
        int pb = bs.padding[2] > 0 ? bs.padding[2] : 0;
        int bl = bs.border_width[3] > 0 ? bs.border_width[3] : 0;
        int br = bs.border_width[1] > 0 ? bs.border_width[1] : 0;
        int bt = bs.border_width[0] > 0 ? bs.border_width[0] : 0;
        int bb = bs.border_width[2] > 0 ? bs.border_width[2] : 0;
        int ml = bs.margin[3] > 0 ? bs.margin[3] : 0;
        int mr = bs.margin[1] > 0 ? bs.margin[1] : 0;
        int mt = bs.margin[0] > 0 ? bs.margin[0] : 0;
        int mb = bs.margin[2] > 0 ? bs.margin[2] : 0;
        int content_w;
        int w;
        int h;
        lbuf[0] = 0;
        ns_plain_content(n, lbuf, sizeof(lbuf), &o);
        if (!lbuf[0] && !n->first_child)
            return;
        if (!lbuf[0]) {
            NsCssNode* svg = ns_find_svg(n, 0);
            if (svg && s_svg_lookup) {
                int bw = 0, bh = 0, iw = 0, ih = 0;
                const uint32_t* px = s_svg_lookup(svg, &bw, &bh, &iw, &ih);
                if (px && bw > 0 && bh > 0) {
                    int w = ns_attr_int(svg, "width");
                    int h = ns_attr_int(svg, "height");
                    if (w <= 0)
                        w = 24;
                    if (h <= 0)
                        h = 24;
                    ns_add_control(&it, NULL, w + ml + mr, h + mt + mb, 0, 0, bs.font_px,
                                   bs.line_px, bs.color, bs.decor, ns_href_box(n));
                    it.kind = 3;
                    it.framed = 0;
                    it.pixels = px;
                    it.img_w = w;
                    it.img_h = h;
                    it.src_w = bw;
                    it.src_h = bh;
                    it.margin[0] = mt;
                    it.margin[1] = mr;
                    it.margin[2] = mb;
                    it.margin[3] = ml;
                    it.position = bs.position;
                    ns_list_push(list, &it);
                    return;
                }
            }
        }
        content_w = ns_text_width(bs.font_px, lbuf, strlen(lbuf));
        if (!bs.width_auto && bs.width > 0)
            content_w = bs.width - (bs.box_sizing ? pl + pr + bl + br : 0);
        if (content_w < 1)
            content_w = 1;
        w = content_w + pl + pr + bl + br;
        h = bs.line_px + pt + pb + bt + bb;
        if (!bs.height_auto && bs.height > 0)
            h = bs.height;
        if (bs.min_width > 0 && w < bs.min_width)
            w = bs.min_width;
        if (bs.min_height > 0 && h < bs.min_height)
            h = bs.min_height;
        if (!bs.max_width || w <= bs.max_width)
            ;
        else {
            int64_t nh = (int64_t)h * bs.max_width / w;
            w = bs.max_width;
            h = nh > 0 ? (int)nh : 1;
        }
        ns_add_control(&it, ns_pool_add(lbuf, strlen(lbuf)), w + ml + mr, h + mt + mb,
                       bs.background, bs.border_width[0] > 0 ? bs.border_color[0] : 0,
                       bs.font_px, bs.line_px, bs.color, bs.decor, ns_href_box(n));
        it.node = n;
        it.kind = NS_BOX_INLINE_BLOCK;
        it.framed = (bs.border_width[0] > 0 || bs.border_width[1] > 0 ||
                     bs.border_width[2] > 0 || bs.border_width[3] > 0);
        it.margin[0] = mt;
        it.margin[1] = mr;
        it.margin[2] = mb;
        it.margin[3] = ml;
        it.offset_x = pl;
        it.position = bs.position;
        if (bs.position == NS_POS_RELATIVE) {
            it.offset_x += !bs.left_auto ? bs.left : (!bs.right_auto ? -bs.right : 0);
            it.offset_y = !bs.top_auto ? bs.top : (!bs.bottom_auto ? -bs.bottom : 0);
        }
        ns_list_push(list, &it);
        return;
    }

    /* Ordinary inline elements: flatten their content onto the current
     * line, preserving inherited styles through the parent chain. */
    ns_collect_children(n, list);
}

/* ---- line building ---- */

typedef struct {
    NsLayoutBox* block;
    NsLayoutBox* line;
    NsLayoutBox* last_run;
    int x;
    int y;
    int pen_x;
    int content_w;
    int line_h;
    int has;
    int pending_space;
    int align;
    int nowrap;
} NsLineCtx;

static void ns_line_start(NsLineCtx* c) {
    if (c->line)
        return;
    c->line = ns_box_new(NS_BOX_LINE);
    if (!c->line)
        return;
    c->line->x = c->x;
    c->line->y = c->y;
    c->line->w = c->content_w;
    c->last_run = NULL;
    c->pen_x = c->x;
    c->line_h = 0;
    c->has = 0;
    c->pending_space = 0;
}

static void ns_register_link(NsLayoutBox* run) {
    if (!run || !run->href || s_nlinks >= NS_LAYOUT_MAX_LINKS)
        return;
    s_links[s_nlinks].x = run->x;
    s_links[s_nlinks].y = run->y;
    s_links[s_nlinks].w = run->w;
    s_links[s_nlinks].h = run->h;
    s_links[s_nlinks].href = run->href;
    s_nlinks++;
}

static int ns_is_form_control(NsCssNode* n) {
    if (!n || n->is_text)
        return 0;
    if (!strcmp(n->tag, "input")) {
        const char* type = NsCssNodeAttr(n, "type");
        if (type && (!strcmp(type, "hidden") || !strcmp(type, "file")))
            return 0;
        return 1;
    }
    return !strcmp(n->tag, "textarea") || !strcmp(n->tag, "button") ||
           !strcmp(n->tag, "select");
}

/* textarea and single-line text inputs.  Submit, checkbox, and file stay
 * on the inline control path. */
static int ns_is_text_field(NsCssNode* n) {
    const char* type;
    char buf[16];
    size_t i, nlen;
    if (!n || n->is_text)
        return 0;
    if (!strcmp(n->tag, "textarea"))
        return 1;
    if (strcmp(n->tag, "input"))
        return 0;
    type = NsCssNodeAttr(n, "type");
    if (!type || !type[0])
        return 1;
    nlen = strlen(type);
    if (nlen >= sizeof(buf))
        nlen = sizeof(buf) - 1;
    for (i = 0; i < nlen; i++) {
        char c = type[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        buf[i] = c;
    }
    buf[nlen] = 0;
    return !strcmp(buf, "text") || !strcmp(buf, "search") || !strcmp(buf, "email") ||
           !strcmp(buf, "url") || !strcmp(buf, "password") || !strcmp(buf, "number") ||
           !strcmp(buf, "tel");
}

static void ns_register_control(NsLayoutBox* run) {
    if (!run || !run->node || !ns_is_form_control(run->node))
        return;
    if (s_nformctrls >= NS_LAYOUT_MAX_CONTROLS)
        return;
    s_formctrls[s_nformctrls].x = run->x;
    s_formctrls[s_nformctrls].y = run->y;
    s_formctrls[s_nformctrls].w = run->w;
    s_formctrls[s_nformctrls].h = run->h;
    s_formctrls[s_nformctrls].node = run->node;
    s_nformctrls++;
}

static void ns_line_run(NsLineCtx* c, int kind, const char* text, size_t len, int w,
                        const NsInlineItem* item) {
    NsLayoutBox* run;
    if (!c->line)
        ns_line_start(c);
    if (!c->line)
        return;
    run = ns_box_new(kind);
    if (!run)
        return;
    run->x = c->pen_x;
    run->y = c->y;
    run->node = item->node;
    run->w = w;
    run->h = c->line_h > 0 ? c->line_h : item->line_px;
    run->font_px = item->font_px;
    run->color = item->color;
    run->decor = item->decor;
    run->href = item->href;
    run->text = text;
    run->text_len = len;
    run->label = item->label;
    run->bg = item->bg;
    run->border_color = item->border_color;
    run->pixels = item->pixels;
    run->img_w = item->img_w;
    run->img_h = item->img_h;
    run->src_w = item->src_w;
    run->src_h = item->src_h;
    run->position = item->position;
    run->framed = item->framed;
    memcpy(run->margin, item->margin, sizeof(run->margin));
    run->offset_x = item->offset_x;
    run->offset_y = item->offset_y;
    run->inline_svg = item->inline_svg;
    if (kind == NS_BOX_CONTROL || kind == NS_BOX_IMAGE || kind == NS_BOX_INLINE_BLOCK) {
        run->w = w;
        run->h = item->height;
    }
    if (!c->line->first_child) {
        c->line->first_child = run;
    } else if (c->last_run) {
        c->last_run->next = run;
    }
    c->last_run = run;
    c->pen_x += w;
}

static void ns_line_finish(NsLineCtx* c) {
    NsLayoutBox* run;
    int line_h;
    int line_w;
    int shift = 0;
    int baseline;
    if (!c || !c->line)
        return;
    if (!c->line->first_child) {
        free(c->line);
        c->line = NULL;
        c->last_run = NULL;
        c->pen_x = c->x;
        c->line_h = 0;
        c->has = 0;
        c->pending_space = 0;
        return;
    }
    line_h = c->line_h;
    if (line_h < 12)
        line_h = 12;
    line_w = c->pen_x - c->x;
    if (line_w < 0)
        line_w = 0;
    if (c->align == 1)
        shift = (c->content_w - line_w) / 2;
    else if (c->align == 2)
        shift = c->content_w - line_w;
    else {
        /* A lone image in a block is usually a logo or a figure.  Grid
         * containers (Google's logo wrapper) centre it with justify-items,
         * which libcss does not expose to us; treat the common one-image
         * line the same way as text-align:center. */
        NsLayoutBox* r = c->line->first_child;
        if (r && r->kind == NS_BOX_IMAGE && r->inline_svg && !r->next)
            shift = (c->content_w - line_w) / 2;
    }
    if (shift < 0)
        shift = 0;
    {
        int font_px = 14;
        int em;
        for (run = c->line->first_child; run; run = run->next) {
            if (run->font_px > font_px)
                font_px = run->font_px;
        }
        em = ns_font_ascent(font_px) + ns_font_descent(font_px);
        if (line_h < em)
            line_h = em;
        /* The draw call takes this y as the baseline, then the glyph
         * hangs from it by its own ascent. */
        baseline = ns_baseline_y(c->line->y, line_h, font_px);
    }
    for (run = c->line->first_child; run; run = run->next) {
        int rh = run->h;
        run->x += shift;
        if (run->kind == NS_BOX_CONTROL || run->kind == NS_BOX_IMAGE ||
            run->kind == NS_BOX_INLINE_BLOCK) {
            run->y = c->line->y + (line_h - rh) / 2;
            run->h = rh;
        } else {
            run->y = c->line->y;
            run->h = line_h;
        }
        run->baseline = baseline;
        if (run->href)
            ns_register_link(run);
        if (run->node)
            ns_register_control(run);
    }
    c->line->h = line_h;
    c->line->w = line_w;
    ns_append_box(c->block, c->line);
    c->line = NULL;
    c->last_run = NULL;
    c->y += line_h;
    c->pen_x = c->x;
    c->line_h = 0;
    c->has = 0;
    c->pending_space = 0;
}

static void ns_place_word(NsLineCtx* c, const NsInlineItem* item, const char* text, size_t len) {
    int ww = ns_text_width(item->font_px, text, len);
    int space_w = 0;
    if (len == 0)
        return;
    if (c->has && c->pending_space && !item->pre)
        space_w = ns_char_width(item->font_px, ' ');
    if (!item->nowrap && c->has && c->pen_x + space_w + ww > c->x + c->content_w) {
        ns_line_finish(c);
        space_w = 0;
    }
    if (!c->line)
        ns_line_start(c);
    if (space_w > 0)
        ns_line_run(c, NS_BOX_RUN, ns_pool_add(" ", 1), 1, space_w, item);
    c->pending_space = 0;
    /* A word wider than the whole column: hard-break at characters. */
    if (ww > c->content_w && c->content_w > 0) {
        size_t start = 0;
        while (start < len) {
            int used = 0;
            size_t take = 0;
            while (start + take < len) {
                int cw = ns_char_width(item->font_px, text[start + take]);
                if (used + cw > c->content_w && take > 0)
                    break;
                used += cw;
                take++;
            }
            if (take == 0)
                take = 1;
            if (c->has)
                ns_line_finish(c);
            if (!c->line)
                ns_line_start(c);
            ns_line_run(c, NS_BOX_RUN, text + start, take, used, item);
            c->line_h = item->line_px > c->line_h ? item->line_px : c->line_h;
            c->has = 1;
            start += take;
            if (start < len)
                ns_line_finish(c);
        }
    } else {
        ns_line_run(c, NS_BOX_RUN, text, len, ww, item);
        c->line_h = item->line_px > c->line_h ? item->line_px : c->line_h;
        c->has = 1;
    }
}

static void ns_place_control(NsLineCtx* c, const NsInlineItem* item) {
    int w = item->width > 0 ? item->width : 40;
    int h = item->height > 0 ? item->height : 20;
    int space_w = (c->has && c->pending_space) ? ns_char_width(item->font_px, ' ') : 0;
    if (!item->nowrap && c->has && c->pen_x + space_w + w > c->x + c->content_w) {
        ns_line_finish(c);
        space_w = 0;
    }
    if (!c->line)
        ns_line_start(c);
    if (space_w > 0)
        ns_line_run(c, NS_BOX_RUN, ns_pool_add(" ", 1), 1, space_w, item);
    c->pending_space = 0;
    ns_line_run(c, item->kind == 3 ? NS_BOX_IMAGE
                                  : (item->kind == 6 ? NS_BOX_INLINE_BLOCK : NS_BOX_CONTROL),
                NULL, 0, w, item);
    c->line_h = h > c->line_h ? h : c->line_h;
    if (item->line_px > c->line_h)
        c->line_h = item->line_px;
    c->has = 1;
}

static int ns_layout_lines(NsLayoutBox* block, NsInlineList* list, int y) {
    NsLineCtx c;
    int li;
    if (!block || !list || list->n == 0)
        return y;
    memset(&c, 0, sizeof(c));
    c.block = block;
    c.x = block->cx;
    c.y = y;
    c.pen_x = block->cx;
    c.content_w = block->cw;
    c.align = block->sty.text_align;
    for (li = 0; li < list->n; li++) {
        NsInlineItem* item = &list->items[li];
        if (item->kind == 1) {
            if (!c.line || !c.line->first_child) {
                int step = item->line_px > 12 ? item->line_px : 12;
                if (c.line) {
                    free(c.line);
                    c.line = NULL;
                }
                c.y += step;
                c.pen_x = c.x;
                c.line_h = 0;
                c.has = 0;
                c.pending_space = 0;
            } else {
                ns_line_finish(&c);
            }
            continue;
        }
        if (item->kind != 0) {
            ns_place_control(&c, item);
            continue;
        }
        {
            const char* t = item->text;
            size_t len = item->text_len;
            size_t i = 0;
            while (i < len) {
                char ch = t[i];
                if (ch == '\n') {
                    ns_line_finish(&c);
                    i++;
                    continue;
                }
                if (ch == ' ' || ch == '\t') {
                    if (item->pre) {
                        if (!c.line)
                            ns_line_start(&c);
                        if (ch == '\t') {
                            int k;
                            for (k = 0; k < 4; k++) {
                                int sw = ns_char_width(item->font_px, ' ');
                                ns_line_run(&c, NS_BOX_RUN, ns_pool_add(" ", 1), 1, sw, item);
                            }
                        } else {
                            int sw = ns_char_width(item->font_px, ' ');
                            ns_line_run(&c, NS_BOX_RUN, ns_pool_add(" ", 1), 1, sw, item);
                        }
                    } else {
                        c.pending_space = 1;
                    }
                    i++;
                    continue;
                }
                {
                    size_t start = i;
                    while (i < len && t[i] != ' ' && t[i] != '\t' && t[i] != '\n')
                        i++;
                    ns_place_word(&c, item, t + start, i - start);
                }
            }
        }
    }
    ns_line_finish(&c);
    return c.y;
}

/* ---- block layout ---- */

static NsLayoutBox* s_cur_containing;
static NsLayoutBox* ns_layout_block(NsCssNode* node, int x0, int border_y, int avail_w, int depth);

static void ns_append_abs(NsLayoutBox* box) {
    if (!box)
        return;
    box->abs_next = NULL;
    if (s_abs_tail)
        s_abs_tail->abs_next = box;
    else
        s_abs_head = box;
    s_abs_tail = box;
}

static void ns_layout_out_of_flow(NsCssNode* child, NsLayoutBox* parent, int parent_w, int depth) {
    NsCssBoxStyle cbs;
    NsLayoutBox* ab;
    NsLayoutBox* cb;
    int cbx, cby, cbw, cbh;
    int x, y;
    int link_start = s_nlinks;
    int ctrl_start = s_nformctrls;
    int li;
    NsCssNodeBoxStyle(child, parent_w, 0, s_viewport_w, s_viewport_h, &cbs);
    if (cbs.position != NS_POS_ABSOLUTE && cbs.position != NS_POS_FIXED)
        return;
    cb = (cbs.position == NS_POS_FIXED) ? NULL : s_cur_containing;
    cbx = cb ? cb->cx : 0;
    cby = cb ? cb->cy : 0;
    cbw = cb ? cb->cw : s_viewport_w;
    cbh = cb ? cb->ch : s_viewport_h;
    if (cbw <= 0)
        cbw = parent_w > 0 ? parent_w : s_viewport_w;
    ab = ns_layout_block(child, 0, 0, cbw, depth + 1);
    if (!ab)
        return;
    if (!cbs.left_auto)
        x = cbx + cbs.left;
    else if (!cbs.right_auto)
        x = cbx + cbw - cbs.right - ab->w;
    else
        x = cbx + (parent ? parent->cx - cbx : 0);
    if (!cbs.top_auto)
        y = cby + cbs.top;
    else if (!cbs.bottom_auto)
        y = cby + cbh - cbs.bottom - ab->h;
    else
        y = cby;
    ab->x = x;
    ab->y = y;
    ab->position = cbs.position;
    ab->z_index = cbs.z_index;
    /* Link rectangles are recorded while the subtree is laid out at the
     * origin.  Shift the ones belonging to this out-of-flow box into page
     * coordinates now that its final position is known. */
    for (li = link_start; li < s_nlinks; li++) {
        s_links[li].x += x;
        s_links[li].y += y;
    }
    for (li = ctrl_start; li < s_nformctrls; li++) {
        s_formctrls[li].x += x;
        s_formctrls[li].y += y;
    }
    ns_append_abs(ab);
}

typedef struct {
    NsCssNode* node;
    NsCssBoxStyle st;
    int outer_w;
    int extra_h;
    NsLayoutBox* box;
    int link0, link1, ctrl0, ctrl1;
} NsFlexItem;

/* Children and hit rectangles are stored in page coordinates, so a flex
 * cross-axis shift has to move the subtree, not only the item's border. */
static void ns_shift_box(NsLayoutBox* b, int dx, int dy) {
    NsLayoutBox* c;
    if (!b || (dx == 0 && dy == 0))
        return;
    b->x += dx;
    b->y += dy;
    b->cx += dx;
    b->cy += dy;
    if (b->baseline)
        b->baseline += dy;
    for (c = b->first_child; c; c = c->next)
        ns_shift_box(c, dx, dy);
}

static void ns_shift_hits(int link0, int link1, int ctrl0, int ctrl1, int dx, int dy) {
    int i;
    if (dx == 0 && dy == 0)
        return;
    if (link0 < 0)
        link0 = 0;
    if (ctrl0 < 0)
        ctrl0 = 0;
    if (link1 > s_nlinks)
        link1 = s_nlinks;
    if (ctrl1 > s_nformctrls)
        ctrl1 = s_nformctrls;
    for (i = link0; i < link1; i++) {
        s_links[i].x += dx;
        s_links[i].y += dy;
    }
    for (i = ctrl0; i < ctrl1; i++) {
        s_formctrls[i].x += dx;
        s_formctrls[i].y += dy;
    }
}

/* self: 0 auto, 1 center, 2 flex-end, 3 flex-start, 4 stretch.
 * items: 0 stretch, 1 center, 2 flex-end, 3 flex-start. */
static int ns_used_align(uint8_t self, uint8_t items) {
    if (self == 1 || self == 2 || self == 3)
        return (int)self;
    if (self == 4)
        return 0;
    return (int)items;
}

static void ns_sync_control(NsLayoutBox* b) {
    int i;
    if (!b || !b->node)
        return;
    for (i = 0; i < s_nformctrls; i++) {
        if (s_formctrls[i].node != b->node)
            continue;
        s_formctrls[i].x = b->x;
        s_formctrls[i].y = b->y;
        s_formctrls[i].w = b->w;
        s_formctrls[i].h = b->h;
    }
}

static void ns_grow_box(NsLayoutBox* b, int new_h) {
    NsLayoutBox* c;
    NsLayoutBox* only = NULL;
    int extra;
    int nblock = 0;
    if (!b || new_h <= b->h)
        return;
    extra = new_h - b->h;
    b->h = new_h;
    b->ch += extra;
    for (c = b->first_child; c; c = c->next) {
        if (c->kind != NS_BOX_BLOCK && c->kind != NS_BOX_CONTROL)
            continue;
        nblock++;
        only = c;
    }
    if (nblock == 1)
        ns_grow_box(only, only->h + extra);
    ns_sync_control(b);
}

/* Content-box cross size from a definite height or min-height.  0 when
 * the container's cross size comes only from its items. */
static int ns_flex_inner_h(const NsLayoutBox* box) {
    int frame = 0;
    int inner = 0;
    int mh;
    if (!box)
        return 0;
    if (box->sty.box_sizing) {
        int pt = box->sty.padding[0] > 0 ? box->sty.padding[0] : 0;
        int pb = box->sty.padding[2] > 0 ? box->sty.padding[2] : 0;
        int bt = box->sty.border_width[0] > 0 ? box->sty.border_width[0] : 0;
        int bb = box->sty.border_width[2] > 0 ? box->sty.border_width[2] : 0;
        frame = pt + pb + bt + bb;
    }
    if (!box->sty.height_auto && box->sty.height > 0) {
        inner = box->sty.height - frame;
        if (inner < 0)
            inner = 0;
    }
    if (box->sty.min_height > 0) {
        mh = box->sty.min_height - frame;
        if (mh < 0)
            mh = 0;
        if (mh > inner)
            inner = mh;
    }
    return inner;
}

static void ns_align_flex_line(NsFlexItem* items, int begin, int end, int cross, int align_items) {
    int i;
    if (!items || cross <= 0)
        return;
    for (i = begin; i < end; i++) {
        NsLayoutBox* b = items[i].box;
        int used;
        int dy = 0;
        if (!b)
            continue;
        used = ns_used_align(items[i].st.align_self, (uint8_t)align_items);
        if (used == 0 && items[i].st.height_auto) {
            int target = cross - items[i].extra_h;
            if (target > b->h)
                ns_grow_box(b, target);
        }
        if (used == 1 && cross > b->h)
            dy = (cross - b->h) / 2;
        else if (used == 2 && cross > b->h)
            dy = cross - b->h;
        if (!dy)
            continue;
        ns_shift_box(b, 0, dy);
        ns_shift_hits(items[i].link0, items[i].link1, items[i].ctrl0, items[i].ctrl1, 0, dy);
    }
}

/* Max-content width estimate for a flex item.  CSS flexbox sizes an
 * "auto" item from its contents, not by dividing the row equally.  Google's
 * home page relies on that everywhere (nav links, the header cluster, the
 * search field), so give text/controls their real width and let flex-grow
 * distribute whatever is left. */
static int ns_intrinsic_text_width(int font_px, const char* text, size_t len) {
    int w = 0;
    int word = 0;
    int space = 0;
    size_t i;
    if (!text)
        return 0;
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (word) {
                w += word;
                word = 0;
                space = 1;
            }
            continue;
        }
        if (space) {
            w += ns_char_width(font_px, ' ');
            space = 0;
        }
        word += ns_char_width(font_px, (char)ch);
    }
    return w + word;
}

static int ns_intrinsic_width(NsCssNode* n, int avail_w, int depth) {
    NsCssBoxStyle st;
    int inner = 0;
    int pl, pr, bl, br, ml, mr;
    NsCssNode* c;
    if (!n || n->is_text || depth > 8)
        return 0;
    NsCssNodeBoxStyle(n, avail_w, 0, s_viewport_w, s_viewport_h, &st);
    if (st.display == NS_DISPLAY_NONE)
        return 0;
    if (!strcmp(n->tag, "br"))
        return 0;
    if (!strcmp(n->tag, "svg")) {
        const char* w = NsDomAttr(n, "width");
        if (w && w[0])
            return atoi(w);
        return 24;
    }
    if (!strcmp(n->tag, "img")) {
        const char* w = NsDomAttr(n, "width");
        if (w && w[0])
            return atoi(w);
        return 0;
    }
    if (!strcmp(n->tag, "textarea"))
        return 0;
    if (!strcmp(n->tag, "input")) {
        const char* v = NsDomAttr(n, "value");
        if (!v || !v[0])
            v = NsDomAttr(n, "placeholder");
        if (!v || !v[0])
            v = NsDomAttr(n, "aria-label");
        if (!v || !v[0])
            v = "Button";
        return ns_intrinsic_text_width(st.font_px, v, strlen(v)) + 20;
    }
    for (c = n->first_child; c; c = c->next) {
        NsCssBoxStyle cs;
        int cw;
        if (c->is_text) {
            inner += ns_intrinsic_text_width(st.font_px, c->text, c->text_len);
            continue;
        }
        if (ns_skip_node(c))
            continue;
        NsCssNodeBoxStyle(c, avail_w, 0, s_viewport_w, s_viewport_h, &cs);
        if (cs.position == NS_POS_ABSOLUTE || cs.position == NS_POS_FIXED)
            continue;
        cw = ns_intrinsic_width(c, avail_w, depth + 1);
        if (st.flex_container || cs.display == NS_DISPLAY_INLINE ||
            cs.display == NS_DISPLAY_INLINE_BLOCK)
            inner += cw;
        else if (cw > inner)
            inner = cw;
    }
    pl = st.padding[3] > 0 ? st.padding[3] : 0;
    pr = st.padding[1] > 0 ? st.padding[1] : 0;
    bl = st.border_width[3] > 0 ? st.border_width[3] : 0;
    br = st.border_width[1] > 0 ? st.border_width[1] : 0;
    ml = st.margin[3] > 0 ? st.margin[3] : 0;
    mr = st.margin[1] > 0 ? st.margin[1] : 0;
    /* Auto width means content + padding regardless of box-sizing.
     * box-sizing only changes how an explicit width is interpreted. */
    inner += pl + pr + bl + br;
    if (st.min_width > 0 && inner < st.min_width)
        inner = st.min_width;
    return inner + ml + mr;
}

static int ns_layout_flex_row(NsLayoutBox* box, NsCssNode* node, int content_w, int y, int depth) {
    NsCssNode* child;
    NsFlexItem* items;
    int n = 0, i, cursor = 0;
    int count = 0;
    int total = 0;
    int row_start = 0;
    int line_h = 0;
    int gap = 0;
    int start = 0;
    if (!box || content_w <= 0)
        return y;
    for (child = node->first_child; child; child = child->next) {
        if (child->is_text || ns_skip_node(child))
            continue;
        count++;
    }
    if (count <= 0)
        return y;
    items = (NsFlexItem*)calloc((size_t)count, sizeof(*items));
    if (!items)
        return y;
    n = 0;
    for (child = node->first_child; child; child = child->next) {
        NsCssBoxStyle st;
        int outer, pad, ml, mr, pl, pr, bl, br;
        if (child->is_text || ns_skip_node(child))
            continue;
        NsCssNodeBoxStyle(child, content_w, 0, s_viewport_w, s_viewport_h, &st);
        if (st.position == NS_POS_ABSOLUTE || st.position == NS_POS_FIXED) {
            ns_layout_out_of_flow(child, box, content_w, depth);
            continue;
        }
        ml = st.margin[3] > 0 ? st.margin[3] : 0;
        mr = st.margin[1] > 0 ? st.margin[1] : 0;
        pl = st.padding[3] > 0 ? st.padding[3] : 0;
        pr = st.padding[1] > 0 ? st.padding[1] : 0;
        bl = st.border_width[3] > 0 ? st.border_width[3] : 0;
        br = st.border_width[1] > 0 ? st.border_width[1] : 0;
        pad = st.box_sizing ? 0 : pl + pr + bl + br;
        if (!st.flex_basis_auto && st.flex_basis > 0)
            outer = st.flex_basis + pad + ml + mr;
        else if (!st.width_auto && st.width > 0)
            outer = st.width + pad + ml + mr;
        else
            outer = ns_intrinsic_width(child, content_w, 0);
        if (st.min_width > 0 && outer < st.min_width + ml + mr)
            outer = st.min_width + ml + mr;
        if (outer < 0)
            outer = 0;
        items[n].node = child;
        items[n].st = st;
        items[n].outer_w = outer;
        items[n].extra_h = (st.margin[0] > 0 ? st.margin[0] : 0) +
                           (st.margin[2] > 0 ? st.margin[2] : 0);
        total += items[n].outer_w;
        n++;
    }
    if (n <= 0) {
        free(items);
        return y;
    }
    if (!box->sty.flex_wrap) {
        int grow_total = 0;
        for (i = 0; i < n; i++) {
            if (items[i].st.flex_grow > 0)
                grow_total += items[i].st.flex_grow;
        }
        if (grow_total > 0 && total < content_w) {
            int free = content_w - total;
            for (i = 0; i < n; i++) {
                if (items[i].st.flex_grow > 0)
                    items[i].outer_w += (free * items[i].st.flex_grow) / grow_total;
            }
            total = content_w;
        } else if (total > content_w && total > 0) {
            int shrink_total = 0;
            int excess = total - content_w;
            int use_all = 0;
            for (i = 0; i < n; i++) {
                if (items[i].st.flex_shrink > 0)
                    shrink_total += items[i].outer_w * items[i].st.flex_shrink;
            }
            if (shrink_total <= 0) {
                use_all = 1;
                for (i = 0; i < n; i++)
                    shrink_total += items[i].outer_w;
            }
            if (shrink_total > 0) {
                for (i = 0; i < n; i++) {
                    int weight = use_all ? items[i].outer_w
                                         : (items[i].st.flex_shrink > 0
                                                ? items[i].outer_w * items[i].st.flex_shrink
                                                : 0);
                    int cut;
                    if (weight <= 0)
                        continue;
                    cut = (int)(((int64_t)excess * weight) / shrink_total);
                    items[i].outer_w -= cut;
                    if (items[i].outer_w < 1)
                        items[i].outer_w = 1;
                }
            }
            total = content_w;
        }
    }
    if (!box->sty.flex_wrap && box->sty.justify_content == 3 && n > 1)
        gap = (content_w - total) / (n - 1);
    else if (box->sty.justify_content == 1)
        start = (content_w - total) / 2;
    else if (box->sty.justify_content == 2)
        start = content_w - total;
    if (start < 0)
        start = 0;
    if (gap < 0)
        gap = 0;
    cursor = start;
    row_start = 0;
    for (i = 0; i < n; i++) {
        NsLayoutBox* cb;
        int cby = y;
        if (box->sty.flex_wrap && cursor > 0 && cursor + items[i].outer_w > content_w) {
            if (line_h > 0)
                ns_align_flex_line(items, row_start, i, line_h, box->sty.align_items);
            y += line_h;
            cursor = 0;
            line_h = 0;
            cby = y;
            row_start = i;
        }
        items[i].link0 = s_nlinks;
        items[i].ctrl0 = s_nformctrls;
        cb = ns_layout_block(items[i].node, box->cx + cursor, cby, items[i].outer_w, depth + 1);
        items[i].link1 = s_nlinks;
        items[i].ctrl1 = s_nformctrls;
        items[i].box = cb;
        if (cb) {
            ns_append_box(box, cb);
            if (cb->h + items[i].extra_h > line_h)
                line_h = cb->h + items[i].extra_h;
        }
        cursor += items[i].outer_w + (box->sty.flex_wrap ? 0 : gap);
        if (box->sty.flex_wrap && cursor >= content_w) {
            if (line_h > 0)
                ns_align_flex_line(items, row_start, i + 1, line_h, box->sty.align_items);
            y += line_h;
            cursor = 0;
            line_h = 0;
            row_start = i + 1;
        }
    }
    if (line_h > 0) {
        int cross = line_h;
        if (!box->sty.flex_wrap) {
            int inner = ns_flex_inner_h(box);
            if (inner > cross)
                cross = inner;
        }
        ns_align_flex_line(items, row_start, n, cross, box->sty.align_items);
        line_h = cross;
    }
    free(items);
    return y + line_h;
}

/* Column flex containers are common on modern pages (Google's whole home
 * screen is one).  Laying their children out as a row squeezes every
 * section into a narrow strip, so handle the vertical main axis first:
 * each in-flow child gets the container's content width and the next one
 * starts below it. */
static int ns_layout_flex_column(NsLayoutBox* box, NsCssNode* node, int content_w, int y,
                                 int depth) {
    NsCssNode* child;
    int cursor = y;
    int have = 0;
    if (!box || content_w <= 0)
        return y;
    for (child = node->first_child; child; child = child->next) {
        NsCssBoxStyle st;
        NsLayoutBox* cb;
        int mt, mb;
        int link_start;
        int ctrl_start;
        int used;
        int shift;
        if (child->is_text || ns_skip_node(child))
            continue;
        NsCssNodeBoxStyle(child, content_w, 0, s_viewport_w, s_viewport_h, &st);
        if (st.position == NS_POS_ABSOLUTE || st.position == NS_POS_FIXED) {
            ns_layout_out_of_flow(child, box, content_w, depth);
            continue;
        }
        mt = st.margin[0] > 0 ? st.margin[0] : 0;
        mb = st.margin[2] > 0 ? st.margin[2] : 0;
        if (have || mt)
            cursor += mt;
        link_start = s_nlinks;
        ctrl_start = s_nformctrls;
        cb = ns_layout_block(child, box->cx, cursor, content_w, depth + 1);
        if (!cb)
            continue;
        used = ns_used_align(st.align_self, box->sty.align_items);
        shift = 0;
        if ((used == 1 || used == 2) && cb->w < content_w)
            shift = used == 1 ? (content_w - cb->w) / 2 : content_w - cb->w;
        if (shift > 0) {
            ns_shift_box(cb, shift, 0);
            ns_shift_hits(link_start, s_nlinks, ctrl_start, s_nformctrls, shift, 0);
        }
        ns_append_box(box, cb);
        cursor = cb->y + cb->h + mb;
        have = 1;
    }
    return cursor;
}

static NsLayoutBox* ns_layout_block(NsCssNode* node, int x0, int border_y, int avail_w, int depth) {
    NsCssBoxStyle bs;
    NsLayoutBox* box;
    int ml, mr, mt, mb, pl, pr, pt, pb, bl, br, bt, bb;
    int content_w;
    int bx, cx, cy;
    int y;
    int pending_mb = 0;
    int have_block = 0;
    NsInlineList list;
    NsCssNode* child;
    NsLayoutBox* saved_cb = s_cur_containing;
    int saved_base_w = s_collect_base_w;

    if (!node || node->is_text || depth > NS_LAYOUT_MAX_DEPTH)
        return NULL;
    if (ns_skip_node(node))
        return NULL;
    NsCssNodeBoxStyle(node, avail_w, 0, s_viewport_w, s_viewport_h, &bs);
    if (!strcmp(node->tag, "html") || !strcmp(node->tag, "body")) {
        if (bs.display == NS_DISPLAY_NONE || bs.display == NS_DISPLAY_INLINE)
            bs.display = NS_DISPLAY_BLOCK;
    }
    if (bs.display == NS_DISPLAY_NONE)
        return NULL;

    ml = bs.margin[3];
    mr = bs.margin[1];
    mt = bs.margin[0];
    mb = bs.margin[2];
    pl = bs.padding[3];
    pr = bs.padding[1];
    pt = bs.padding[0];
    pb = bs.padding[2];
    bl = bs.border_width[3];
    br = bs.border_width[1];
    bt = bs.border_width[0];
    bb = bs.border_width[2];

    if (!bs.width_auto) {
        content_w = bs.width;
        if (bs.box_sizing)
            content_w -= pl + pr + bl + br;
    } else {
        content_w = avail_w - ml - mr - pl - pr - bl - br;
    }
    if (content_w < 0)
        content_w = 0;
    if (bs.min_width > 0) {
        int minw = bs.min_width;
        if (bs.box_sizing)
            minw -= pl + pr + bl + br;
        if (minw < 0)
            minw = 0;
        if (content_w < minw)
            content_w = minw;
    }
    if (bs.max_width > 0) {
        int maxw = bs.max_width;
        if (bs.box_sizing)
            maxw -= pl + pr + bl + br;
        if (maxw < 0)
            maxw = 0;
        if (content_w > maxw)
            content_w = maxw;
    }

    bx = x0 + ml;
    cx = bx + bl + pl;
    cy = border_y + bt + pt;

    box = ns_box_new(NS_BOX_BLOCK);
    if (!box)
        return NULL;
    box->node = node;
    box->x = bx;
    box->y = border_y;
    box->w = bl + pl + content_w + pr + br;
    box->h = bt + pt + pb + bb;
    box->cx = cx;
    box->cy = cy;
    box->cw = content_w;
    box->ch = 0;
    box->sty = bs;
    box->bg = bs.background;
    box->color = bs.color;
    box->font_px = bs.font_px;
    if (bs.position != NS_POS_STATIC)
        s_cur_containing = box;
    if (bs.position == NS_POS_RELATIVE) {
        box->offset_x = !bs.left_auto ? bs.left : (!bs.right_auto ? -bs.right : 0);
        box->offset_y = !bs.top_auto ? bs.top : (!bs.bottom_auto ? -bs.bottom : 0);
    }
    s_collect_base_w = content_w;

    if (!strcmp(node->tag, "hr")) {
        box->ch = 0;
        box->h = bt + pt + pb + bb;
        if (box->h < 1)
            box->h = 1;
        s_cur_containing = saved_cb;
        s_collect_base_w = saved_base_w;
        return box;
    }

    /* A flex textarea (the Google search field) has no element children.
     * Laying it out as an empty flex container never registers a control,
     * so typing has nowhere to land.  The field is this box. */
    if (ns_is_text_field(node)) {
        int lh = bs.line_px > 16 ? bs.line_px : 16;
        int field_h = lh;
        if (!bs.height_auto && bs.height > 0) {
            int th = bs.height;
            if (bs.box_sizing)
                th -= pt + pb + bt + bb;
            if (th < 0)
                th = 0;
            if (th > field_h)
                field_h = th;
        }
        box->ch = field_h;
        box->h = bt + pt + field_h + pb + bb;
        if (box->h < 1)
            box->h = 1;
        ns_register_control(box);
        s_cur_containing = saved_cb;
        s_collect_base_w = saved_base_w;
        return box;
    }

    memset(&list, 0, sizeof(list));
    y = cy;
    have_block = 0;

    if (bs.flex_container) {
        if (bs.flex_direction == 1)
            y = ns_layout_flex_column(box, node, content_w, y, depth);
        else
            y = ns_layout_flex_row(box, node, content_w, y, depth);
        goto finish_block;
    }

    /* List marker: put it at the head of the first line. */
    if (!strcmp(node->tag, "li") && bs.list_type != 0) {
        char mbuf[32];
        int idx = 1;
        NsCssNode* p;
        NsInlineItem mi;
        const char* mark;
        for (p = node->prev; p; p = p->prev) {
            if (!p->is_text && !strcmp(p->tag, "li"))
                idx++;
        }
        if (bs.list_type == 4)
            snprintf(mbuf, sizeof(mbuf), "%d. ", idx);
        else if (bs.list_type == 2)
            strcpy(mbuf, "o ");
        else if (bs.list_type == 3)
            strcpy(mbuf, "- ");
        else
            strcpy(mbuf, "* ");
        mark = ns_pool_add(mbuf, strlen(mbuf));
        memset(&mi, 0, sizeof(mi));
        mi.kind = 0;
        mi.text = mark;
        mi.text_len = strlen(mbuf);
        mi.font_px = bs.font_px;
        mi.line_px = bs.line_px;
        mi.color = bs.color;
        mi.decor = bs.decor;
        mi.nowrap = 1;
        ns_list_push(&list, &mi);
    }

    for (child = node->first_child; child; child = child->next) {
        if (child->is_text) {
            ns_collect_inline(child, node, &list);
            continue;
        }
        if (ns_skip_node(child))
            continue;
        {
            NsCssBoxStyle cbs;
            NsCssNodeBoxStyle(child, content_w, 0, s_viewport_w, s_viewport_h, &cbs);
            if (cbs.position == NS_POS_ABSOLUTE || cbs.position == NS_POS_FIXED) {
                ns_layout_out_of_flow(child, box, content_w, depth);
                continue;
            }
        }
        if (ns_is_block(child)) {
            if (list.n) {
                y += pending_mb;
                pending_mb = 0;
                y = ns_layout_lines(box, &list, y);
                list.n = 0;
            }
            {
                NsCssBoxStyle cbs;
                int gap;
                int cby;
                NsLayoutBox* cb;
                NsCssNodeBoxStyle(child, content_w, 0, s_viewport_w, s_viewport_h, &cbs);
                gap = cbs.margin[0];
                if (have_block && pending_mb > gap)
                    gap = pending_mb;
                y += gap;
                cby = y;
                cb = ns_layout_block(child, cx, cby, content_w, depth + 1);
                if (cb) {
                    ns_append_box(box, cb);
                    y = cby + cb->h;
                    pending_mb = cb->sty.margin[2];
                    have_block = 1;
                } else {
                    pending_mb = 0;
                }
            }
        } else {
            ns_collect_inline(child, node, &list);
        }
    }
    if (list.n || pending_mb) {
        y += pending_mb;
        pending_mb = 0;
        if (list.n)
            y = ns_layout_lines(box, &list, y);
    }
finish_block:
    free(list.items);

    {
        int content_h = y - cy;
        if (have_block && pending_mb > 0)
            content_h += pending_mb;
        /* A specified height is a minimum for flow purposes here: the
         * engine has no overflow clipping yet, so shrinking the flow
         * footprint would make the next sibling paint over the children
         * that still extend below the nominal box. */
        if (!bs.height_auto && bs.height > 0) {
            int th = bs.height;
            if (bs.box_sizing)
                th -= pt + pb + bt + bb;
            if (th < 0)
                th = 0;
            if (content_h < th)
                content_h = th;
        }
        if (bs.min_height > 0) {
            int th = bs.min_height;
            if (bs.box_sizing)
                th -= pt + pb + bt + bb;
            if (th < 0)
                th = 0;
            if (content_h < th)
                content_h = th;
        }
        if (content_h < 0)
            content_h = 0;
        box->ch = content_h;
        box->h = bt + pt + content_h + pb + bb;
    }
    /* The border box is the click target.  Text runs inside an anchor stay
     * registered too; hit testing keeps the smaller one. */
    if (ns_is_form_control(node))
        ns_register_control(box);
    if (!strcmp(node->tag, "a")) {
        const char* href = NsCssNodeAttr(node, "href");
        if (href && href[0]) {
            box->href = href;
            ns_register_link(box);
        }
    }
    s_cur_containing = saved_cb;
    s_collect_base_w = saved_base_w;
    return box;
}

/* ---- painting ---- */

typedef struct {
    int x, y, w, h;
} NsClip;

static int ns_clip_rect(int* x, int* y, int* w, int* h, const NsClip* c) {
    int x1 = *x;
    int y1 = *y;
    int x2 = *x + *w;
    int y2 = *y + *h;
    if (x1 < c->x)
        x1 = c->x;
    if (y1 < c->y)
        y1 = c->y;
    if (x2 > c->x + c->w)
        x2 = c->x + c->w;
    if (y2 > c->y + c->h)
        y2 = c->y + c->h;
    if (x2 <= x1 || y2 <= y1)
        return 0;
    *x = x1;
    *y = y1;
    *w = x2 - x1;
    *h = y2 - y1;
    return 1;
}

static void ns_fill(ChCanvas* canv, int x, int y, int w, int h, uint32_t color, const NsClip* clip) {
    if (!canv || !clip || w <= 0 || h <= 0 || ((color >> 24) == 0))
        return;
    if (!ns_clip_rect(&x, &y, &w, &h, clip))
        return;
    ChDrawRect(canv, x, y, w, h, color);
}

static void ns_border(NsLayoutBox* b, ChCanvas* canv, int ox, int oy, const NsClip* clip, int scroll) {
    int x = ox + b->x;
    int y = oy + b->y - scroll;
    int bt = b->sty.border_width[0];
    int br = b->sty.border_width[1];
    int bb = b->sty.border_width[2];
    int bl = b->sty.border_width[3];
    if (bt > 0)
        ns_fill(canv, x, y, b->w, bt, b->sty.border_color[0], clip);
    if (bb > 0)
        ns_fill(canv, x, y + b->h - bb, b->w, bb, b->sty.border_color[2], clip);
    if (bl > 0)
        ns_fill(canv, x, y + bt, bl, b->h - bt - bb, b->sty.border_color[3], clip);
    if (br > 0)
        ns_fill(canv, x + b->w - br, y + bt, br, b->h - bt - bb, b->sty.border_color[1], clip);
}

static void ns_draw_text(ChCanvas* canv, int x, int baseline, const char* text, size_t len,
                         int font_px, uint32_t color, uint8_t decor, const NsClip* clip,
                         ChRect* crect) {
    ChFont* f = ns_font_for_px(font_px);
    ChRect ink;
    size_t i;
    if (!f || !text || !clip || !crect)
        return;
    ink = *crect;
    /* The baked glyph hangs above the baseline. A box only about one
     * line tall clips that ink, so the caps look cut off. Four pixels
     * covers the overshoot. A full em would paint the previous line.
     * A page run's clip is the viewport, which already holds the ink,
     * so the top stays put except on the first pixel rows. */
    {
        int top = baseline - ns_font_ascent(font_px) - 1;
        int bot = baseline + ns_font_descent(font_px) + 2;
        if (top < ink.y - 4)
            top = ink.y - 4;
        if (top < 0)
            top = 0;
        if (bot > ink.y + ink.h + 4)
            bot = ink.y + ink.h + 4;
        if (top < ink.y) {
            ink.h += ink.y - top;
            ink.y = top;
        }
        if (bot > ink.y + ink.h)
            ink.h = bot - ink.y;
    }
    for (i = 0; i < len; i++) {
        char c = text[i];
        int cw = ns_char_width(font_px, c);
        int cx = x;
        x += cw;
        if (c == '\n')
            break;
        if (cx + cw < clip->x || cx >= clip->x + clip->w)
            continue;
        if (baseline < clip->y - 32 || baseline > clip->y + clip->h + 10)
            continue;
        if (decor & NS_CSS_ITALIC)
            ChFontDrawCharClipped(canv, f, c, cx + 1, baseline, color, &ink);
        ChFontDrawCharClipped(canv, f, c, cx, baseline, color, &ink);
        if (decor & NS_CSS_BOLD)
            ChFontDrawCharClipped(canv, f, c, cx + 1, baseline, color, &ink);
    }
    if (decor & NS_CSS_UNDER)
        ns_fill(canv, x - ns_text_width(font_px, text, len), baseline + 2,
                ns_text_width(font_px, text, len), 1, color, clip);
    if (decor & NS_CSS_STRIKE)
        ns_fill(canv, x - ns_text_width(font_px, text, len), baseline - 5,
                ns_text_width(font_px, text, len), 1, color, clip);
    if (decor & NS_CSS_OVER)
        ns_fill(canv, x - ns_text_width(font_px, text, len), baseline - 12,
                ns_text_width(font_px, text, len), 1, color, clip);
}

static uint32_t ns_blend_rgb(uint32_t d, uint32_t p, unsigned a) {
    unsigned r = (((p >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255;
    unsigned g = (((p >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255;
    unsigned b = ((p & 255) * a + (d & 255) * (255 - a)) / 255;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

/* Alpha-aware scaling.  Nearest-neighbour sampling made the landing
 * dashboard and the product art look blocky whenever the CSS display
 * size differed from the decoded size.  Minification by more than 2x
 * uses a box average; everything else samples bilinearly. */
static void ns_blit_image(ChCanvas* canv, int dx, int dy, int dw, int dh,
                          const uint32_t* src, int sw, int sh, const NsClip* clip) {
    int x, y;
    int minify;
    int stride;
    int full_dx, full_dy, full_dw, full_dh;
    if (!canv || !canv->buffer || !src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;
    /* Cropping the source rectangle must not change the row stride.
     * Using the cropped width as the stride shears a wide image into
     * blocks as soon as the viewport cuts it. */
    stride = sw;
    full_dx = dx;
    full_dy = dy;
    full_dw = dw;
    full_dh = dh;
    if (!ns_clip_rect(&dx, &dy, &dw, &dh, clip))
        return;
    if (dx != full_dx || dy != full_dy || dw != full_dw || dh != full_dh) {
        int64_t sx = ((int64_t)(dx - full_dx) * sw) / full_dw;
        int64_t sy = ((int64_t)(dy - full_dy) * sh) / full_dh;
        int64_t sw2 = ((int64_t)dw * sw) / full_dw;
        int64_t sh2 = ((int64_t)dh * sh) / full_dh;
        if (sx < 0)
            sx = 0;
        if (sy < 0)
            sy = 0;
        if (sx >= sw)
            sx = sw - 1;
        if (sy >= sh)
            sy = sh - 1;
        if (sw2 < 1)
            sw2 = 1;
        if (sh2 < 1)
            sh2 = 1;
        if (sx + sw2 > sw)
            sw2 = sw - sx;
        if (sy + sh2 > sh)
            sh2 = sh - sy;
        src += (uint64_t)sy * (uint64_t)stride + (uint64_t)sx;
        sw = (int)sw2;
        sh = (int)sh2;
    }

    if (sw == dw && sh == dh) {
        for (y = 0; y < dh; y++) {
            uint32_t* dstp = canv->buffer + (uint64_t)(dy + y) * canv->canvasWidth + dx;
            const uint32_t* srcp = src + (uint64_t)y * (uint64_t)stride;
            for (x = 0; x < dw; x++) {
                uint32_t p = srcp[x];
                unsigned a = p >> 24;
                if (!a)
                    continue;
                dstp[x] = (a == 255) ? p : ns_blend_rgb(dstp[x], p, a);
            }
        }
        return;
    }

    minify = (sw >= dw * 2) || (sh >= dh * 2);
    for (y = 0; y < dh; y++) {
        uint32_t* dstp = canv->buffer + (uint64_t)(dy + y) * canv->canvasWidth + dx;
        int sy0, sy1;
        unsigned wy = 0;
        if (minify) {
            sy0 = (int)(((int64_t)y * sh) / dh);
            sy1 = (int)(((int64_t)(y + 1) * sh) / dh);
            if (sy1 <= sy0)
                sy1 = sy0 + 1;
            if (sy1 > sh)
                sy1 = sh;
        } else {
            int64_t fy = ((int64_t)(2 * y + 1) * sh * 65536) / (2 * dh) - 32768;
            sy0 = (int)(fy >> 16);
            wy = (unsigned)(fy & 0xffff);
            if (sy0 < 0) {
                sy0 = 0;
                wy = 0;
            }
            if (sy0 > sh - 1) {
                sy0 = sh - 1;
                wy = 0;
            }
            sy1 = (sy0 + 1 < sh) ? sy0 + 1 : sy0;
            if (sh == 1) {
                sy1 = 0;
                wy = 0;
            }
        }
        for (x = 0; x < dw; x++) {
            uint64_t nr = 0, ng = 0, nb = 0;
            uint32_t sa = 0;
            unsigned a;
            if (minify) {
                int sx0 = (int)(((int64_t)x * sw) / dw);
                int sx1 = (int)(((int64_t)(x + 1) * sw) / dw);
                int yy, xx;
                unsigned n = 0;
                if (sx1 <= sx0)
                    sx1 = sx0 + 1;
                if (sx1 > sw)
                    sx1 = sw;
                for (yy = sy0; yy < sy1; yy++) {
                    const uint32_t* row = src + (uint64_t)yy * (uint64_t)stride;
                    for (xx = sx0; xx < sx1; xx++) {
                        uint32_t p = row[xx];
                        unsigned pa = p >> 24;
                        if (!pa)
                            continue;
                        nr += (uint64_t)((p >> 16) & 255) * pa;
                        ng += (uint64_t)((p >> 8) & 255) * pa;
                        nb += (uint64_t)(p & 255) * pa;
                        sa += pa;
                        n++;
                    }
                }
                if (!sa || !n)
                    continue;
                {
                    uint32_t p = ((sa / n) << 24) | ((unsigned)(nr / sa) << 16) |
                                 ((unsigned)(ng / sa) << 8) | (unsigned)(nb / sa);
                    a = sa / n;
                    dstp[x] = (a == 255) ? p : ns_blend_rgb(dstp[x], p, a);
                }
                continue;
            }
            {
                int64_t fx = ((int64_t)(2 * x + 1) * sw * 65536) / (2 * dw) - 32768;
                int sx0 = (int)(fx >> 16);
                unsigned wx = (unsigned)(fx & 0xffff);
                int sx1;
                unsigned w00, w01, w10, w11;
                uint32_t p00, p01, p10, p11;
                uint64_t den;
                if (sx0 < 0) {
                    sx0 = 0;
                    wx = 0;
                }
                if (sx0 > sw - 1) {
                    sx0 = sw - 1;
                    wx = 0;
                }
                sx1 = (sx0 + 1 < sw) ? sx0 + 1 : sx0;
                if (sw == 1) {
                    sx1 = 0;
                    wx = 0;
                }
                w00 = (unsigned)(((uint64_t)(65536u - wx) * (65536u - wy)) >> 16);
                w01 = (unsigned)(((uint64_t)wx * (65536u - wy)) >> 16);
                w10 = (unsigned)(((uint64_t)(65536u - wx) * wy) >> 16);
                w11 = (unsigned)(((uint64_t)wx * wy) >> 16);
                p00 = src[(uint64_t)sy0 * (uint64_t)stride + sx0];
                p01 = src[(uint64_t)sy0 * (uint64_t)stride + sx1];
                p10 = src[(uint64_t)sy1 * (uint64_t)stride + sx0];
                p11 = src[(uint64_t)sy1 * (uint64_t)stride + sx1];
                {
                    unsigned a00 = p00 >> 24, a01 = p01 >> 24;
                    unsigned a10 = p10 >> 24, a11 = p11 >> 24;
                    nr = (uint64_t)((p00 >> 16) & 255) * w00 * a00 +
                         (uint64_t)((p01 >> 16) & 255) * w01 * a01 +
                         (uint64_t)((p10 >> 16) & 255) * w10 * a10 +
                         (uint64_t)((p11 >> 16) & 255) * w11 * a11;
                    ng = (uint64_t)((p00 >> 8) & 255) * w00 * a00 +
                         (uint64_t)((p01 >> 8) & 255) * w01 * a01 +
                         (uint64_t)((p10 >> 8) & 255) * w10 * a10 +
                         (uint64_t)((p11 >> 8) & 255) * w11 * a11;
                    nb = (uint64_t)(p00 & 255) * w00 * a00 +
                         (uint64_t)(p01 & 255) * w01 * a01 +
                         (uint64_t)(p10 & 255) * w10 * a10 +
                         (uint64_t)(p11 & 255) * w11 * a11;
                    den = (uint64_t)a00 * w00 + (uint64_t)a01 * w01 +
                          (uint64_t)a10 * w10 + (uint64_t)a11 * w11;
                }
                if (!den)
                    continue;
                a = (unsigned)(den >> 16);
                p00 = (unsigned)(nr / den) << 16 | (unsigned)(ng / den) << 8 |
                      (unsigned)(nb / den);
                if (a == 255)
                    dstp[x] = 0xff000000u | p00;
                else
                    dstp[x] = ns_blend_rgb(dstp[x], 0xff000000u | p00, a);
            }
        }
    }
}

static void ns_paint_box(NsLayoutBox* b, ChCanvas* canv, int ox, int oy, int scroll, const NsClip* clip) {
    int x, y;
    if (!b)
        return;
    if (b->kind == NS_BOX_BLOCK) {
        ox += b->offset_x;
        oy += b->offset_y;
        x = ox + b->x;
        y = oy + b->y - scroll;
        if (y + b->h < clip->y || y > clip->y + clip->h)
            return;
        if (b->bg)
            ns_fill(canv, x, y, b->w, b->h, b->bg, clip);
        ns_border(b, canv, ox, oy, clip, scroll);
        {
            NsLayoutBox* c = b->first_child;
            for (; c; c = c->next)
                ns_paint_box(c, canv, ox, oy, scroll, clip);
        }
        /* The search field is the block itself, so the caret cannot wait
         * for an inner control box.  CSS draws the border; this only draws
         * the typed text. */
        if (b->node && b->node == s_edit_node && s_edit_active) {
            int bl = b->sty.border_width[3] > 0 ? b->sty.border_width[3] : 0;
            int bt = b->sty.border_width[0] > 0 ? b->sty.border_width[0] : 0;
            int pl = b->sty.padding[3] > 0 ? b->sty.padding[3] : 0;
            int pt = b->sty.padding[0] > 0 ? b->sty.padding[0] : 0;
            int font_px = b->font_px > 0 ? b->font_px : 14;
            int tx = x + bl + pl + 2;
            int top = y + bt + pt;
            int ch = b->ch > 0 ? b->ch : font_px;
            int ly = ns_baseline_y(top, ch, font_px);
            int fx = x + bl + pl;
            int fy = top;
            int fw = b->cw > 0 ? b->cw : b->w;
            int fh = ch;
            NsClip fc = *clip;
            if (ns_clip_rect(&fx, &fy, &fw, &fh, &fc)) {
                const char* t = s_edit_text;
                uint32_t color = b->color ? b->color : 0xff202124u;
                ChRect cr;
                int cw;
                int caret;
                fc.x = fx;
                fc.y = fy;
                fc.w = fw;
                fc.h = fh;
                cr.x = fc.x;
                cr.y = fc.y;
                cr.w = fc.w;
                cr.h = fc.h;
                if (t[0])
                    ns_draw_text(canv, tx, ly, t, strlen(t), font_px, color, 0, &fc, &cr);
                cw = ns_text_width(font_px, t, (size_t)s_edit_cursor);
                caret = tx + cw;
                if (caret > fx + fw - 3)
                    caret = fx + fw - 3;
                if (caret < fx + 2)
                    caret = fx + 2;
                ns_fill(canv, caret, fy + 2, 2, fh > 6 ? fh - 4 : 1, color, &fc);
            }
        }
        return;
    }
    x = ox + b->x + b->offset_x;
    y = oy + b->y - scroll + b->offset_y;
    if (b->kind == NS_BOX_LINE) {
        NsLayoutBox* r = b->first_child;
        for (; r; r = r->next)
            ns_paint_box(r, canv, ox, oy, scroll, clip);
        return;
    }
    if (b->kind == NS_BOX_RUN) {
        ChRect cr;
        cr.x = clip->x;
        cr.y = clip->y;
        cr.w = clip->w;
        cr.h = clip->h;
        if (b->text && b->font_px > 0)
            ns_draw_text(canv, x, oy + b->baseline - scroll, b->text, b->text_len, b->font_px,
                         b->color, b->decor, clip, &cr);
        return;
    }
    if (b->kind == NS_BOX_IMAGE) {
        int bx = x + b->margin[3];
        int by = y + b->margin[0];
        int bw = b->img_w > 0 ? b->img_w : b->w - b->margin[1] - b->margin[3];
        int bh = b->img_h > 0 ? b->img_h : b->h - b->margin[0] - b->margin[2];
        NsClip bc = *clip;
        if (b->pixels) {
            int dw = bw;
            int dh = bh;
            int ox = 0;
            int oy = 0;
            /* Fit the bitmap inside the box. A box whose ratio is not the
             * bitmap's used to scale the product screenshots, and the type
             * inside them lost the top of every glyph. A one-pixel rounding
             * gap is left as a fill so a matching box is not letterboxed. */
            if (b->src_w > 1 && b->src_h > 1 && bw > 1 && bh > 1) {
                int64_t fw = bw;
                int64_t fh = ((int64_t)bw * b->src_h + b->src_w / 2) / b->src_w;
                if (fh > bh) {
                    fh = bh;
                    fw = ((int64_t)bh * b->src_w + b->src_h / 2) / b->src_h;
                }
                if (fw < 1)
                    fw = 1;
                if (fh < 1)
                    fh = 1;
                if (fw <= bw && fh <= bh && (bw - (int)fw > 1 || bh - (int)fh > 1)) {
                    ox = (int)((bw - fw) / 2);
                    oy = (int)((bh - fh) / 2);
                    dw = (int)fw;
                    dh = (int)fh;
                }
            }
            /* Pass the unclipped image rectangle: ns_blit_image crops the
             * source to the visible part.  Passing a pre-clipped
             * destination while keeping the whole source made images
             * squash and stretch while scrolling. */
            ns_blit_image(canv, bx + ox, by + oy, dw, dh, b->pixels, b->src_w, b->src_h, &bc);
        } else {
            ChRect cr;
            ns_fill(canv, bx, by, bw, bh, b->bg ? b->bg : 0xfff3f3f3u, &bc);
            ns_fill(canv, bx, by, bw, 1, b->border_color ? b->border_color : 0xffb0b0b0u, &bc);
            ns_fill(canv, bx, by + bh - 1, bw, 1,
                    b->border_color ? b->border_color : 0xffb0b0b0u, &bc);
            ns_fill(canv, bx, by, 1, bh, b->border_color ? b->border_color : 0xffb0b0b0u, &bc);
            ns_fill(canv, bx + bw - 1, by, 1, bh,
                    b->border_color ? b->border_color : 0xffb0b0b0u, &bc);
            if (b->label && b->label[0]) {
                int lw = ns_text_width(b->font_px, b->label, strlen(b->label));
                int lx = bx + (bw - lw) / 2;
                int ly = ns_baseline_y(by, bh, b->font_px);
                cr.x = bc.x;
                cr.y = bc.y;
                cr.w = bc.w;
                cr.h = bc.h;
                ns_draw_text(canv, lx, ly, b->label, strlen(b->label), b->font_px,
                             b->color, 0, &bc, &cr);
            }
        }
        return;
    }
    if (b->kind == NS_BOX_CONTROL || b->kind == NS_BOX_INLINE_BLOCK) {
        int bx = x + b->margin[3];
        int by = y + b->margin[0];
        int bw = b->w - b->margin[1] - b->margin[3];
        int bh = b->h - b->margin[0] - b->margin[2];
        NsClip bc = *clip;
        int clipped = ns_clip_rect(&bx, &by, &bw, &bh, &bc);
        if (!clipped)
            return;
        bc.x = bx;
        bc.y = by;
        bc.w = bw;
        bc.h = bh;
        if (b->bg)
            ns_fill(canv, bx, by, bw, bh, b->bg, &bc);
        {
            int editing = (b->node && b->node == s_edit_node && s_edit_active);
            if (b->kind == NS_BOX_CONTROL || b->framed) {
                uint32_t border = editing
                                      ? 0xff4285f4u
                                      : (b->border_color ? b->border_color : 0xff8a8a8au);
                ns_fill(canv, bx, by, bw, 1, border, &bc);
                ns_fill(canv, bx, by + bh - 1, bw, 1, border, &bc);
                ns_fill(canv, bx, by, 1, bh, border, &bc);
                ns_fill(canv, bx + bw - 1, by, 1, bh, border, &bc);
            }
            if (editing) {
                const char* t = s_edit_text;
                int tx = bx + 6;
                int ly = ns_baseline_y(by, bh, b->font_px);
                ChRect cr;
                int cw;
                int cx;
                cr.x = bc.x;
                cr.y = bc.y;
                cr.w = bc.w;
                cr.h = bc.h;
                if (t[0])
                    ns_draw_text(canv, tx, ly, t, strlen(t), b->font_px,
                                 0xff202124u, 0, &bc, &cr);
                cw = ns_text_width(b->font_px, t, (size_t)s_edit_cursor);
                cx = tx + cw;
                if (cx > bx + bw - 3)
                    cx = bx + bw - 3;
                if (cx < bx + 2)
                    cx = bx + 2;
                ns_fill(canv, cx, by + 4, 2, bh > 8 ? bh - 8 : 1, 0xff202124u, &bc);
            } else if (b->label && b->label[0]) {
                int lw = ns_text_width(b->font_px, b->label, strlen(b->label));
                int lx;
                int ly;
                ChRect cr;
                int captcha = b->node && !b->node->is_text && b->node->id[0] &&
                              !strcmp(b->node->id, "ns-captcha");
                if (captcha) {
                    int box = 24;
                    int sqx = bx + 16;
                    int sqy = by + (bh - box) / 2;
                    uint32_t edge = 0xffc1c1c1u;
                    ns_fill(canv, sqx, sqy, box, 1, edge, &bc);
                    ns_fill(canv, sqx, sqy + box - 1, box, 1, edge, &bc);
                    ns_fill(canv, sqx, sqy, 1, box, edge, &bc);
                    ns_fill(canv, sqx + box - 1, sqy, 1, box, edge, &bc);
                    lx = sqx + box + 12;
                } else if (b->kind == NS_BOX_INLINE_BLOCK && b->offset_x > 0) {
                    lx = bx + b->offset_x;
                } else {
                    lx = bx + (bw - lw) / 2;
                }
                ly = ns_baseline_y(by, bh, b->font_px);
                cr.x = bc.x;
                cr.y = bc.y;
                cr.w = bc.w;
                cr.h = bc.h;
                ns_draw_text(canv, lx, ly, b->label, strlen(b->label), b->font_px,
                             b->color, b->decor, &bc, &cr);
            }
        }
        return;
    }
}

/* ---- public API ---- */

int NsLayoutBuild(NsCssNode* root, int viewport_w, int viewport_h) {
    NsCssNode* body = NULL;
    int i;
    NsLayoutDestroy();
    if (!root)
        return -1;
    s_root_node = root;
    s_blocks = s_lines = s_runs = s_controls = 0;
    s_viewport_w = viewport_w > 0 ? viewport_w : 640;
    s_viewport_h = viewport_h > 0 ? viewport_h : 420;
    s_page_bg = 0xffffffffu;
    {
        uint32_t bg = NsCssNodeBackground(root);
        if (bg)
            s_page_bg = bg;
        for (i = 0; i < 2; i++) {
            NsCssNode* c;
            for (c = root->first_child; c; c = c->next) {
                if (!c->is_text && !strcmp(c->tag, "body")) {
                    body = c;
                    break;
                }
            }
            if (!body)
                break;
            bg = NsCssNodeBackground(body);
            if (bg)
                s_page_bg = bg;
            break;
        }
    }
    s_root_box = ns_layout_block(root, 0, 0, s_viewport_w, 0);
    if (!s_root_box)
        return -1;
    s_content_h = s_root_box->h;
    s_page_h = s_content_h;
    if (s_page_h < s_viewport_h)
        s_page_h = s_viewport_h;
    s_active = 1;
    _KePrint("[layout] page %dx%d content=%d blocks=%d lines=%d runs=%d controls=%d\n",
             s_viewport_w, s_page_h, s_content_h, s_blocks, s_lines, s_runs, s_controls);
    return 0;
}

void NsLayoutDestroy(void) {
    ns_box_free(s_root_box);
    s_root_box = NULL;
    while (s_abs_head) {
        NsLayoutBox* n = s_abs_head->abs_next;
        ns_box_free(s_abs_head);
        s_abs_head = n;
    }
    s_abs_tail = NULL;
    s_cur_containing = NULL;
    s_root_node = NULL;
    while (s_pool) {
        NsTextPool* n = s_pool->next;
        free(s_pool);
        s_pool = n;
    }
    s_nlinks = 0;
    s_nformctrls = 0;
    s_edit_node = NULL;
    s_edit_active = 0;
    s_page_h = 0;
    s_content_h = 0;
    s_blocks = s_lines = s_runs = s_controls = 0;
    s_active = 0;
    s_plain[0] = 0;
}

int NsLayoutActive(void) {
    return s_active;
}

int NsLayoutPageHeight(void) {
    return s_page_h;
}

int NsLayoutPageWidth(void) {
    return s_viewport_w;
}

int NsLayoutContentHeight(void) {
    return s_content_h;
}

void NsLayoutStats(int* blocks, int* lines, int* runs, int* links) {
    if (blocks)
        *blocks = s_blocks;
    if (lines)
        *lines = s_lines;
    if (runs)
        *runs = s_runs + s_controls;
    if (links)
        *links = s_nlinks;
}

uint32_t NsLayoutPageBg(void) {
    return s_page_bg;
}

void NsLayoutPaint(ChCanvas* canv, int vx, int vy, int vw, int vh, int scroll_y) {
    NsClip clip;
    if (!canv || vw <= 0 || vh <= 0)
        return;
    clip.x = vx;
    clip.y = vy;
    clip.w = vw;
    clip.h = vh;
    ChDrawRect(canv, vx, vy, vw, vh, s_page_bg);
    if (!s_active || !s_root_box)
        return;
    if (scroll_y > s_page_h - vh)
        scroll_y = s_page_h - vh;
    if (scroll_y < 0)
        scroll_y = 0;
    ns_paint_box(s_root_box, canv, vx, vy, scroll_y, &clip);
    {
        NsLayoutBox* ab;
        for (ab = s_abs_head; ab; ab = ab->abs_next) {
            int ascroll = ab->position == NS_POS_FIXED ? 0 : scroll_y;
            ns_paint_box(ab, canv, vx, vy, ascroll, &clip);
        }
    }
}

int NsLayoutHitLink(int x, int y, char* href, size_t hrefsz) {
    int i;
    int best = -1;
    int64_t best_area = 0;
    if (!href || hrefsz == 0)
        return 0;
    href[0] = 0;
    for (i = 0; i < s_nlinks; i++) {
        int w = s_links[i].w;
        int h = s_links[i].h;
        int64_t area;
        if (w <= 0 || h <= 0)
            continue;
        if (x < s_links[i].x || x >= s_links[i].x + w || y < s_links[i].y ||
            y >= s_links[i].y + h)
            continue;
        area = (int64_t)w * (int64_t)h;
        if (best < 0 || area < best_area) {
            best = i;
            best_area = area;
        }
    }
    if (best < 0)
        return 0;
    strncpy(href, s_links[best].href, hrefsz - 1);
    href[hrefsz - 1] = 0;
    return 1;
}

int NsLayoutHitControl(int x, int y, NsCssNode** node) {
    int i;
    if (node)
        *node = NULL;
    for (i = s_nformctrls - 1; i >= 0; i--) {
        if (x >= s_formctrls[i].x && x < s_formctrls[i].x + s_formctrls[i].w &&
            y >= s_formctrls[i].y && y < s_formctrls[i].y + s_formctrls[i].h) {
            if (node)
                *node = s_formctrls[i].node;
            return 1;
        }
    }
    return 0;
}

void NsLayoutSetEdit(NsCssNode* node, const char* text, int cursor, int active) {
    s_edit_node = node;
    s_edit_active = active && node != NULL;
    if (!s_edit_active) {
        s_edit_cursor = 0;
        return;
    }
    if (!text)
        text = "";
    strncpy(s_edit_text, text, sizeof(s_edit_text) - 1);
    s_edit_text[sizeof(s_edit_text) - 1] = 0;
    s_edit_cursor = cursor;
    if (s_edit_cursor < 0)
        s_edit_cursor = 0;
    if (s_edit_cursor > (int)strlen(s_edit_text))
        s_edit_cursor = (int)strlen(s_edit_text);
}

static void ns_text_walk(NsLayoutBox* b, size_t* o, int* last_nl) {
    if (!b || *o + 2 >= NS_PLAIN_MAX)
        return;
    if (b->kind == NS_BOX_BLOCK) {
        NsLayoutBox* c;
        for (c = b->first_child; c; c = c->next)
            ns_text_walk(c, o, last_nl);
        if (!*last_nl && *o + 1 < NS_PLAIN_MAX) {
            s_plain[(*o)++] = '\n';
            *last_nl = 1;
        }
        return;
    }
    if (b->kind == NS_BOX_LINE) {
        NsLayoutBox* r;
        for (r = b->first_child; r; r = r->next)
            ns_text_walk(r, o, last_nl);
        if (!*last_nl && *o + 1 < NS_PLAIN_MAX) {
            s_plain[(*o)++] = '\n';
            *last_nl = 1;
        }
        return;
    }
    if (b->kind == NS_BOX_RUN) {
        size_t i;
        for (i = 0; i < b->text_len && *o + 1 < NS_PLAIN_MAX; i++)
            s_plain[(*o)++] = b->text[i];
        *last_nl = 0;
        return;
    }
    if ((b->kind == NS_BOX_CONTROL || b->kind == NS_BOX_IMAGE) && b->label) {
        size_t i;
        for (i = 0; b->label[i] && *o + 1 < NS_PLAIN_MAX; i++)
            s_plain[(*o)++] = b->label[i];
        *last_nl = 0;
    }
}

static void ns_dump_box(NsLayoutBox* b, int depth) {
    NsLayoutBox* c;
    if (!b)
        return;
    if (b->kind == NS_BOX_BLOCK) {
        const char* tag = (b->node && !b->node->is_text) ? b->node->tag : "?";
        _KePrint("[box] d=%d y=%d h=%d tag=%s\n", depth, b->y, b->h, tag);
        for (c = b->first_child; c; c = c->next)
            ns_dump_box(c, depth + 1);
    } else if (b->kind == NS_BOX_LINE) {
        NsLayoutBox* r = b->first_child;
        char head[48];
        size_t n = 0;
        while (r && n + 1 < sizeof(head)) {
            if (r->kind == NS_BOX_RUN && r->text) {
                size_t i;
                for (i = 0; i < r->text_len && n + 1 < sizeof(head); i++)
                    head[n++] = r->text[i];
            }
            r = r->next;
        }
        head[n] = 0;
        _KePrint("[line] d=%d y=%d h=%d '%s'\n", depth, b->y, b->h, head);
    }
}

void NsLayoutDumpBoxes(void) {
    ns_dump_box(s_root_box, 0);
}

void NsLayoutPrintLinks(int max) {
    int i;
    if (max > s_nlinks)
        max = s_nlinks;
    for (i = 0; i < max; i++) {
        char hit[96];
        int ok = NsLayoutHitLink(s_links[i].x + s_links[i].w / 2,
                                 s_links[i].y + s_links[i].h / 2, hit, sizeof(hit));
        _KePrint("[link] %d %d %d %d hit=%d %s\n", s_links[i].x, s_links[i].y,
                 s_links[i].w, s_links[i].h, ok, s_links[i].href);
    }
}

int NsLayoutPaintTest(int w, int h, int scroll) {
    ChCanvas cv;
    uint32_t* px;
    uint32_t bgpx;
    size_t i;
    int nonbg = 0;
    if (!s_active || w <= 0 || h <= 0)
        return -1;
    px = (uint32_t*)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!px)
        return -1;
    memset(px, 0, (size_t)w * (size_t)h * sizeof(uint32_t));
    memset(&cv, 0, sizeof(cv));
    cv.canvasWidth = (uint32_t)w;
    cv.canvasHeight = (uint32_t)h;
    cv.bpp = 32;
    cv.pitch = (uint32_t)w * 4;
    cv.buffer = px;
    cv.bufferSz = (size_t)w * (size_t)h * sizeof(uint32_t);
    NsLayoutPaint(&cv, 0, 0, w, h, scroll);
    _KePrint("[paintdone] %d %d\n", w, h);
    bgpx = px[0];
    for (i = 0; i < (size_t)w * (size_t)h; i++) {
        if (px[i] != bgpx)
            nonbg++;
    }
    free(px);
    return nonbg;
}

static int ns_paint_soft(ChCanvas* cv, uint32_t** px_out, int w, int h, int scroll) {
    uint32_t* px = (uint32_t*)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!px)
        return 0;
    memset(px, 0, (size_t)w * (size_t)h * sizeof(uint32_t));
    memset(cv, 0, sizeof(*cv));
    cv->canvasWidth = (uint32_t)w;
    cv->canvasHeight = (uint32_t)h;
    cv->bpp = 32;
    cv->pitch = (uint32_t)w * 4;
    cv->buffer = px;
    cv->bufferSz = (size_t)w * (size_t)h * sizeof(uint32_t);
    NsLayoutPaint(cv, 0, 0, w, h, scroll);
    *px_out = px;
    return 1;
}

int NsLayoutDumpPpm(const char* path, int w, int h, int scroll) {
    ChCanvas cv;
    uint32_t* px = NULL;
    unsigned char* out;
    char hdr[64];
    size_t hlen;
    size_t total;
    size_t o;
    int fd;
    int y;
    if (!s_active || !path || w <= 0 || h <= 0)
        return -1;
    if (!ns_paint_soft(&cv, &px, w, h, scroll))
        return -1;
    hlen = (size_t)snprintf(hdr, sizeof(hdr), "P6\n%d %d\n255\n", w, h);
    total = hlen + (size_t)w * (size_t)h * 3;
    out = (unsigned char*)malloc(total);
    if (!out) {
        free(px);
        return -1;
    }
    memcpy(out, hdr, hlen);
    o = hlen;
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            uint32_t p = px[(size_t)y * w + x];
            out[o++] = (unsigned char)((p >> 16) & 0xff);
            out[o++] = (unsigned char)((p >> 8) & 0xff);
            out[o++] = (unsigned char)(p & 0xff);
        }
    }
    fd = _KeOpenFile((char*)path, FILE_OPEN_WRITE | FILE_OPEN_CREAT);
    if (fd == -1) {
        free(out);
        free(px);
        return -2;
    }
    _KeWriteFile(fd, out, total);
    _KeCloseFile(fd);
    free(out);
    free(px);
    return 0;
}

const char* NsLayoutText(void) {
    size_t o = 0;
    int last_nl = 1;
    if (!s_active || !s_root_box)
        return "";
    s_plain[0] = 0;
    ns_text_walk(s_root_box, &o, &last_nl);
    s_plain[o] = 0;
    return s_plain;
}
