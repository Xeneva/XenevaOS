/**
 * NetSurf CSS wrapper for XenevaOS (Phase 1+2).
 *
 * Thin element tree + libcss select client. The HTML pass builds NsCssNodes
 * as it scans tags; each node is selected against the UA + author sheets on
 * open (ancestors are always selected first, so inheritance composes).
 * Inline style="" attributes parse to per-node author sheets.
 *
 * Exposes only what the readability renderer needs: text color
 * (0xAARRGGBB, libcss css_color layout matches Chitralekha pixels),
 * font size in px, and extra blank lines from vertical margins.
 */

#ifndef NS_CSS_H
#define NS_CSS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NS_CSS_MAX_CLASSES 8
#define NS_CSS_CLASS_LEN 31
#define NS_CSS_ID_LEN 63
#define NS_CSS_TAG_LEN 15
#define NS_CSS_MAX_ATTRS 24
#define NS_CSS_ATTR_NAME_LEN 31

typedef struct NsCssAttr {
    char* name;
    char* value;
} NsCssAttr;

typedef struct NsCssNode {
    struct NsCssNode* parent;
    /* Sibling links + child list, so libcss sibling combinators and
     * :empty can see the real document shape. */
    struct NsCssNode* next;
    struct NsCssNode* prev;
    struct NsCssNode* first_child;
    struct NsCssNode* last_child;
    char tag[NS_CSS_TAG_LEN + 1];
    char cls[NS_CSS_MAX_CLASSES][NS_CSS_CLASS_LEN + 1];
    uint32_t n_cls;
    char id[NS_CSS_ID_LEN + 1];
    bool is_link;
    bool is_text;
    char* text;
    size_t text_len;
    NsCssAttr attrs[NS_CSS_MAX_ATTRS];
    uint32_t n_attrs;
    void* lwcName;
    void* lwcCls[NS_CSS_MAX_CLASSES];
    void* lwcId;
    void* inlineSheet;
    void* selectResults;
} NsCssNode;

/* Per-page lifecycle: begin, add author <style> sheets, end. A UA sheet
 * with reader defaults (body white, headings sized, links blue) is
 * installed by NsCssBegin. */
int NsCssBegin(void);
/* Set the media-query viewport before selecting the DOM. Must be called
 * after NsCssBegin() (which resets the media block). */
void NsCssSetViewport(int width, int height);
int NsCssAddSheet(const char* data, size_t len);
void NsCssEnd(void);

NsCssNode* NsCssNodeCreate(NsCssNode* parent, const char* tag);
NsCssNode* NsCssNodeCreateText(NsCssNode* parent, const char* text, size_t len);
void NsCssNodeAppendChild(NsCssNode* parent, NsCssNode* child);
void NsCssNodeAddAttribute(NsCssNode* n, const char* name, const char* value);
const char* NsCssNodeAttr(NsCssNode* n, const char* name);
void NsCssNodeAddClass(NsCssNode* n, const char* cls);
void NsCssNodeSetId(NsCssNode* n, const char* id);
void NsCssNodeSetLink(NsCssNode* n, bool link);
/* Parse a style="" attribute body into a per-node author sheet.
 * An empty string drops the inline sheet. */
int NsCssNodeSetInline(NsCssNode* n, const char* style);
/* Depth-first id lookup. */
NsCssNode* NsCssNodeById(NsCssNode* n, const char* id);
/* Select n and its descendants. Parent of n must already be selected
 * when n is not the root. The select context must still be alive. */
void NsCssReselectTree(NsCssNode* n);
/* Select + inherit-compose n. Parent must already be selected. */
int NsCssSelect(NsCssNode* n);
void NsCssNodeDestroy(NsCssNode* n);
/* Recursively destroy a parsed tree (children + siblings). */
void NsCssNodeDestroyTree(NsCssNode* n);

/* Display values translated for the layout engine. */
#define NS_DISPLAY_INLINE 0
#define NS_DISPLAY_BLOCK 1
#define NS_DISPLAY_LIST_ITEM 2
#define NS_DISPLAY_NONE 3
#define NS_DISPLAY_INLINE_BLOCK 4
#define NS_DISPLAY_TABLE_ROW 5
#define NS_DISPLAY_TABLE_CELL 6
#define NS_DISPLAY_FLEX_ROW 7

/* Position values used by the layout engine. */
#define NS_POS_STATIC 0
#define NS_POS_ABSOLUTE 1
#define NS_POS_FIXED 2
#define NS_POS_RELATIVE 3

/* Fully resolved box metrics. Lengths are px (may be negative for
 * margins); width/height are 0 when auto. Percentages are resolved
 * against base_width (horizontal) / base_height (vertical). */
typedef struct NsCssBoxStyle {
    uint8_t display;
    uint8_t text_align; /* 0 left, 1 center, 2 right */
    uint8_t white_space_pre;
    uint8_t white_space_nowrap;
    uint8_t list_type; /* 0 none, 1 disc, 2 circle, 3 square, 4 decimal */
    uint8_t decor;
    uint8_t width_auto;
    uint8_t height_auto;
    uint8_t position;
    uint8_t float_side;
    uint8_t box_sizing;
    /* Out-of-flow / relative offsets. */
    uint8_t top_auto, right_auto, bottom_auto, left_auto;
    uint8_t z_index_auto;
    int top, right, bottom, left;
    int z_index;
    /* Flex container and item properties. */
    uint8_t flex_container;
    uint8_t flex_direction; /* 0 row, 1 column */
    uint8_t flex_wrap;
    uint8_t justify_content;
    uint8_t align_items;
    /* 0 auto (use the container's align-items), 1 center, 2 flex-end,
     * 3 flex-start, 4 stretch. */
    uint8_t align_self;
    uint8_t flex_basis_auto;
    int flex_basis;
    int flex_grow;
    int flex_shrink;
    int width;
    int height;
    int min_width;
    int max_width;
    int min_height;
    int max_height;
    int margin[4];      /* top, right, bottom, left */
    int padding[4];
    int border_width[4];
    uint32_t border_color[4];
    uint8_t border_style[4]; /* 0 none/hidden, otherwise visible */
    uint32_t color;
    uint32_t background;
    int font_px;
    int line_px;
    int opacity; /* 0..255 */
} NsCssBoxStyle;

void NsCssNodeBoxStyle(NsCssNode* n, int base_width, int base_height,
                       int viewport_w, int viewport_h, NsCssBoxStyle* out);

uint32_t NsCssNodeColor(NsCssNode* n);
int NsCssNodeFontPx(NsCssNode* n);
int NsCssNodeMarginLines(NsCssNode* n);
/* True when the element must not contribute text: display:none or
 * visibility:hidden/collapse. */
int NsCssNodeSkip(NsCssNode* n);
/* 1 when text-align computes to center. */
int NsCssNodeCenter(NsCssNode* n);
/* 1 when this element starts a new line (block, table, flex, grid).
 * Inline, inline-block, and table-cell stay on the current line.
 * Children of a table-row or flex row also stay on that line. */
int NsCssNodeBlock(NsCssNode* n);
/* 0 when the background is transparent. Otherwise 0xAARRGGBB. */
uint32_t NsCssNodeBackground(NsCssNode* n);

/* Bits the painter applies. Everything else in the computed style either
 * has no face to switch to (font-family) or no box tree (position). */
#define NS_CSS_BOLD 1
#define NS_CSS_ITALIC 2
#define NS_CSS_UNDER 4
#define NS_CSS_STRIKE 8
#define NS_CSS_OVER 16

typedef struct NsCssUsed {
	uint8_t decor;
	uint8_t align; /* 0 left, 1 center, 2 right */
	uint8_t indent;
	uint8_t line; /* row step in px */
	uint8_t list; /* 0 none, 1 disc, 2 circle, 3 square, 4 decimal, 5 alpha */
	uint8_t upper; /* 1 uppercase, 2 lowercase */
	uint8_t pre;
	uint8_t nowrap;
	uint16_t width; /* 0 = auto */
	uint8_t opacity; /* 255 = solid */
	uint32_t border; /* 0, or the bottom-border color */
} NsCssUsed;

void NsCssNodeUsed(NsCssNode* n, NsCssUsed* out);

#ifdef __cplusplus
}
#endif

#endif
