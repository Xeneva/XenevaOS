/**
 * NetSurf bootstrap browser for XenevaOS.
 *
 * Staging frontend, not upstream NetSurf core yet: a Chitralekha window with
 * a URL bar + Go button, HTTP/1.1 fetch (TLS on https://), and crude
 * HTML-to-text rendering into a textbox.
 */

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/button.h>
#include <widgets/textbox.h>
#include <widgets/window.h>
#include <keycode.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/socket.h>
#include <sys/netdb.h>
#include <arpa/inet.h>
#include <sys/iocodes.h>
#include <ctype.h>

#include "css.h"
#include "dom.h"
#include "layout.h"
#include "js.h"

#include "pngdec.h"
#include "svgdec.h"
#include <nanojpg.h>
#include <new>
#include <pthread.h>
#include <https.h>

extern "C" void xe_heap_check(const char* tag);

#define NS_URL_MAX	2048
#define NS_BODY_MAX (256 * 1024)
/* Watch-page player JSON starts after ~780 KiB. The text view stays smaller. */
#define NS_RAW_MAX (1400 * 1024)
/* Visible page lines: 420px box at the painter's 19px line stride. */
#define NS_PAGE_LINES 22

/* Extended key codes (0xE0-prefixed AT Set-1, as the virtio-kbd driver
 * forwards them -- same encoding Doom's platform layer decodes). */
#define NS_KEY_LEFT	 0xE04B
#define NS_KEY_RIGHT 0xE04D
#define NS_KEY_UP	 0xE048
#define NS_KEY_DOWN	 0xE050
#define NS_KEY_HOME	 0xE047
#define NS_KEY_END	 0xE04F
#define NS_KEY_PGUP	 0xE049
#define NS_KEY_PGDN	 0xE051

static ChitralekhaApp* s_app = NULL;
static ChWindow* s_mainWin = NULL;
static ChTextBox* s_urlBox = NULL;
static ChTextBox* s_pageBox = NULL;

static char s_url[NS_URL_MAX] = "https://example.com/";
static size_t s_urlCursor = 0;
static char s_body[NS_RAW_MAX + 1];
static char s_text[NS_BODY_MAX + 1];
static char s_view[NS_BODY_MAX + 1];
static int s_pageTop = 0;
static size_t s_viewStart = 0;
static int s_layoutActive = 0;
static int s_layoutScroll = 0;
static NsCssNode* s_domRoot = NULL;
static char s_docUrl[NS_URL_MAX];
/* This document's anchor had no token. Another click must not fetch it again. */
static char s_capAnchorUrl[NS_URL_MAX];
static int s_capAnchorMiss;
static ChFont* s_fontBig = NULL;
/* Per-character style for s_text, recorded during the HTML pass. */
static uint32_t s_col[NS_BODY_MAX + 1];
static uint32_t s_bg[NS_BODY_MAX + 1];
static uint8_t s_sz[NS_BODY_MAX + 1];
static uint8_t s_fl[NS_BODY_MAX + 1];
/* Author CSS (inline <style> + fetched <link rel=stylesheet>) collected
 * before the render pass. */
#define NS_CSS_MAX (192 * 1024)
static char s_cssBuf[NS_CSS_MAX];
/* Current emission run, refreshed from the element stack top. */
static uint32_t g_runColor = 0xff202124u;
static uint32_t g_runBg = 0;
static uint8_t g_runSize = 14;
static uint8_t g_runDecor = 0;
static uint8_t g_runAlign = 0;
static uint8_t g_runIndent = 0;
static uint8_t g_runLine = 19;
static uint8_t g_runCase = 0;
static uint8_t g_runPre = 0;
static uint8_t g_runNowrap = 0;
static uint16_t g_runWidth = 0;
static uint32_t g_runBorder = 0;
static int g_linePx = 0;
static int s_liNo = 0;

static uint32_t NsMixOpacity(uint32_t fg, uint32_t bg, uint8_t a) {
	int i;
	uint32_t out = 0xff000000u;
	if (a == 255)
		return fg;
	for (i = 0; i < 3; i++) {
		int shift = i * 8;
		int f = (int)((fg >> shift) & 255);
		int b = (int)((bg >> shift) & 255);
		int m = (f * a + b * (255 - a)) / 255;
		out |= (uint32_t)m << shift;
	}
	return out;
}
static int g_runCenter = 0;
static uint32_t g_pageBg = 0xffffffffu;
static int g_pageCenter = 0;
static int s_googleHome = 0;
#define NS_IMG_MAX 32
static uint32_t* s_imgPx[NS_IMG_MAX];
static int s_imgW[NS_IMG_MAX];
static int s_imgH[NS_IMG_MAX];
/* CSS natural size of an SVG.  The bitmap may be supersampled above this
 * (s_imgW/s_imgH) so painting stays sharp, but layout must still treat the
 * artwork as its authored size. */
static int s_imgIW[NS_IMG_MAX];
static int s_imgIH[NS_IMG_MAX];
static char s_imgUrl[NS_IMG_MAX][512];
static int s_nimg = 0;

/* English stand-in for a link whose visible text is not Latin (hl=hi, …). */
static const char* s_latinLink = NULL;
static int s_latinUsed = 0;
/* Painted search field. Click focuses it; typing does not go to the URL bar. */
static char s_query[240];
static size_t s_qCursor = 0;
static int s_qFocus = 0;
static int s_qHave = 0;
static int s_qX, s_qY, s_qW, s_qH;
static int s_btnHave = 0;
static int s_btnX, s_btnY, s_btnW, s_btnH;
static int s_luckyHave = 0;
static int s_luckyX, s_luckyY, s_luckyW, s_luckyH;
/* Layout-mode form control with keyboard focus and its live value. */
static NsCssNode* s_formFocus = NULL;
static char s_formValue[512];
static size_t s_formCursor = 0;
/* Anchors in s_text. x/y/w/h are filled while painting the visible slice. */
#define NS_LINK_MAX 48
static struct {
	size_t a, b;
	char href[320];
	int x, y, w, h, have;
} s_links[NS_LINK_MAX];
static int s_nlinks = 0;
static int NsFetch(char* url);
static void NsResolveUrl(const char* base, const char* href, char* out, size_t outsz);
static void NsEncodeQuery(const char* in, char* out, size_t outsz);
static void NsFormClearFocus(void);
static void NsFormSubmit(NsCssNode* submitNode);
static void NsCaptchaOffer(NsCssNode* root);
static void NsCaptchaActivate(void);
static const char kNsUa[] =
	"Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36";

/* ---------------- JS host plumbing (Ports/quickjs via js.c) ----------------
 * js.c calls the sink for document.write and console lines. Mid-parse the
 * output lands at the live parse position under the current run style;
 * after the parse (timers) it appends to the page text end. */
static char* s_outDst = NULL;
static size_t s_outSz = 0;
static size_t* s_outDi = NULL;
static bool s_outLive = false;  /* NsHtmlToText emission target is live */
static bool s_postPage = false; /* parse finished: sink appends instead */
static bool g_inWrite = false;  /* parsing a document.write fragment */
static int g_writeDepth = 0;    /* fragment recursion guard (max 3) */

/* Style-array indirection: a fragment parse styles into a scratch buffer
 * instead of clobbering page prefix styles, since NsEmitChar indexes the
 * arrays with the local (restart-at-zero) di. */
static uint32_t* g_colCur = s_col;
static uint32_t* g_bgCur = s_bg;
static uint8_t* g_szCur = s_sz;
static uint8_t* g_flCur = s_fl;
static size_t g_colMaxCur = NS_BODY_MAX;

/* Console colors (0xAARRGGBB, matching the CSS pass values). */
#define NS_JS_COL_LOG 0xffaaaaaau
#define NS_JS_COL_WARN 0xffe0a030u
#define NS_JS_COL_ERR 0xffff5050u

/* document.write fragment conversion buffers, one pair per depth level:
 * nested writes parse while the outer parse is suspended, so static
 * buffers must not be shared across levels. */
#define NS_FRAG_MAX 8192
static char s_fragSrc[3][NS_FRAG_MAX + 1];
static char s_fragOut[3][NS_FRAG_MAX + 1];
/* Fragment parses write styles here; the converted output is re-emitted
 * under the run style, so the per-char styles are never read back. */
static uint32_t s_fragJunkCol[NS_FRAG_MAX + 1];
static uint32_t s_fragJunkBg[NS_FRAG_MAX + 1];
static uint8_t s_fragJunkFl[NS_FRAG_MAX + 1];
static uint8_t s_fragJunkSz[NS_FRAG_MAX + 1];

/* Lines that fit in the page box. The painter steps 19px from a baseline
 * 19px under the box top (56px when the Google wordmark is drawn). */
static int NsVisibleLines(void) {
	int origin = s_googleHome ? 56 : 19;
	int h;
	int n;
	if (!s_pageBox)
		return NS_PAGE_LINES;
	h = s_pageBox->wid.h - origin;
	if (h < 19)
		h = 19;
	n = h / 19;
	if (n < 1)
		n = 1;
	return n;
}

static void NsPageRender(void) {
	if (!s_pageBox || !s_mainWin)
		return;
	if (s_layoutActive) {
		ChTextBoxUpdate(s_pageBox, s_mainWin);
		return;
	}
	/* Clamp the first visible line, then slice one viewport. */
	int vis = NsVisibleLines();
	int total = 0;
	if (s_text[0]) {
		total = 1;
		for (const char* p = s_text; *p; p++)
			if (*p == '\n')
				total++;
	}
	int maxTop = total - vis;
	if (maxTop < 0)
		maxTop = 0;
	if (s_pageTop > maxTop)
		s_pageTop = maxTop;
	if (s_pageTop < 0)
		s_pageTop = 0;

	size_t di = 0;
	int line = 0;
	int shown = 0;
	bool marked = false;
	s_viewStart = 0;
	for (size_t i = 0; s_text[i] && di + 1 < sizeof(s_view); i++) {
		if (line < s_pageTop) {
			if (s_text[i] == '\n')
				line++;
			continue;
		}
		if (!marked) {
			s_viewStart = i;
			marked = true;
		}
		if (shown >= vis)
			break;
		s_view[di++] = s_text[i];
		if (s_text[i] == '\n')
			shown++;
	}
	s_view[di] = 0;
	ChTextBoxSetText(s_pageBox, s_view);
	ChTextBoxUpdate(s_pageBox, s_mainWin);
}

static void NsPagePaint(ChWidget* wid, ChWindow* win) {
	ChTextBox* tb = (ChTextBox*)wid;
	if (s_layoutActive) {
		NsLayoutPaint(win->canv, tb->wid.x, tb->wid.y, tb->wid.w, tb->wid.h, s_layoutScroll);
		return;
	}
	/* Styled page paint: per-character color/size from the CSS pass.
     * Line stride stays 19px so the NS_PAGE_LINES scroll math holds. */
	uint32_t bg = g_pageBg ? g_pageBg : 0xffffffffu;
	s_qHave = 0;
	s_btnHave = 0;
	s_luckyHave = 0;
	for (int li = 0; li < s_nlinks; li++)
		s_links[li].have = 0;
	ChDrawRect(win->canv, tb->wid.x, tb->wid.y, tb->wid.w, tb->wid.h, bg);
	if (!tb->font)
		return;
	ChRect clip;
	clip.x = tb->wid.x;
	clip.y = tb->wid.y;
	clip.w = tb->wid.w;
	clip.h = tb->wid.h;
	int penx = tb->wid.x + 4;
	int peny = tb->wid.y + 19;
	if (s_googleHome && tb->font) {
		static const char* letters = "Google";
		static const uint32_t cols[6] = {0xff4285f4u, 0xffea4335u, 0xfffbbc05u,
										 0xff4285f4u, 0xff34a853u, 0xffea4335u};
		int tw = 0;
		int i;
		for (i = 0; letters[i]; i++)
			tw += ChFontGetWidthChar(s_fontBig ? s_fontBig : tb->font, letters[i]);
		int x = tb->wid.x + (tb->wid.w - tw) / 2;
		int y = tb->wid.y + 36;
		ChFont* logo = s_fontBig ? s_fontBig : tb->font;
		for (i = 0; letters[i]; i++) {
			ChFontDrawCharClipped(win->canv, logo, letters[i], x, y, cols[i], &clip);
			x += ChFontGetWidthChar(logo, letters[i]);
		}
		peny = tb->wid.y + 56;
	}
	int lineMax = 0;
	int lineX0 = penx;
	for (size_t k = 0; s_view[k]; k++) {
		char c = s_view[k];
		size_t si = s_viewStart + k;
		uint32_t col = (si <= NS_BODY_MAX) ? s_col[si] : 0xff202124u;
		uint32_t runBg = (si <= NS_BODY_MAX) ? s_bg[si] : 0;
		uint8_t szpx = (si <= NS_BODY_MAX) ? (uint8_t)(s_sz[si] & 0x7f) : 14;
		uint8_t fl = (si <= NS_BODY_MAX) ? s_fl[si] : 0;
		if ((k == 0 || s_view[k - 1] == '\n') && c != '\n') {
			uint8_t raw0 = (si <= NS_BODY_MAX) ? s_sz[si] : 0;
			int indent = 0;
			int align = (fl >> 5) & 3;
			if (k > 0 && si > 0 && si - 1 <= NS_BODY_MAX) {
				indent = s_sz[si - 1];
				if (indent > 120)
					indent = 0;
				lineMax = (int)(s_col[si - 1] >> 8);
			} else {
				lineMax = 0;
			}
			int lineCenter = g_pageCenter || (raw0 & 0x80) || align == 1;
			if (lineCenter) {
				int width = 0;
				for (size_t t = k; s_view[t] && s_view[t] != '\n'; t++) {
					size_t si2 = s_viewStart + t;
					uint8_t sz2 = (si2 <= NS_BODY_MAX) ? (uint8_t)(s_sz[si2] & 0x7f) : 14;
					ChFont* f2 = (sz2 >= 17 && s_fontBig) ? s_fontBig : tb->font;
					width += ChFontGetWidthChar(f2, s_view[t]);
				}
				penx = tb->wid.x + (tb->wid.w - width) / 2;
				if (penx < tb->wid.x + 4)
					penx = tb->wid.x + 4;
			} else if (align == 2) {
				int width = 0;
				for (size_t t = k; s_view[t] && s_view[t] != '\n'; t++) {
					size_t si2 = s_viewStart + t;
					uint8_t sz2 = (si2 <= NS_BODY_MAX) ? (uint8_t)(s_sz[si2] & 0x7f) : 14;
					ChFont* f2 = (sz2 >= 17 && s_fontBig) ? s_fontBig : tb->font;
					width += ChFontGetWidthChar(f2, s_view[t]);
				}
				penx = tb->wid.x + tb->wid.w - width - 4;
				if (penx < tb->wid.x + 4 + indent)
					penx = tb->wid.x + 4 + indent;
			} else {
				penx = tb->wid.x + 4 + indent;
			}
			lineX0 = penx;
			{
				const char* line = s_view + k;
				size_t llen = 0;
				while (line[llen] && line[llen] != '\n')
					llen++;
				if (s_googleHome && llen == 12 && !strncmp(line, "Gmail Images", 12)) {
					int width = 0;
					int x;
					for (size_t t = 0; t < llen; t++)
						width += ChFontGetWidthChar(tb->font, line[t]);
					x = tb->wid.x + tb->wid.w - width - 88;
					if (x < tb->wid.x + 4)
						x = tb->wid.x + 4;
					for (size_t t = 0; t < llen; t++) {
						ChFontDrawCharClipped(win->canv, tb->font, line[t], x, tb->wid.y + 22,
											  0xff202124u, &clip);
						x += ChFontGetWidthChar(tb->font, line[t]);
					}
					k += llen;
					continue;
				}
				if (s_googleHome && llen == 7 && !strncmp(line, "Sign in", 7)) {
					int bw = 72;
					int bx = tb->wid.x + tb->wid.w - bw - 8;
					ChRect chip;
					ChDrawRect(win->canv, bx, tb->wid.y + 6, bw, 22, 0xff1a73e8u);
					chip.x = bx;
					chip.y = tb->wid.y + 6;
					chip.w = bw;
					chip.h = 22;
					ChFontDrawTextClipped(win->canv, tb->font, "Sign in", bx + 12, tb->wid.y + 22,
										  0xffffffffu, &chip);
					k += llen;
					continue;
				}
				if (llen == 17 && !strncmp(line, "[ Search Google ]", 17)) {
					int bw = tb->wid.w - 120;
					int bx;
					if (bw < 40)
						bw = tb->wid.w > 8 ? tb->wid.w - 8 : tb->wid.w;
					bx = tb->wid.x + (tb->wid.w - bw) / 2;
					ChDrawRect(win->canv, bx, peny - 16, bw, 26, 0xffffffffu);
					ChDrawRectUnfilled(win->canv, bx, peny - 16, bw, 26,
									   s_qFocus ? 0xff4285f4u : 0xffdadce0u);
					s_qX = bx;
					s_qY = peny - 16;
					s_qW = bw;
					s_qH = 26;
					s_qHave = 1;
					if (s_query[0] && tb->font) {
						ChRect field;
						field.x = bx + 8;
						field.y = peny - 16;
						field.w = bw - 16;
						field.h = 26;
						ChFontDrawTextClipped(win->canv, tb->font, s_query, bx + 8, peny,
											  0xff202124u, &field);
					}
					if (s_qFocus && tb->font) {
						int cw = 0;
						size_t ci;
						for (ci = 0; ci < s_qCursor && s_query[ci]; ci++)
							cw += ChFontGetWidthChar(tb->font, s_query[ci]);
						if (cw > bw - 20)
							cw = bw - 20;
						ChDrawRect(win->canv, bx + 8 + cw, peny - 14, 2, 18, 0xff202124u);
					}
					k += llen;
					peny += 19;
					continue;
				}
				if (llen >= 5 && line[0] == '[' && line[1] == ' ') {
					/* One centered row of "[ label ]" chips, Google Search
					 * beside I'm Feeling Lucky instead of a stack. */
					int total = 0;
					int nchip = 0;
					size_t scan = 0;
					while (scan < llen) {
						while (scan < llen && line[scan] == ' ')
							scan++;
						if (scan >= llen || line[scan] != '[')
							break;
						size_t e = scan;
						int tw = 0;
						while (e < llen && line[e] != ']')
							e++;
						if (e >= llen)
							break;
						for (size_t t = scan + 2; t + 1 < e; t++)
							tw += ChFontGetWidthChar(tb->font, line[t]);
						total += tw + 24;
						nchip++;
						scan = e + 1;
					}
					if (nchip > 0) {
						int gap = 8;
						int row = total + gap * (nchip - 1);
						int bx = tb->wid.x + (tb->wid.w - row) / 2;
						if (bx < tb->wid.x + 4)
							bx = tb->wid.x + 4;
						scan = 0;
						while (scan < llen) {
							char label[80];
							int tw = 0;
							size_t t;
							size_t e;
							int bw;
							ChRect chip;
							while (scan < llen && line[scan] == ' ')
								scan++;
							if (scan >= llen || line[scan] != '[')
								break;
							e = scan;
							while (e < llen && line[e] != ']')
								e++;
							if (e >= llen)
								break;
							t = 0;
							for (size_t u = scan + 2; u + 1 < e && t + 1 < sizeof(label); u++) {
								label[t++] = line[u];
								tw += ChFontGetWidthChar(tb->font, line[u]);
							}
							label[t] = 0;
							bw = tw + 24;
							ChDrawRect(win->canv, bx, peny - 16, bw, 24, 0xfff8f9fau);
							ChDrawRectUnfilled(win->canv, bx, peny - 16, bw, 24, 0xffdadce0u);
							chip.x = bx;
							chip.y = peny - 16;
							chip.w = bw;
							chip.h = 24;
							ChFontDrawTextClipped(win->canv, tb->font, label, bx + 12, peny,
												  0xff3c4043u, &chip);
							if (!strcmp(label, "Google Search")) {
								s_btnX = bx;
								s_btnY = peny - 16;
								s_btnW = bw;
								s_btnH = 24;
								s_btnHave = 1;
							} else if (!strcmp(label, "I'm Feeling Lucky")) {
								s_luckyX = bx;
								s_luckyY = peny - 16;
								s_luckyW = bw;
								s_luckyH = 24;
								s_luckyHave = 1;
							}
							bx += bw + gap;
							scan = e + 1;
						}
						k += llen;
						peny += 19;
						continue;
					}
				}
			}
		}
		if (c == '\x01' && s_view[k + 1] >= '0' && s_view[k + 1] < '0' + NS_IMG_MAX) {
			int ix = s_view[k + 1] - '0';
			k++;
			if (ix < s_nimg && s_imgPx[ix] && s_imgW[ix] > 0 && s_imgH[ix] > 0) {
				int dw = tb->wid.w - 16;
				int dh = s_imgH[ix];
				int row, col;
				if (dw > s_imgW[ix])
					dw = s_imgW[ix];
				if (dw > 480)
					dw = 480;
				if (dh > 280)
					dh = 280;
				for (row = 0; row < dh; row++) {
					int sy = row * s_imgH[ix] / dh;
					uint32_t* dstp = win->canv->buffer +
									 (uint32_t)(peny + row) * win->canv->canvasWidth + (tb->wid.x + 8);
					if (peny + row >= tb->wid.y + tb->wid.h)
						break;
					for (col = 0; col < dw; col++) {
						int sx = col * s_imgW[ix] / dw;
						dstp[col] = s_imgPx[ix][sy * s_imgW[ix] + sx];
					}
				}
				peny += dh + 4;
			}
			penx = tb->wid.x + 4;
			continue;
		}
		if (c == '\n') {
			int step = 19;
			if (si <= NS_BODY_MAX) {
				int packed = (int)(s_col[si] & 0xff);
				if (packed >= 14 && packed <= 36)
					step = packed;
				if (s_bg[si])
					ChDrawRect(win->canv, tb->wid.x + 4, peny + 2, tb->wid.w - 8, 1, s_bg[si]);
			}
			penx = tb->wid.x + 4;
			peny += step;
			if (peny > tb->wid.y + tb->wid.h)
				break;
			continue;
		}
		/* White glyphs on the white page were the "HTTPS only" screen. */
		{
			uint32_t bgc = runBg ? runBg : (g_pageBg ? g_pageBg : 0xffffffffu);
			int cr = (int)((col >> 16) & 255), cg = (int)((col >> 8) & 255), cb = (int)(col & 255);
			int br = (int)((bgc >> 16) & 255), bgg = (int)((bgc >> 8) & 255), bb = (int)(bgc & 255);
			int cl = (cr * 3 + cg * 6 + cb) / 10;
			int bl = (br * 3 + bgg * 6 + bb) / 10;
			int d = cl - bl;
			if (d < 0)
				d = -d;
			if (d < 48)
				col = bl > 140 ? 0xff202124u : 0xffffffffu;
		}
		{
			ChFont* f = (szpx >= 17 && s_fontBig) ? s_fontBig : tb->font;
			int cw = ChFontGetWidthChar(f, c);
			if (runBg && cw > 0) {
				int by = peny - 14;
				if (by < tb->wid.y)
					by = tb->wid.y;
				ChDrawRect(win->canv, penx, by, cw, 18, runBg);
			}
			if (lineMax > 0 && !(fl & 0x80) && penx > lineX0 && penx + cw > lineX0 + lineMax) {
				penx = lineX0;
				peny += 19;
				if (peny > tb->wid.y + tb->wid.h)
					break;
			}
			if (fl & NS_CSS_ITALIC)
				ChFontDrawCharClipped(win->canv, f, c, penx + 1, peny, col, &clip);
			ChFontDrawCharClipped(win->canv, f, c, penx, peny, col, &clip);
			if (fl & NS_CSS_BOLD)
				ChFontDrawCharClipped(win->canv, f, c, penx + 1, peny, col, &clip);
			if (fl & NS_CSS_UNDER)
				ChDrawRect(win->canv, penx, peny + 2, cw > 0 ? cw : 1, 1, col);
			if (fl & NS_CSS_STRIKE)
				ChDrawRect(win->canv, penx, peny - 5, cw > 0 ? cw : 1, 1, col);
			if (fl & NS_CSS_OVER)
				ChDrawRect(win->canv, penx, peny - 12, cw > 0 ? cw : 1, 1, col);
			for (int li = 0; li < s_nlinks; li++) {
				if (si < s_links[li].a || si >= s_links[li].b)
					continue;
				ChDrawRect(win->canv, penx, peny + 2, cw > 0 ? cw : 1, 1, 0xff1a0dabu);
				if (!s_links[li].have) {
					s_links[li].x = penx;
					s_links[li].y = peny - 14;
					s_links[li].w = cw;
					s_links[li].h = 18;
					s_links[li].have = 1;
				} else if (penx + cw > s_links[li].x + s_links[li].w &&
						   peny - 14 < s_links[li].y + s_links[li].h) {
					s_links[li].w = penx + cw - s_links[li].x;
				}
			}
			penx += cw;
		}
	}
}

static void NsPageScroll(int delta) {
	if (s_layoutActive) {
		int vh = s_pageBox ? s_pageBox->wid.h : 420;
		int max = NsLayoutPageHeight() - vh;
		if (max < 0)
			max = 0;
		s_layoutScroll += delta * 19;
		if (s_layoutScroll > max)
			s_layoutScroll = max;
		if (s_layoutScroll < 0)
			s_layoutScroll = 0;
		NsPageRender();
		return;
	}
	s_pageTop += delta;
	NsPageRender();
}

static void NsShowStatus(const char* msg) {
	if (!s_pageBox || !s_mainWin || !msg)
		return;
	/* Every new page/message resets the scroll to the top. */
	if (msg != (const char*)s_text) {
		s_layoutActive = 0;
		strncpy(s_text, msg, sizeof(s_text) - 1);
		s_text[sizeof(s_text) - 1] = 0;
		/* Unstyled message: UA white on dark, base size. */
		for (size_t f = 0; f <= NS_BODY_MAX && s_text[f]; f++) {
			s_col[f] = 0xffffffffu;
			s_bg[f] = 0;
			s_sz[f] = 14;
			s_fl[f] = 0;
		}
		s_nlinks = 0;
	}
	s_pageTop = 0;
	if (msg == (const char*)s_text)
		s_layoutScroll = 0;
	NsPageRender();
}

static void NsUrlPaint(ChWidget* wid, ChWindow* win) {
	/* Custom single-line paint: the default textbox painter starts its pen
     * 38px below the box top, which never fits a 24px bar -- and hover
     * repaints invoke the handler directly, so per-update cursor tweaks
     * don't survive mouse movement. Fixed pen here instead. */
	ChTextBox* tb = (ChTextBox*)wid;
	ChDrawRect(win->canv, tb->wid.x, tb->wid.y, tb->wid.w, tb->wid.h, 0xFFFFFFFFu);
	if (!tb->text || !tb->text[0] || !tb->font)
		return;
	ChRect clip;
	clip.x = tb->wid.x;
	clip.y = tb->wid.y;
	clip.w = tb->wid.w;
	clip.h = tb->wid.h;
	ChFontDrawTextClipped(
		win->canv, tb->font, tb->text, tb->wid.x + 4, tb->wid.y + 19, tb->textColor, &clip);
	/* Block caret at the edit cursor: measure the prefix width. */
	if (tb->font) {
		size_t len = strlen(tb->text);
		size_t caret = s_urlCursor <= len ? s_urlCursor : len;
		int64_t cw = 0;
		if (caret > 0) {
			char prefix[NS_URL_MAX + 1];
			memcpy(prefix, tb->text, caret);
			prefix[caret] = 0;
			cw = ChFontGetWidth(tb->font, prefix);
			if (cw < 0)
				cw = 0;
		}
		int cx = tb->wid.x + 4 + (int)cw;
		if (cx + 2 <= tb->wid.x + tb->wid.w)
			ChDrawRect(win->canv, cx, tb->wid.y + 3, 2, tb->wid.h - 6, tb->textColor);
	}
}

static void NsShowUrl(void) {
	if (!s_urlBox || !s_mainWin)
		return;
	ChTextBoxSetText(s_urlBox, s_url);
	ChTextBoxUpdate(s_urlBox, s_mainWin);
}

/* Clicking the URL bar blurs a page form and places the edit caret at the
 * clicked character.  Keyboard input is handled globally by NsHandleKey. */
static void NsUrlMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	(void)y;
	if (!button || !wid || !win)
		return;
	NsFormClearFocus();
	int rel = x - (win->info->x + wid->x) - 4;
	size_t i = 0;
	int w = 0;
	ChTextBox* tb = (ChTextBox*)wid;
	if (rel < 0)
		rel = 0;
	if (tb->font) {
		while (s_url[i]) {
			int cw = ChFontGetWidthChar(tb->font, s_url[i]);
			if (w + cw / 2 > rel)
				break;
			w += cw;
			i++;
		}
	} else {
		i = strlen(s_url);
	}
	s_urlCursor = i;
	NsShowUrl();
}

/* Readability-mode HTML to text (NOT a layout engine): block tags become
 * newlines, list items get bullets, links keep their URLs, script/style
 * bodies are dropped, entities (named + numeric) are decoded. */
static void NsEmitChar(char* dst, size_t dstsz, size_t* di, char c) {
	if (*di + 1 >= dstsz)
		return;
	if (c != '\n' && g_runWidth && !g_runNowrap && g_linePx >= (int)g_runWidth && g_linePx > 0) {
		g_linePx = 0;
		NsEmitChar(dst, dstsz, di, '\n');
	}
	if (g_runCase == 1 && c >= 'a' && c <= 'z')
		c = (char)(c - 32);
	else if (g_runCase == 2 && c >= 'A' && c <= 'Z')
		c = (char)(c + 32);
	dst[*di] = c;
	if (*di <= g_colMaxCur) {
		uint8_t sz = g_runSize;
		if (sz > 127)
			sz = 127;
		if (g_runCenter && c != '\n')
			sz = (uint8_t)(sz | 0x80);
		g_colCur[*di] = g_runColor;
		g_bgCur[*di] = g_runBg;
		g_szCur[*di] = sz;
		g_flCur[*di] = (uint8_t)(g_runDecor | (g_runAlign << 5) | (g_runNowrap ? 0x80 : 0));
		if (c == '\n') {
			g_szCur[*di] = g_runIndent;
			g_colCur[*di] = g_runLine | ((uint32_t)g_runWidth << 8);
			g_bgCur[*di] = g_runBorder;
			g_linePx = 0;
		} else {
			g_linePx += 8;
		}
	}
	(*di)++;
}

static void NsEmitNewlines(char* dst, size_t dstsz, size_t* di, int want) {
	int have = 0;
	size_t t = *di;
	while (t > 0 && dst[t - 1] == '\n' && have < want) {
		t--;
		have++;
	}
	while (have < want && *di + 1 < dstsz) {
		NsEmitChar(dst, dstsz, di, '\n');
		have++;
	}
}

static void NsImgReset(void) {
	int i;
	for (i = 0; i < s_nimg; i++) {
		free(s_imgPx[i]);
		s_imgPx[i] = NULL;
	}
	s_nimg = 0;
}

/* 1 = png, 2 = jpeg/jpg. The query string is ignored. SVG is neither. */
static int NsImgKind(const char* href) {
	const char* q = strchr(href, '?');
	const char* end = q ? q : href + strlen(href);
	const char* s = end;
	char tail[8];
	int n = 0;
	int i;
	while (s > href && n < 7) {
		s--;
		n++;
	}
	for (i = 0; i < n; i++) {
		char c = s[i];
		if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
		tail[i] = c;
	}
	tail[n] = 0;
	if (n >= 4 && !strcmp(tail + n - 4, ".png"))
		return 1;
	if (n >= 4 && !strcmp(tail + n - 4, ".jpg"))
		return 2;
	if (n >= 5 && !strcmp(tail + n - 5, ".jpeg"))
		return 2;
	if (n >= 4 && !strcmp(tail + n - 4, ".svg"))
		return 3;
	return 0;
}

static void* NsImgAlloc(uint32_t n) {
	return malloc(n);
}

int NsJpegDecode(const unsigned char* data, size_t len, unsigned** out_px, int* out_w,
						int* out_h) {
	void* mem;
	Jpeg::Decoder* d;
	unsigned char* src;
	unsigned* px;
	int w, h, i, n;
	*out_px = NULL;
	if (!data || len < 2 || data[0] != 0xFF || data[1] != 0xD8)
		return -1;
	mem = malloc(sizeof(Jpeg::Decoder));
	if (!mem)
		return -1;
	d = new (mem) Jpeg::Decoder(data, (uint32_t)len, NsImgAlloc, free);
	if (d->GetResult() != Jpeg::Decoder::OK || d->GetWidth() <= 0 || d->GetHeight() <= 0 ||
		d->GetWidth() > 1920 || d->GetHeight() > 1920) {
		d->~Decoder();
		free(mem);
		return -1;
	}
	w = d->GetWidth();
	h = d->GetHeight();
	n = w * h;
	px = (unsigned*)malloc((size_t)n * 4);
	src = d->GetImage();
	if (!px || !src) {
		free(px);
		d->~Decoder();
		free(mem);
		return -1;
	}
	if (d->IsColor()) {
		for (i = 0; i < n; i++)
			px[i] = 0xff000000u | ((unsigned)src[i * 3] << 16) | ((unsigned)src[i * 3 + 1] << 8) |
					src[i * 3 + 2];
	} else {
		for (i = 0; i < n; i++)
			px[i] = 0xff000000u | ((unsigned)src[i] << 16) | ((unsigned)src[i] << 8) | src[i];
	}
	d->~Decoder();
	free(mem);
	*out_px = px;
	*out_w = w;
	*out_h = h;
	return 0;
}

static int NsImgQueue(const char* href) {
	char abs[NS_URL_MAX * 2];
	int ix;
	if (!href || !href[0] || s_nimg >= NS_IMG_MAX)
		return -1;
	if (!NsImgKind(href))
		return -1;
	NsResolveUrl(s_docUrl[0] ? s_docUrl : s_url, href, abs, sizeof(abs));
	if (strncmp(abs, "http://", 7) && strncmp(abs, "https://", 8))
		return -1;
	ix = s_nimg++;
	snprintf(s_imgUrl[ix], sizeof(s_imgUrl[ix]), "%s", abs);
	s_imgPx[ix] = NULL;
	s_imgW[ix] = s_imgH[ix] = 0;
	s_imgIW[ix] = s_imgIH[ix] = 0;
	return ix;
}

static void NsImgDecode(int ix, xe_http_response* resp) {
	unsigned* px = NULL;
	int w = 0, h = 0, iw = 0, ih = 0, kind, ok;
	kind = NsImgKind(s_imgUrl[ix]);
	if (!kind || !resp || !resp->body)
		return;
	if (kind == 1)
		ok = ns_png_decode((const unsigned char*)resp->body, resp->body_len, &px, &w, &h);
	else if (kind == 2)
		ok = NsJpegDecode((const unsigned char*)resp->body, resp->body_len, &px, &w, &h);
	else
		ok = ns_svg_decode(resp->body, resp->body_len, &px, &w, &h, &iw, &ih);
	if (ok != 0 || !px || w <= 0 || h <= 0)
		goto done;
	if (iw <= 0 || ih <= 0) {
		iw = w;
		ih = h;
	}
	s_imgPx[ix] = (uint32_t*)px;
	s_imgW[ix] = w;
	s_imgH[ix] = h;
	s_imgIW[ix] = iw;
	s_imgIH[ix] = ih;
	px = NULL;
done:
	free(px);
}

static void NsImgFetchAll(void) {
	int i = 0;
	/* One thread only. A worker returns to ELR 0, and dlmalloc is not
	 * safe on more than one thread. Overlapped GETs stay on this thread. */
	if (s_nimg <= 0)
		return;
	_KePrint("[img] parallel %d\n", s_nimg);
	while (i < s_nimg) {
		const char* urls[XE_HTTP_MANY_MAX];
		xe_http_response resp[XE_HTTP_MANY_MAX];
		size_t caps[XE_HTTP_MANY_MAX];
		int rcs[XE_HTTP_MANY_MAX];
		int map[XE_HTTP_MANY_MAX];
		int n = 0;
		int k;
		while (n < XE_HTTP_MANY_MAX && i < s_nimg) {
			int kind = NsImgKind(s_imgUrl[i]);
			if (kind) {
				urls[n] = s_imgUrl[i];
				caps[n] = (kind == 3) ? (2u * 1024u * 1024u) : (512u * 1024u);
				map[n] = i;
				n++;
			}
			i++;
		}
		if (!n)
			continue;
		if (xe_http_get_many(urls, n, kNsUa, resp, caps, rcs) != 0)
			continue;
		for (k = 0; k < n; k++) {
			if (rcs[k] == 0 && resp[k].body)
				NsImgDecode(map[k], &resp[k]);
			xe_http_response_free(&resp[k]);
		}
	}
}

/* Inline SVG support.  Google's logo (and most current headers) is an
 * <svg> in the DOM rather than an image URL.  Serialise the small SVG
 * subtree and rasterise it through the same decoder used for .svg files. */
static void NsSvgAppend(char* out, size_t cap, size_t* len, const char* text) {
	size_t n;
	if (!out || !len || !text)
		return;
	n = strlen(text);
	if (*len + n >= cap)
		n = cap - *len - 1;
	if (n) {
		memcpy(out + *len, (void*)text, n);
		*len += n;
	}
}

static void NsSvgAppendEscaped(char* out, size_t cap, size_t* len, const char* text) {
	char tmp[2];
	while (text && *text) {
		if (*text == '&')
			NsSvgAppend(out, cap, len, "&amp;");
		else if (*text == '"')
			NsSvgAppend(out, cap, len, "&quot;");
		else if (*text == '<')
			NsSvgAppend(out, cap, len, "&lt;");
		else {
			tmp[0] = *text;
			tmp[1] = 0;
			NsSvgAppend(out, cap, len, tmp);
		}
		text++;
	}
}

static const char* NsSvgTagName(const char* tag) {
	if (!strcmp(tag, "clippath")) return "clipPath";
	if (!strcmp(tag, "lineargradient")) return "linearGradient";
	if (!strcmp(tag, "radialgradient")) return "radialGradient";
	if (!strcmp(tag, "pattern")) return "pattern";
	if (!strcmp(tag, "textpath")) return "textPath";
	return tag;
}

static const char* NsSvgAttrName(const char* name) {
	if (!strcmp(name, "viewbox")) return "viewBox";
	if (!strcmp(name, "preserveaspectratio")) return "preserveAspectRatio";
	if (!strcmp(name, "gradientunits")) return "gradientUnits";
	if (!strcmp(name, "gradienttransform")) return "gradientTransform";
	if (!strcmp(name, "patternunits")) return "patternUnits";
	if (!strcmp(name, "patterncontentunits")) return "patternContentUnits";
	if (!strcmp(name, "patterntransform")) return "patternTransform";
	if (!strcmp(name, "markerwidth")) return "markerWidth";
	if (!strcmp(name, "markerheight")) return "markerHeight";
	if (!strcmp(name, "markerunits")) return "markerUnits";
	if (!strcmp(name, "refx")) return "refX";
	if (!strcmp(name, "refy")) return "refY";
	return name;
}

static bool NsSvgSelfClosing(const char* tag) {
	return !strcmp(tag, "path") || !strcmp(tag, "rect") || !strcmp(tag, "circle") ||
	       !strcmp(tag, "ellipse") || !strcmp(tag, "line") || !strcmp(tag, "polyline") ||
	       !strcmp(tag, "polygon") || !strcmp(tag, "stop") || !strcmp(tag, "use");
}

static void NsSvgSerializeNode(NsCssNode* n, char* out, size_t cap, size_t* len, int depth) {
	uint32_t i;
	bool self;
	if (!n || n->is_text || depth > 20)
		return;
	NsSvgAppend(out, cap, len, "<");
	NsSvgAppend(out, cap, len, NsSvgTagName(n->tag));
	for (i = 0; i < n->n_attrs; i++) {
		const char* an = n->attrs[i].name;
		const char* av = n->attrs[i].value;
		if (!an || !av || !an[0])
			continue;
		if (!strcmp(an, "class") || !strcmp(an, "id") || !strcmp(an, "role") ||
		    !strcmp(an, "aria-label") || !strcmp(an, "focusable") ||
		    !strcmp(an, "aria-hidden") || !strcmp(an, "tabindex"))
			continue;
		NsSvgAppend(out, cap, len, " ");
		NsSvgAppend(out, cap, len, NsSvgAttrName(an));
		NsSvgAppend(out, cap, len, "=\"");
		NsSvgAppendEscaped(out, cap, len, av);
		NsSvgAppend(out, cap, len, "\"");
	}
	self = NsSvgSelfClosing(n->tag) || !n->first_child;
	if (self) {
		NsSvgAppend(out, cap, len, "/>");
		return;
	}
	NsSvgAppend(out, cap, len, ">");
	for (NsCssNode* c = n->first_child; c; c = c->next) {
		if (c->is_text) {
			NsSvgAppendEscaped(out, cap, len, c->text);
			continue;
		}
		if (!strcmp(c->tag, "script") || !strcmp(c->tag, "style"))
			continue;
		NsSvgSerializeNode(c, out, cap, len, depth + 1);
	}
	NsSvgAppend(out, cap, len, "</");
	NsSvgAppend(out, cap, len, NsSvgTagName(n->tag));
	NsSvgAppend(out, cap, len, ">");
}

extern "C" const uint32_t* NsInlineSvgLookup(NsCssNode* node, int* w, int* h,
											int* iw, int* ih) {
	static char markup[96 * 1024];
	size_t len = 0;
	unsigned* px = NULL;
	int bw = 0, bh = 0, biw = 0, bih = 0;
	if (w) *w = 0;
	if (h) *h = 0;
	if (iw) *iw = 0;
	if (ih) *ih = 0;
	if (!node)
		return NULL;
	markup[0] = 0;
	NsSvgSerializeNode(node, markup, sizeof(markup) - 1, &len, 0);
	if (!len)
		return NULL;
	markup[len] = 0;
	if (ns_svg_decode(markup, len, &px, &bw, &bh, &biw, &bih) != 0 || !px)
		return NULL;
	if (w) *w = bw;
	if (h) *h = bh;
	if (iw) *iw = biw > 0 ? biw : bw;
	if (ih) *ih = bih > 0 ? bih : bh;
	return px;
}

/* Called by the layout engine for every <img>.  Resolve relative URLs
 * against the document, not the current URL-bar text. */
extern "C" const uint32_t* NsImageLookup(const char* src, int* w, int* h,
										 int* iw, int* ih) {
	char abs[NS_URL_MAX * 2];
	int i;
	if (w)
		*w = 0;
	if (h)
		*h = 0;
	if (iw)
		*iw = 0;
	if (ih)
		*ih = 0;
	if (!src || !src[0])
		return NULL;
	NsResolveUrl(s_docUrl[0] ? s_docUrl : s_url, src, abs, sizeof(abs));
	for (i = 0; i < s_nimg; i++) {
		if (!s_imgPx[i] || s_imgW[i] <= 0 || s_imgH[i] <= 0)
			continue;
		if (!strcmp(s_imgUrl[i], abs) || !strcmp(s_imgUrl[i], src)) {
			if (w)
				*w = s_imgW[i];
			if (h)
				*h = s_imgH[i];
			if (iw)
				*iw = s_imgIW[i] > 0 ? s_imgIW[i] : s_imgW[i];
			if (ih)
				*ih = s_imgIH[i] > 0 ? s_imgIH[i] : s_imgH[i];
			return s_imgPx[i];
		}
	}
	return NULL;
}

static void NsEmitStr(char* dst, size_t dstsz, size_t* di, const char* s) {
	while (*s && *di + 1 < dstsz) {
		if (*s == '\n') {
			NsEmitNewlines(dst, dstsz, di, 1);
		} else {
			NsEmitChar(dst, dstsz, di, *s);
		}
		s++;
	}
}

/* Decode one entity at src[i]=='&'. Returns chars consumed (0 = literal). */
static int NsEntity(const char* src, size_t srclen, size_t i, char* dst, size_t dstsz, size_t* di) {
	static const struct {
		const char* name;
		const char* val;
	} kEnts[] = {{"amp", "&"},
				 {"lt", "<"},
				 {"gt", ">"},
				 {"quot", "\""},
				 {"nbsp", " "},
				 {"copy", "(c)"},
				 {"reg", "(R)"},
				 {"mdash", "-"},
				 {"ndash", "-"},
				 {"hellip", "..."},
				 {"trade", "(TM)"},
				 {NULL, NULL}};
	if (i + 1 < srclen && src[i + 1] == '#') {
		size_t j = i + 2;
		int base = 10;
		if (j < srclen && (src[j] == 'x' || src[j] == 'X')) {
			base = 16;
			j++;
		}
		unsigned v = 0;
		size_t digits = 0;
		while (j < srclen && digits < 6) {
			int d = -1;
			if (src[j] >= '0' && src[j] <= '9')
				d = src[j] - '0';
			else if (base == 16 && src[j] >= 'a' && src[j] <= 'f')
				d = src[j] - 'a' + 10;
			else if (base == 16 && src[j] >= 'A' && src[j] <= 'F')
				d = src[j] - 'A' + 10;
			else
				break;
			v = v * (unsigned)base + (unsigned)d;
			j++;
			digits++;
		}
		if (digits == 0)
			return 0;
		if (j < srclen && src[j] == ';')
			j++;
		if (v > 0 && v < 128)
			NsEmitChar(dst, dstsz, di, (char)v);
		else if (s_latinLink && !s_latinUsed) {
			NsEmitStr(dst, dstsz, di, s_latinLink);
			s_latinUsed = 1;
		} else if (!s_latinLink)
			NsEmitChar(dst, dstsz, di, '?');
		return (int)(j - i);
	}
	for (int k = 0; kEnts[k].name; k++) {
		size_t nl = strlen(kEnts[k].name);
		if (i + 1 + nl < srclen && !strncmp(src + i + 1, kEnts[k].name, nl) &&
			src[i + 1 + nl] == ';') {
			NsEmitStr(dst, dstsz, di, kEnts[k].val);
			return (int)(nl + 2);
		}
	}
	return 0;
}

/* attribute="value" within tag text [j,k), case-insensitive name. */
static bool
NsTagAttr(const char* src, size_t j, size_t k, const char* name, char* out, size_t outsz) {
	size_t nl = strlen(name);
	for (size_t h = j; h + nl < k; h++) {
		size_t t;
		for (t = 0; t < nl; t++) {
			char a = src[h + t];
			if (a >= 'A' && a <= 'Z')
				a += 32;
			if (a != name[t])
				break;
		}
		if (t != nl)
			continue;
		/* Name must end at a boundary (space, =, /, >). */
		{
			char e = src[h + nl];
			if (e != ' ' && e != '\t' && e != '=' && e != '/' && e != '>')
				continue;
		}
		size_t v = h + nl;
		while (v < k && (src[v] == ' ' || src[v] == '\t' || src[v] == '='))
			v++;
		if (v >= k)
			return false;
		if (src[v] == '"' || src[v] == '\'') {
			char aq = src[v++];
			size_t ol = 0;
			while (v < k && src[v] != aq && ol + 1 < outsz)
				out[ol++] = src[v++];
			out[ol] = 0;
			return true;
		}
		{
			size_t ol = 0;
			while (v < k && src[v] != ' ' && src[v] != '\t' && src[v] != '>' && ol + 1 < outsz)
				out[ol++] = src[v++];
			out[ol] = 0;
			return ol > 0;
		}
	}
	return false;
}

#define NS_CSS_STACK 24

static void NsRunFromTop(NsCssNode** stack, int depth) {
	NsCssNode* top = (depth > 0) ? stack[depth - 1] : NULL;
	if (!top) {
		g_runColor = 0xff202124u;
		g_runBg = 0;
		g_runSize = 14;
		g_runCenter = 0;
		g_runDecor = 0;
		g_runAlign = 0;
		g_runIndent = 0;
		g_runLine = 19;
		g_runCase = 0;
		g_runPre = 0;
		g_runNowrap = 0;
		g_runWidth = 0;
		g_runBorder = 0;
		return;
	}
	g_runColor = NsCssNodeColor(top);
	g_runBg = 0;
	for (int bi = depth - 1; bi >= 0; bi--) {
		uint32_t b = NsCssNodeBackground(stack[bi]);
		if (!b || b == g_pageBg)
			continue;
		g_runBg = b;
		break;
	}
	g_runSize = (uint8_t)NsCssNodeFontPx(top);
	{
		NsCssUsed u;
		NsCssNodeUsed(top, &u);
		g_runDecor = u.decor;
		g_runAlign = u.align;
		g_runIndent = u.indent;
		g_runLine = u.line ? u.line : 19;
		g_runCase = u.upper;
		g_runPre = u.pre;
		g_runNowrap = u.nowrap;
		g_runWidth = u.width;
		g_runBorder = u.border;
		if (u.opacity < 255) {
			uint32_t onto = g_runBg ? g_runBg : g_pageBg;
			g_runColor = NsMixOpacity(g_runColor, onto, u.opacity);
		}
	}
	g_runCenter = 0;
	for (int ci = 0; ci < depth; ci++) {
		if (!strcmp(stack[ci]->tag, "center") || NsCssNodeCenter(stack[ci]))
			g_runCenter = 1;
	}
	if (!top->parent) {
		uint32_t page = NsCssNodeBackground(top);
		if (page)
			g_pageBg = page;
	}
	if (!strcmp(top->tag, "html") || !strcmp(top->tag, "body")) {
		uint32_t page = NsCssNodeBackground(top);
		if (page)
			g_pageBg = page;
		if (NsCssNodeCenter(top))
			g_pageCenter = 1;
	}
}

/* libcss drops var(). Google's sheet is almost entirely var(--token),
 * so text and background never resolved. Substitute a single-token
 * custom property, or the fallback, when the replacement fits. */
/* Last `--name:` whose value is a single token with no function call. */
static int NsCssVarValue(const char* name, size_t nl, const char** val, size_t* vl,
						  const char* gap_lo, const char* gap_hi) {
	const char* p = s_cssBuf;
	int found = 0;
	*val = NULL;
	*vl = 0;
	while ((p = strstr(p, "--")) != NULL) {
		if (p >= gap_lo && p < gap_hi) {
			p = gap_hi;
			continue;
		}
		const char* v;
		const char* e;
		size_t n;
		char prev;
		prev = (p == s_cssBuf) ? ';' : p[-1];
		if (prev != '{' && prev != ';' && prev != ' ' && prev != '\t' &&
			prev != '\n' && prev != '\r') {
			p += 2;
			continue;
		}
		if (strncmp(p + 2, name, nl) != 0 || p[2 + nl] != ':') {
			p += 2;
			continue;
		}
		v = p + 2 + nl + 1;
		while (*v == ' ' || *v == '\t')
			v++;
		e = v;
		while (*e && *e != ';' && *e != '}' && *e != '{')
			e++;
		n = (size_t)(e - v);
		while (n && (v[n - 1] == ' ' || v[n - 1] == '\t'))
			n--;
		if (n && n < 48 && !memchr(v, '(', n)) {
			*val = v;
			*vl = n;
			found = 1;
		}
		p += 2;
	}
	return found;
}

static void NsCssResolveVars(void) {
	char* r = s_cssBuf;
	char* w = s_cssBuf;
	while (*r) {
		if (!strncmp(r, "var(--", 6)) {
			char* n = r + 6;
			char* c = n;
			char* end = NULL;
			const char* rep = NULL;
			size_t rl = 0;
			size_t nl;
			while (*c && (isalpha((unsigned char)*c) || isdigit((unsigned char)*c) ||
						  *c == '_' || *c == '-'))
				c++;
			nl = (size_t)(c - n);
			if (nl && (*c == ')' || *c == ',')) {
				if (*c == ')')
					end = c + 1;
				else {
					end = strchr(c, ')');
					if (end)
						end++;
				}
				NsCssVarValue(n, nl, &rep, &rl, w, r);
				if (end && !rep && *c == ',') {
					char* fb = c + 1;
					size_t fl;
					while (*fb == ' ' || *fb == '\t')
						fb++;
					fl = (size_t)((end - 1) - fb);
					if (fl && fl <= (size_t)(end - r)) {
						memmove(w, fb, fl);
						w += fl;
						r = end;
						continue;
					}
				}
				if (end && rep && rl <= (size_t)(end - r)) {
					memmove(w, rep, rl);
					w += rl;
					r = end;
					continue;
				}
			}
		}
		*w++ = *r++;
	}
	*w = 0;
}

/* libcss has no light-dark(). Keep the first color. */
static void NsCssFixLightDark(void) {
	char* r = s_cssBuf;
	char* w = s_cssBuf;
	while (*r) {
		if (!strncmp(r, "light-dark(", 11)) {
			char* comma = strchr(r + 11, ',');
			char* endp = comma ? strchr(comma, ')') : NULL;
			if (comma && endp) {
				size_t n = (size_t)(comma - (r + 11));
				memmove(w, r + 11, n);
				w += n;
				r = endp + 1;
				continue;
			}
		}
		*w++ = *r++;
	}
	*w = 0;
	NsCssResolveVars();
	NsCssResolveVars();
}

/* Collect <style> bodies before the render pass. */
static void NsCollectCss(const char* src, size_t srclen) {
	size_t di = 0;
	size_t i = 0;
	int in_noscript = 0;
	s_cssBuf[0] = 0;
	while (i < srclen && di + 1 < sizeof(s_cssBuf)) {
		if (src[i] != '<') {
			i++;
			continue;
		}
		size_t j = i + 1;
		/* "noscript" is 8 letters. A 7-character cap stored "noscrip",
		 * missed the element, and kept its div{display:none} rule. */
		char tag[16];
		int tl = 0;
		while (j < srclen && tl < 15 &&
			   (isalpha((unsigned char)src[j]) || isdigit((unsigned char)src[j]))) {
			char tc = src[j];
			if (tc >= 'A' && tc <= 'Z')
				tc += 32;
			tag[tl++] = tc;
			j++;
		}
		tag[tl] = 0;
		if (src[i + 1] == '/') {
			char ctag[12];
			int cl = 0;
			size_t cj = i + 2;
			while (cj < srclen && cl < 11 &&
				   (isalpha((unsigned char)src[cj]) || isdigit((unsigned char)src[cj]))) {
				char tc = src[cj];
				if (tc >= 'A' && tc <= 'Z')
					tc += 32;
				ctag[cl++] = tc;
				cj++;
			}
			ctag[cl] = 0;
			if (!strcmp(ctag, "noscript") && in_noscript > 0)
				in_noscript--;
			i = cj;
			continue;
		}
		if (!strcmp(tag, "noscript")) {
			in_noscript++;
			i = j;
			continue;
		}
		/* The search interstitial hides div/p from inside <noscript>. */
		if (strcmp(tag, "style") != 0 || in_noscript) {
			i++;
			continue;
		}
		/* Quote-aware end of the open tag. */
		bool q = false;
		char qc = 0;
		while (j < srclen && (src[j] != '>' || q)) {
			if (!q && (src[j] == '"' || src[j] == '\'')) {
				q = true;
				qc = src[j];
			} else if (q && src[j] == qc) {
				q = false;
			}
			j++;
		}
		if (j < srclen)
			j++;
		/* Copy until </style (case-insensitive). */
		while (j < srclen && di + 1 < sizeof(s_cssBuf)) {
			if (src[j] == '<' && j + 7 < srclen && src[j + 1] == '/' &&
				(src[j + 2] == 's' || src[j + 2] == 'S') &&
				(src[j + 3] == 't' || src[j + 3] == 'T') &&
				(src[j + 4] == 'y' || src[j + 4] == 'Y') &&
				(src[j + 5] == 'l' || src[j + 5] == 'L') &&
				(src[j + 6] == 'e' || src[j + 6] == 'E')) {
				while (j < srclen && src[j] != '>')
					j++;
				if (j < srclen)
					j++;
				break;
			}
			s_cssBuf[di++] = src[j++];
		}
		s_cssBuf[di] = 0;
		i = j;
	}
	s_cssBuf[di] = 0;
	NsCssFixLightDark();
}

/* Case-insensitive substring test (rel="stylesheet"). */
static bool NsContainsCI(const char* h, const char* needle) {
	size_t n = strlen(needle);
	if (!n)
		return true;
	if (!h)
		return false;
	for (; *h; h++) {
		size_t i;
		for (i = 0; i < n; i++) {
			char a = h[i];
			char b = needle[i];
			if (a >= 'A' && a <= 'Z')
				a += 32;
			if (b >= 'A' && b <= 'Z')
				b += 32;
			if (a != b)
				break;
		}
		if (i == n)
			return true;
	}
	return false;
}

/* Resolve a stylesheet href against the page URL. */
/* Xeneva's snprintf has no %.*s support, so prefixes are copied by hand. */
static void NsUrlPrefix(char* dst, size_t dstsz, const char* a, size_t alen,
						const char* b, size_t blen) {
	if (!dst || dstsz == 0)
		return;
	if (alen >= dstsz)
		alen = dstsz - 1;
	memcpy(dst, (void*)a, alen);
	dst[alen] = 0;
	if (b && blen) {
		size_t room = dstsz - alen - 1;
		if (blen > room)
			blen = room;
		memcpy(dst + alen, (void*)b, blen);
		dst[alen + blen] = 0;
	}
}

static void NsResolveUrl(const char* base, const char* href, char* out, size_t outsz) {
	char clean[NS_URL_MAX];
	char prefix[NS_URL_MAX];
	char* q;
	char* f;
	const char* scheme;
	const char* host;
	const char* slash;
	size_t origin_len;
	if (!out || outsz == 0)
		return;
	out[0] = 0;
	if (!href || !href[0])
		return;
	if (!strncmp(href, "http://", 7) || !strncmp(href, "https://", 8)) {
		snprintf(out, outsz, "%s", href);
		return;
	}
	if (!strncmp(href, "//", 2)) {
		scheme = strstr(base, "://");
		if (scheme) {
			NsUrlPrefix(prefix, sizeof(prefix), base, (size_t)(scheme - base), NULL, 0);
			snprintf(out, outsz, "%s:%s", prefix, href);
		} else {
			snprintf(out, outsz, "https:%s", href);
		}
		return;
	}
	snprintf(clean, sizeof(clean), "%s", base);
	q = strchr(clean, '?');
	if (q)
		*q = 0;
	f = strchr(clean, '#');
	if (f)
		*f = 0;
	scheme = strstr(clean, "://");
	host = scheme ? scheme + 3 : clean;
	slash = strchr(host, '/');
	origin_len = slash ? (size_t)(slash - clean) : strlen(clean);
	if (href[0] == '/') {
		NsUrlPrefix(prefix, sizeof(prefix), clean, origin_len, NULL, 0);
		snprintf(out, outsz, "%s%s", prefix, href);
		return;
	}
	if (slash) {
		const char* last = strrchr(slash, '/');
		size_t dir_len = last ? (size_t)(last - slash) + 1 : 1;
		NsUrlPrefix(prefix, sizeof(prefix), clean, origin_len, slash, dir_len);
		snprintf(out, outsz, "%s%s", prefix, href);
	} else {
		NsUrlPrefix(prefix, sizeof(prefix), clean, origin_len, NULL, 0);
		snprintf(out, outsz, "%s/%s", prefix, href);
	}
}

static int NsCssAppendFetched(xe_http_response* resp) {
	size_t at;
	size_t n;
	size_t room;
	const char* css;
	if (!resp || !resp->body || !resp->body_len)
		return 0;
	at = strlen(s_cssBuf);
	css = resp->body;
	n = resp->body_len;
	room = sizeof(s_cssBuf) - at - 2;
	if (n >= 3 && (unsigned char)css[0] == 0xEF &&
		(unsigned char)css[1] == 0xBB && (unsigned char)css[2] == 0xBF) {
		css += 3;
		n -= 3;
	}
	/* A cut sheet fails data_done and the author CSS is dropped. Copy only
	 * what still fits, and keep the bytes that did fit. */
	if (n > room)
		n = room;
	if (!n)
		return 0;
	memcpy(s_cssBuf + at, (void*)css, n);
	at += n;
	s_cssBuf[at++] = '\n';
	s_cssBuf[at] = 0;
	return 1;
}

/* Two stylesheets share one round trip window. A failed sheet does not
 * count, so the scan keeps going until two have been stored. */
static void NsCssFetchBatch(char* url0, char* url1, int n, int* fetched) {
	const char* urls[2];
	xe_http_response resp[2];
	size_t caps[2];
	int rcs[2];
	int k;
	if (!fetched || n <= 0)
		return;
	urls[0] = url0;
	if (n > 1)
		urls[1] = url1;
	for (k = 0; k < n; k++) {
		caps[k] = sizeof(s_cssBuf) - 2;
		memset(&resp[k], 0, sizeof(resp[k]));
	}
	if (xe_http_get_many(urls, n, kNsUa, resp, caps, rcs) != 0)
		return;
	for (k = 0; k < n; k++) {
		_KePrint("[css] link %s -> %d bytes rc=%d %s\n", urls[k],
				 (int)resp[k].body_len, rcs[k], resp[k].error);
		if (*fetched < 2 && rcs[k] == 0 && NsCssAppendFetched(&resp[k]))
			(*fetched)++;
		xe_http_response_free(&resp[k]);
	}
}

/* Fetch <link rel=stylesheet> targets and append them to the inline CSS.
 * libcss gets one author sheet, so concatenation is deliberate. */
static void NsCollectCssLinks(const char* src, size_t srclen, const char* base) {
	int fetched = 0;
	size_t i = 0;
	char batch[2][NS_URL_MAX * 2];
	int nbatch = 0;
	while (i + 7 < srclen && fetched < 2) {
		char href[512];
		char rel[64];
		char media[64];
		char abs[NS_URL_MAX * 2];
		size_t j;
		size_t k;
		bool quote;
		char quote_c;
		if (src[i] != '<' ||
			tolower((unsigned char)src[i + 1]) != 'l' ||
			tolower((unsigned char)src[i + 2]) != 'i' ||
			tolower((unsigned char)src[i + 3]) != 'n' ||
			tolower((unsigned char)src[i + 4]) != 'k') {
			i++;
			continue;
		}
		j = i + 5;
		if (j < srclen && src[j] != ' ' && src[j] != '\t' &&
			src[j] != '\r' && src[j] != '\n' && src[j] != '/' && src[j] != '>') {
			i++;
			continue;
		}
		k = j;
		quote = false;
		quote_c = 0;
		while (k < srclen && (src[k] != '>' || quote)) {
			if (!quote && (src[k] == '"' || src[k] == '\'')) {
				quote = true;
				quote_c = src[k];
			} else if (quote && src[k] == quote_c) {
				quote = false;
			}
			k++;
		}
		if (k >= srclen)
			break;
		href[0] = rel[0] = media[0] = 0;
		if (!NsTagAttr(src, j, k, "href", href, sizeof(href)) ||
			!NsTagAttr(src, j, k, "rel", rel, sizeof(rel)) ||
			!NsContainsCI(rel, "stylesheet") ||
			!href[0] || !strncmp(href, "data:", 5) ||
			!strncmp(href, "javascript:", 11) ||
			(strlen(s_cssBuf) + 2048 >= sizeof(s_cssBuf))) {
			i = k + 1;
			continue;
		}
		if (NsTagAttr(src, j, k, "media", media, sizeof(media)) &&
			NsContainsCI(media, "print") && !NsContainsCI(media, "screen") &&
			!NsContainsCI(media, "all")) {
			i = k + 1;
			continue;
		}
		NsResolveUrl(base, href, abs, sizeof(abs));
		if ((!strncmp(abs, "http://", 7) || !strncmp(abs, "https://", 8)) && nbatch < 2) {
			snprintf(batch[nbatch], sizeof(batch[nbatch]), "%s", abs);
			nbatch++;
			if (nbatch == 2) {
				NsCssFetchBatch(batch[0], batch[1], nbatch, &fetched);
				nbatch = 0;
			}
		}
		i = k + 1;
	}
	if (nbatch && fetched < 2)
		NsCssFetchBatch(batch[0], batch[1], nbatch, &fetched);
	if (fetched)
		NsCssFixLightDark();
}

static void NsHtmlToText(const char* src, size_t srclen, char* dst, size_t dstsz) {
	size_t di = 0;
	size_t i = 0;
	int skip = 0; /* 1 = inside <script>, 2 = inside <style>, 3 = <noscript> */
	g_pageCenter = 0;
	g_pageBg = 0xffffffffu;
	if (!g_inWrite) {
		s_nlinks = 0;
		NsImgReset();
	}
	int link_slot = -1;
	size_t link_at = 0;
	char href[512];
	href[0] = 0;
	NsCssNode* stack[NS_CSS_STACK];
	uint8_t skipped[NS_CSS_STACK];
	size_t opened[NS_CSS_STACK];
	int depth = 0;
	int hide = 0;

	/* Mid-parse JS output (document.write/console) must land exactly at
	 * the parse position: publish the emission target for the sink.
	 * Fragment parses keep the outer target untouched. */
	if (!g_inWrite) {
		s_outLive = true;
		s_outDst = dst;
		s_outSz = dstsz;
		s_outDi = &di;
		g_colCur = s_col;
		g_bgCur = s_bg;
		g_szCur = s_sz;
		g_flCur = s_fl;
		g_colMaxCur = NS_BODY_MAX;
		g_linePx = 0;
		s_liNo = 0;
	}

	stack[0] = NsCssNodeCreate(NULL, "body");
	if (stack[0]) {
		NsCssSelect(stack[0]);
		depth = 1;
	}
	NsRunFromTop(stack, depth);

	while (i < srclen && di + 1 < dstsz) {
		char c = src[i];
		if (c == '<') {
			/* Comments first: they may contain '>' inside. */
			if (i + 3 < srclen && src[i + 1] == '!' && src[i + 2] == '-' && src[i + 3] == '-') {
				i += 4;
				while (i + 2 < srclen && !(src[i] == '-' && src[i + 1] == '-' && src[i + 2] == '>'))
					i++;
				i += 3;
				continue;
			}
			/* Other declarations (<!DOCTYPE ...>): not elements,
             * skip to '>' without creating style nodes. */
			if (i + 1 < srclen && src[i + 1] == '!') {
				size_t j = i + 2;
				while (j < srclen && src[j] != '>')
					j++;
				i = (j < srclen) ? j + 1 : srclen;
				continue;
			}
			if (skip) {
				/* Only the matching close tag ends the skip: any other
                 * '<' inside (e.g. JS `a < b`) is content, and parsing it
                 * as a tag would swallow the real </script> terminator. */
				const char* want = (skip == 1) ? "script" : "style";
				size_t j = i + 1;
				bool isClose = (j < srclen && src[j] == '/');
				if (isClose)
					j++;
				size_t n = strlen(want);
				bool match = isClose;
				for (size_t t = 0; match && t < n; t++) {
					if (j + t >= srclen || tolower((unsigned char)src[j + t]) != want[t])
						match = false;
				}
				if (!match) {
					if (skip == 1 && !g_inWrite)
						NsJsScriptChunk(src[i]);
					i++;
					continue;
				}
			}
			size_t j = i + 1;
			bool closing = false;
			if (j < srclen && src[j] == '/') {
				closing = true;
				j++;
			}
			char tag[16];
			int tl = 0;
			while (j < srclen && tl < 15 &&
				   (isalpha((unsigned char)src[j]) || isdigit((unsigned char)src[j]))) {
				tag[tl++] = (char)tolower((unsigned char)src[j]);
				j++;
			}
			tag[tl] = 0;
			/* Tag extent, quote-aware, for href capture. */
			bool q = false;
			char qc = 0;
			size_t k = j;
			while (k < srclen && (src[k] != '>' || q)) {
				if (!q && (src[k] == '"' || src[k] == '\'')) {
					q = true;
					qc = src[k];
				} else if (q && src[k] == qc) {
					q = false;
				}
				k++;
			}
			if (skip) {
				if (closing && ((skip == 1 && !strcmp(tag, "script")) ||
								(skip == 2 && !strcmp(tag, "style")))) {
					/* End of an inline <script>: hand the accumulated
					 * body to the engine. Fragments (document.write
					 * output) never run their own <script> tags. */
					if (skip == 1 && !g_inWrite)
						NsJsRunPending();
					skip = 0;
				}
				i = (k < srclen) ? k + 1 : srclen;
				continue;
			}
			if (!closing && (!strcmp(tag, "script") || !strcmp(tag, "style"))) {
				if (!strcmp(tag, "script")) {
					/* External src (no loader): noted when the script
					 * element closes with an empty body. Fragment
					 * scripts are skipped wholesale (never run). */
					if (!g_inWrite) {
						char srcAttr[128];
						char typeAttr[64];
						if (NsTagAttr(src, j, k, "src", srcAttr, sizeof(srcAttr)))
							NsJsScriptSrcAttr(srcAttr);
						else
							NsJsScriptSrcAttr(NULL);
						if (NsTagAttr(src, j, k, "type", typeAttr, sizeof(typeAttr)))
							NsJsScriptType(typeAttr);
						else
							NsJsScriptType(NULL);
					}
				}
				skip = !strcmp(tag, "script") ? 1 : 2;
				i = (k < srclen) ? k + 1 : srclen;
				continue;
			}
			bool isH = (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && tag[2] == 0);
			if (!closing) {
				/* href first: the <a> node needs the link flag at creation. */
				if (!strcmp(tag, "a")) {
					href[0] = 0;
					for (size_t h = j; h < k; h++) {
						if ((h + 4 < k) && (src[h] == 'h' || src[h] == 'H') &&
							(src[h + 1] == 'r' || src[h + 1] == 'R') &&
							(src[h + 2] == 'e' || src[h + 2] == 'E') &&
							(src[h + 3] == 'f' || src[h + 3] == 'F')) {
							size_t v = h + 4;
							while (v < k && (src[v] == ' ' || src[v] == '\t' || src[v] == '='))
								v++;
							while (v < k && (src[v] == ' ' || src[v] == '\t'))
								v++;
							if (v < k && (src[v] == '"' || src[v] == '\'')) {
								char hq = src[v++];
								size_t hl = 0;
								while (v < k && src[v] != hq && hl + 1 < sizeof(href))
									href[hl++] = src[v++];
								href[hl] = 0;
								{
									/* href is not passed through NsEntity. */
									char* a = href;
									char* b = href;
									while (*a) {
										if (!strncmp(a, "&amp;", 5)) {
											*b++ = '&';
											a += 5;
										} else if (!strncmp(a, "&quot;", 6)) {
											*b++ = '"';
											a += 6;
										} else if (!strncmp(a, "&#39;", 5) ||
												   !strncmp(a, "&apos;", 6)) {
											*b++ = '\'';
											a += (*a == '&' && a[1] == '#') ? 5 : 6;
										} else {
											*b++ = *a++;
										}
									}
									*b = 0;
								}
							}
							break;
						}
					}
				}
				/* Push the element, select its style, adopt the run.
                 * Empty tag names select nothing. */
				if (tag[0] != 0 && depth < NS_CSS_STACK) {
					NsCssNode* el = NsCssNodeCreate(depth > 0 ? stack[depth - 1] : NULL, tag);
					if (el) {
						char attr[1024];
						if (NsTagAttr(src, j, k, "class", attr, sizeof(attr))) {
							char* tok = attr;
							while (*tok && el->n_cls < NS_CSS_MAX_CLASSES) {
								while (*tok == ' ' || *tok == '\t')
									tok++;
								if (!*tok)
									break;
								char* end = tok;
								while (*end && *end != ' ' && *end != '\t')
									end++;
								char save = *end;
								*end = 0;
								NsCssNodeAddClass(el, tok);
								*end = save;
								tok = end;
							}
						}
						if (NsTagAttr(src, j, k, "id", attr, sizeof(attr)))
							NsCssNodeSetId(el, attr);
						if (!strcmp(tag, "a") && href[0]) {
							NsCssNodeSetLink(el, true);
							if (!g_inWrite && s_nlinks < NS_LINK_MAX) {
								link_slot = s_nlinks;
								link_at = di;
								snprintf(s_links[link_slot].href, sizeof(s_links[link_slot].href), "%s", href);
							}
							s_latinLink = NULL;
							s_latinUsed = 0;
							{
								const char* hl = strstr(href, "hl=");
								static const struct {
									const char* id;
									const char* name;
								} kHl[] = {{"hi", "Hindi"},		{"bn", "Bengali"},
										   {"te", "Telugu"},		{"mr", "Marathi"},
										   {"ta", "Tamil"},		{"gu", "Gujarati"},
										   {"kn", "Kannada"},		{"ml", "Malayalam"},
										   {"pa", "Punjabi"},		{"ur", "Urdu"},
										   {"ar", "Arabic"},		{"ru", "Russian"},
										   {"ja", "Japanese"},	{"ko", "Korean"},
										   {"zh-CN", "Chinese"},	{"zh-TW", "Chinese"},
										   {"de", "German"},		{"fr", "French"},
										   {"es", "Spanish"},		{"pt-BR", "Portuguese"},
										   {"pt", "Portuguese"},	{"it", "Italian"},
										   {"nl", "Dutch"},		{"pl", "Polish"},
										   {"en", "English"},		{NULL, NULL}};
								if (hl) {
									hl += 3;
									for (int hi = 0; kHl[hi].id; hi++) {
										size_t n = strlen(kHl[hi].id);
										if (!strncmp(hl, kHl[hi].id, n) &&
											(hl[n] == '&' || hl[n] == 0 || hl[n] == '#')) {
											s_latinLink = kHl[hi].name;
											break;
										}
									}
								}
							}
						}
						if (NsTagAttr(src, j, k, "style", attr, sizeof(attr)))
							NsCssNodeSetInline(el, attr);
						NsCssSelect(el);
						skipped[depth] = NsCssNodeSkip(el) ? 1 : 0;
						/* link/meta/img never have an end tag. Counting them
						 * as an open display:none region hid the rest of
						 * example.com and the Google form. */
						{
							int void_el = !strcmp(tag, "link") || !strcmp(tag, "meta") ||
										  !strcmp(tag, "img") || !strcmp(tag, "br") ||
										  !strcmp(tag, "hr") || !strcmp(tag, "input") ||
										  !strcmp(tag, "source") || !strcmp(tag, "base") ||
										  !strcmp(tag, "area") || !strcmp(tag, "col") ||
										  !strcmp(tag, "embed") || !strcmp(tag, "wbr");
							if (skipped[depth] && !void_el)
								hide++;
							if (void_el) {
								NsCssNodeDestroy(el);
								el = NULL;
							} else {
								opened[depth] = di;
								stack[depth++] = el;
								NsRunFromTop(stack, depth);
							}
						}
					}
				}
				if (!strcmp(tag, "img") && !g_inWrite) {
					char isrc[512];
					char alt[128];
					int ix;
					isrc[0] = alt[0] = 0;
					NsTagAttr(src, j, k, "src", isrc, sizeof(isrc));
					NsTagAttr(src, j, k, "alt", alt, sizeof(alt));
					ix = NsImgQueue(isrc);
					if (ix >= 0) {
						char mark[3];
						NsEmitNewlines(dst, dstsz, &di, 1);
						mark[0] = '\x01';
						mark[1] = (char)('0' + ix);
						mark[2] = 0;
						NsEmitStr(dst, dstsz, &di, mark);
						NsEmitNewlines(dst, dstsz, &di, 1);
					}
					if (alt[0]) {
						NsEmitStr(dst, dstsz, &di, alt);
						NsEmitChar(dst, dstsz, &di, ' ');
					}
				}
				if (!strcmp(tag, "video") || !strcmp(tag, "source")) {
					char vsrc[256];
					vsrc[0] = 0;
					NsTagAttr(src, j, k, "src", vsrc, sizeof(vsrc));
					NsEmitStr(dst, dstsz, &di, "[video");
					if (vsrc[0]) {
						NsEmitStr(dst, dstsz, &di, " ");
						NsEmitStr(dst, dstsz, &di, vsrc);
					}
					NsEmitStr(dst, dstsz, &di, "]\n");
				}
				if (hide > 0) {
					/* display:none / visibility:hidden: keep the stack,
					 * drop the text. */
					i = (k < srclen) ? k + 1 : srclen;
					continue;
				}
				if (!strcmp(tag, "input") || !strcmp(tag, "textarea")) {
					char typ[16];
					char val[80];
					char name[64];
					typ[0] = val[0] = name[0] = 0;
					NsTagAttr(src, j, k, "type", typ, sizeof(typ));
					NsTagAttr(src, j, k, "value", val, sizeof(val));
					NsTagAttr(src, j, k, "name", name, sizeof(name));
					if (!strcmp(typ, "hidden")) {
						/* not a control the reader can use */
					} else if (val[0] && (!strcmp(typ, "submit") || !strcmp(typ, "button"))) {
						if (di == 0 || dst[di - 1] == '\n')
							NsEmitNewlines(dst, dstsz, &di, 1);
						else
							NsEmitChar(dst, dstsz, &di, ' ');
						NsEmitStr(dst, dstsz, &di, "[ ");
						NsEmitStr(dst, dstsz, &di, val);
						NsEmitStr(dst, dstsz, &di, " ]");
					} else if (!strcmp(name, "q") || !strcmp(typ, "search") ||
							   !strcmp(tag, "textarea")) {
						NsEmitNewlines(dst, dstsz, &di, 1);
						NsEmitStr(dst, dstsz, &di, "[ Search Google ]");
						NsEmitNewlines(dst, dstsz, &di, 1);
					}
				} else if (!strcmp(tag, "br")) {
					NsEmitNewlines(dst, dstsz, &di, 1);
				} else if (depth > 0 && NsCssNodeBlock(stack[depth - 1])) {
					NsEmitNewlines(dst, dstsz, &di, 1);
					if (!strcmp(tag, "ol") || !strcmp(tag, "ul"))
						s_liNo = 0;
					if (!strcmp(tag, "li")) {
						NsCssUsed u;
						char mark[12];
						NsCssNodeUsed(stack[depth - 1], &u);
						mark[0] = 0;
						if (u.list == 4) {
							s_liNo++;
							snprintf(mark, sizeof(mark), "%d. ", s_liNo);
						} else if (u.list == 5) {
							s_liNo++;
							mark[0] = (char)('a' + ((s_liNo - 1) % 26));
							mark[1] = '.';
							mark[2] = ' ';
							mark[3] = 0;
						} else if (u.list == 2) {
							snprintf(mark, sizeof(mark), "o ");
						} else if (u.list == 3) {
							snprintf(mark, sizeof(mark), "# ");
						} else if (u.list != 0) {
							snprintf(mark, sizeof(mark), "- ");
						}
						if (mark[0])
							NsEmitStr(dst, dstsz, &di, mark);
					}
				} else if (depth > 0 && di > 0 && dst[di - 1] != ' ' && dst[di - 1] != '\n') {
					/* Inline / inline-block / table-cell: keep the line,
					 * but don't glue "Gmail" to "Images". */
					NsEmitChar(dst, dstsz, &di, ' ');
				} else if (!strcmp(tag, "hr")) {
					NsEmitNewlines(dst, dstsz, &di, 1);
					NsEmitStr(dst, dstsz, &di, "---");
					NsEmitNewlines(dst, dstsz, &di, 1);
				}
			} else {
				/* Pop the element; author vertical margins add spacing. */
				int mlines = 0;
				int was_skip = 0;
				int was_block = 0;
				size_t opened_at = 0;
				if (depth > 1) {
					was_skip = skipped[depth - 1];
					was_block = NsCssNodeBlock(stack[depth - 1]);
					opened_at = opened[depth - 1];
					if (was_skip && hide > 0)
						hide--;
					mlines = NsCssNodeMarginLines(stack[depth - 1]);
					NsCssNodeDestroy(stack[depth - 1]);
					depth--;
					NsRunFromTop(stack, depth);
				}
				if (!strcmp(tag, "a")) {
					if (!g_inWrite && link_slot >= 0 && di > link_at) {
						s_links[link_slot].a = link_at;
						s_links[link_slot].b = di;
						s_links[link_slot].have = 0;
						s_nlinks++;
					}
					link_slot = -1;
					href[0] = 0;
					s_latinLink = NULL;
					s_latinUsed = 0;
				}
				if (was_skip || hide > 0) {
					i = (k < srclen) ? k + 1 : srclen;
					continue;
				}
				/* An empty wrapper (a div around a div) must not add a blank line. */
				if (was_block && di > opened_at)
					NsEmitNewlines(dst, dstsz, &di, 1);
				for (int t = 0; t < mlines; t++)
					NsEmitChar(dst, dstsz, &di, '\n');
			}
			i = (k < srclen) ? k + 1 : srclen;
			continue;
		}
		if (skip) {
			/* Inline script bodies are fed verbatim to the JS engine,
             * entity/raw char included: JS is not HTML text. */
			if (skip == 1 && !g_inWrite)
				NsJsScriptChunk(c);
			i++;
			continue;
		}
		if (hide > 0) {
			i++;
			continue;
		}
		if ((unsigned char)c >= 0x80) {
			/* UTF-8 text the Latin font cannot draw. A language link
			 * (hl=hi and the rest) is shown by its English name. */
			if (s_latinLink && !s_latinUsed) {
				NsEmitStr(dst, dstsz, &di, s_latinLink);
				s_latinUsed = 1;
			}
			i++;
			continue;
		}
		if (c == '&') {
			int used = NsEntity(src, srclen, i, dst, dstsz, &di);
			if (used > 0) {
				i += (size_t)used;
				continue;
			}
		}
		if (c == '\r') {
			i++;
			continue;
		}
		if (c == '\t') {
			if (g_runPre)
				NsEmitChar(dst, dstsz, &di, ' ');
			i++;
			continue;
		}
		if (c == '\n') {
			if (g_runPre)
				NsEmitChar(dst, dstsz, &di, '\n');
			else if (di > 0 && dst[di - 1] != ' ' && dst[di - 1] != '\n')
				NsEmitChar(dst, dstsz, &di, ' ');
			i++;
			continue;
		}
		NsEmitChar(dst, dstsz, &di, c);
		i++;
	}
	while (depth > 0)
		NsCssNodeDestroy(stack[--depth]);
	dst[di] = 0;
	if (!g_inWrite) {
		s_outLive = false;
		s_outDi = NULL;
		NsImgFetchAll();
	}
}

/* Emit one raw output chunk (already text) into the current target:
 * mid-parse -> the live parse position; post-parse -> page text end. */
static void NsSinkRaw(const char* s, size_t n, int kind) {
	if (!s || !n || (!s_outLive && !s_postPage))
		return;
	/* Console kinds restyle; save/restore so following text keeps its run. */
	uint32_t sc = g_runColor;
	uint8_t ss = g_runSize;
	if (kind == NS_JS_KIND_LOG)
		g_runColor = NS_JS_COL_LOG;
	else if (kind == NS_JS_KIND_WARN)
		g_runColor = NS_JS_COL_WARN;
	else if (kind == NS_JS_KIND_ERR)
		g_runColor = NS_JS_COL_ERR;
	if (kind != NS_JS_KIND_WRITE)
		g_runSize = 14;

	if (s_outLive) {
		/* Mid-parse: the HTML pass publishes its output target and the
		 * live di; style indirections already point at the page arrays. */
		char* dst = s_outDst;
		size_t dstsz = s_outSz;
		size_t* diP = s_outDi;
		while (n--) {
			char c = *s++;
			if (c == '\n')
				NsEmitNewlines(dst, dstsz, diP, 1);
			else
				NsEmitChar(dst, dstsz, diP, c);
		}
		dst[*diP] = 0;
	} else {
		/* Post-parse (timers): append at the page text end, uniform style
		 * for the whole chunk. */
		char* dst = s_text;
		size_t dstsz = sizeof(s_text);
		size_t di = strlen(dst);
		uint32_t* savedCol = g_colCur;
		uint32_t* savedBg = g_bgCur;
		uint8_t* savedSz = g_szCur;
		uint8_t* savedFl = g_flCur;
		size_t savedMax = g_colMaxCur;
		g_colCur = s_col;
		g_bgCur = s_bg;
		g_szCur = s_sz;
		g_flCur = s_fl;
		g_colMaxCur = NS_BODY_MAX;
		while (n--) {
			char c = *s++;
			if (c == '\n')
				NsEmitNewlines(dst, dstsz, &di, 1);
			else
				NsEmitChar(dst, dstsz, &di, c);
		}
		dst[di] = 0;
		g_colCur = savedCol;
		g_bgCur = savedBg;
		g_szCur = savedSz;
		g_flCur = savedFl;
		g_colMaxCur = savedMax;
	}
	g_runColor = sc;
	g_runSize = ss;
}

/* document.write output carries HTML tags: browsers parse it inline. The
 * reader replays the fragment through the same HTML pass (own parse
 * position and style scratch), then emits the converted text. Written
 * <script> tags are skipped, not run. */
static void NsJsSink(const char* s, size_t n, int kind) {
	if (!s || !n)
		return;
	if (kind != NS_JS_KIND_WRITE) {
		NsSinkRaw(s, n, kind);
		return;
	}
	if (g_writeDepth >= 3) { /* runaway fragment chains: degrade to text */
		NsSinkRaw(s, n, kind);
		return;
	}
	int fidx = g_writeDepth;
	size_t cn = n < NS_FRAG_MAX ? n : NS_FRAG_MAX;
	memcpy(s_fragSrc[fidx], (void*)s, cn);
	s_fragSrc[fidx][cn] = 0;
	/* The fragment parse must not disturb the outer parse's sink target
	 * or run style: guarded, saved, restored. */
	bool savedLive = s_outLive;
	char* savedDst = s_outDst;
	size_t savedSz = s_outSz;
	size_t* savedDi = s_outDi;
	uint32_t* savedCol = g_colCur;
	uint32_t* savedBg = g_bgCur;
	uint8_t* savedSzArr = g_szCur;
	uint8_t* savedFl = g_flCur;
	size_t savedMax = g_colMaxCur;
	uint32_t savedRunC = g_runColor;
	uint32_t savedRunB = g_runBg;
	uint8_t savedRunS = g_runSize;
	uint8_t savedDecor = g_runDecor;
	uint8_t savedAlign = g_runAlign;
	uint8_t savedIndent = g_runIndent;
	uint8_t savedLine = g_runLine;
	uint8_t savedCase = g_runCase;
	uint8_t savedPre = g_runPre;
	uint8_t savedNowrap = g_runNowrap;
	uint16_t savedWidth = g_runWidth;
	uint32_t savedBorder = g_runBorder;
	bool savedInWrite = g_inWrite;
	g_colCur = s_fragJunkCol;
	g_bgCur = s_fragJunkBg;
	g_szCur = s_fragJunkSz;
	g_flCur = s_fragJunkFl;
	g_colMaxCur = NS_FRAG_MAX;
	g_inWrite = true;
	g_writeDepth++;
	NsHtmlToText(s_fragSrc[fidx], cn, s_fragOut[fidx], NS_FRAG_MAX);
	g_writeDepth--;
	g_inWrite = savedInWrite;
	s_outLive = savedLive;
	s_outDst = savedDst;
	s_outSz = savedSz;
	s_outDi = savedDi;
	g_colCur = savedCol;
	g_bgCur = savedBg;
	g_szCur = savedSzArr;
	g_flCur = savedFl;
	g_colMaxCur = savedMax;
	g_runColor = savedRunC;
	g_runBg = savedRunB;
	g_runSize = savedRunS;
	g_runDecor = savedDecor;
	g_runAlign = savedAlign;
	g_runIndent = savedIndent;
	g_runLine = savedLine;
	g_runCase = savedCase;
	g_runPre = savedPre;
	g_runNowrap = savedNowrap;
	g_runWidth = savedWidth;
	g_runBorder = savedBorder;
	NsSinkRaw(s_fragOut[fidx], strlen(s_fragOut[fidx]), NS_JS_KIND_WRITE);
}

/* bing.com/search HTML is a script page or a human-check. format=rss is the
 * same query as a short item list, which this layout can paint. */
static int ns_ci_eq(const char* a, const char* b, size_t n) {
	size_t i;
	for (i = 0; i < n; i++) {
		unsigned char ca = (unsigned char)a[i];
		unsigned char cb = (unsigned char)b[i];
		if (ca >= 'A' && ca <= 'Z')
			ca = (unsigned char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z')
			cb = (unsigned char)(cb - 'A' + 'a');
		if (ca != cb)
			return 0;
	}
	return 1;
}

static const char* ns_find_ci(const char* s, const char* end, const char* lit) {
	size_t n = strlen(lit);
	if (n == 0)
		return s <= end ? s : NULL;
	while (s + n <= end) {
		if (ns_ci_eq(s, lit, n))
			return s;
		s++;
	}
	return NULL;
}

static int ns_hex_digit(int c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int ns_utf8_put(unsigned cp, char* d, size_t cap) {
	if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		return 0;
	if (cp < 0x80) {
		if (cap < 1)
			return 0;
		d[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		if (cap < 2)
			return 0;
		d[0] = (char)(0xC0 | (cp >> 6));
		d[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		if (cap < 3)
			return 0;
		d[0] = (char)(0xE0 | (cp >> 12));
		d[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		d[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	if (cap < 4)
		return 0;
	d[0] = (char)(0xF0 | (cp >> 18));
	d[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	d[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	d[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

static size_t ns_xml_decode(const char* s, const char* end, char* out, size_t cap) {
	size_t o = 0;
	if (cap == 0)
		return 0;
	while (s < end && o + 1 < cap) {
		unsigned cp = 0;
		int ok = 0;
		const char* semi;
		const char* ent;
		char tmp[4];
		int nb;
		if (*s != '&') {
			out[o++] = *s++;
			continue;
		}
		ent = s + 1;
		semi = ent;
		while (semi < end && *semi != ';' && (size_t)(semi - ent) < 12)
			semi++;
		if (semi >= end || *semi != ';') {
			out[o++] = *s++;
			continue;
		}
		if ((size_t)(semi - ent) == 3 && ns_ci_eq(ent, "amp", 3)) {
			cp = '&';
			ok = 1;
		} else if ((size_t)(semi - ent) == 2 && ns_ci_eq(ent, "lt", 2)) {
			cp = '<';
			ok = 1;
		} else if ((size_t)(semi - ent) == 2 && ns_ci_eq(ent, "gt", 2)) {
			cp = '>';
			ok = 1;
		} else if ((size_t)(semi - ent) == 4 && ns_ci_eq(ent, "quot", 4)) {
			cp = '"';
			ok = 1;
		} else if ((size_t)(semi - ent) == 4 && ns_ci_eq(ent, "apos", 4)) {
			cp = '\'';
			ok = 1;
		} else if (ent < semi && *ent == '#') {
			const char* n = ent + 1;
			int hex = 0;
			if (n < semi && (*n == 'x' || *n == 'X')) {
				hex = 1;
				n++;
			}
			ok = n < semi;
			while (ok && n < semi) {
				int d = hex ? ns_hex_digit((unsigned char)*n)
							: (*n >= '0' && *n <= '9' ? *n - '0' : -1);
				if (d < 0 || (hex ? (cp > (0x10FFFFu >> 4)) : (cp > 0x10FFFFu / 10u))) {
					ok = 0;
					break;
				}
				cp = hex ? (cp << 4) + (unsigned)d : cp * 10u + (unsigned)d;
				n++;
			}
			if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
				ok = 0;
		}
		if (!ok) {
			out[o++] = *s++;
			continue;
		}
		nb = ns_utf8_put(cp, tmp, sizeof(tmp));
		if (nb <= 0 || o + (size_t)nb >= cap)
			break;
		memcpy(out + o, tmp, (size_t)nb);
		o += (size_t)nb;
		s = semi + 1;
	}
	out[o] = 0;
	return o;
}

static void ns_trim(char* s) {
	char* start = s;
	size_t n;
	while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')
		start++;
	if (start != s)
		memmove(s, start, strlen(start) + 1);
	n = strlen(s);
	while (n > 0) {
		char c = s[n - 1];
		if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
			break;
		s[--n] = 0;
	}
}

static int ns_xml_field(const char* s, const char* end, const char* tag, char* out, size_t cap) {
	char open[24];
	char close[24];
	size_t tlen = strlen(tag);
	const char* p;
	const char* gt;
	const char* stop;
	if (cap == 0 || tlen + 3 >= sizeof(open))
		return -1;
	out[0] = 0;
	open[0] = '<';
	memcpy(open + 1, (void*)tag, tlen);
	open[tlen + 1] = 0;
	close[0] = '<';
	close[1] = '/';
	memcpy(close + 2, (void*)tag, tlen);
	close[tlen + 2] = '>';
	close[tlen + 3] = 0;
	p = ns_find_ci(s, end, open);
	if (!p)
		return -1;
	p += tlen + 1;
	gt = p;
	while (gt < end && *gt != '>')
		gt++;
	if (gt >= end)
		return -1;
	if (gt > s && *(gt - 1) == '/') {
		out[0] = 0;
		return 0;
	}
	p = gt + 1;
	stop = ns_find_ci(p, end, close);
	if (!stop)
		return -1;
	if ((size_t)(stop - p) >= 9 && ns_ci_eq(p, "<![CDATA[", 9)) {
		const char* c0 = p + 9;
		const char* c1 = ns_find_ci(c0, stop, "]]>");
		size_t n;
		if (!c1)
			c1 = stop;
		n = (size_t)(c1 - c0);
		if (n >= cap)
			n = cap - 1;
		memcpy(out, (void*)c0, n);
		out[n] = 0;
	} else {
		ns_xml_decode(p, stop, out, cap);
	}
	ns_trim(out);
	return 0;
}

static int ns_query_has(const char* q, const char* end, const char* key) {
	size_t klen = strlen(key);
	const char* p = q;
	if (p < end && *p == '?')
		p++;
	while (p < end) {
		if ((size_t)(end - p) > klen && ns_ci_eq(p, key, klen) && p[klen] == '=')
			return 1;
		while (p < end && *p != '&')
			p++;
		if (p < end && *p == '&')
			p++;
	}
	return 0;
}

static int NsBingIsSearch(const char* url) {
	const char* p;
	const char* host;
	const char* hend;
	const char* path;
	const char* colon;
	size_t hlen;
	char tail;
	if (!url)
		return 0;
	if (!strncmp(url, "https://", 8))
		p = url + 8;
	else if (!strncmp(url, "http://", 7))
		p = url + 7;
	else
		return 0;
	host = p;
	while (*p && *p != '/' && *p != '?' && *p != '#')
		p++;
	hend = p;
	for (colon = host; colon < hend; colon++) {
		if (*colon == ':') {
			hend = colon;
			break;
		}
	}
	hlen = (size_t)(hend - host);
	if (hlen == 8 && ns_ci_eq(host, "bing.com", 8)) {
		/* host itself */
	} else if (hlen > 9 && host[hlen - 9] == '.' && ns_ci_eq(host + (hlen - 8), "bing.com", 8)) {
		/* subdomain */
	} else {
		return 0;
	}
	path = (*p == '/') ? p : "/";
	if (!ns_ci_eq(path, "/search", 7))
		return 0;
	tail = path[7];
	return tail == 0 || tail == '?' || tail == '#' || tail == '/';
}

/* Drop the fragment and ask Bing for the RSS result list. */
static int NsBingFeedUrl(const char* url, char* out, size_t outsz) {
	const char* hash = strchr(url, '#');
	const char* q = strchr(url, '?');
	size_t n = hash ? (size_t)(hash - url) : strlen(url);
	const char* end = url + n;
	const char* add;
	const char* lang;
	size_t extra;
	int has_fmt;
	int has_lang;
	if (q && q < end) {
		has_fmt = ns_query_has(q, end, "format");
		has_lang = ns_query_has(q, end, "setlang");
	} else {
		has_fmt = 0;
		has_lang = 0;
	}
	if (has_fmt) {
		if (n + 1 > outsz)
			return -1;
		memcpy(out, (void*)url, n);
		out[n] = 0;
		return 0;
	}
	add = q ? "&format=rss" : "?format=rss";
	lang = has_lang ? "" : "&setlang=en";
	extra = strlen(add) + strlen(lang);
	if (n + extra + 1 > outsz)
		return -1;
	memcpy(out, (void*)url, n);
	memcpy(out + n, (void*)add, strlen(add));
	memcpy(out + n + strlen(add), (void*)lang, strlen(lang) + 1);
	return 0;
}

struct ns_out {
	char* p;
	size_t n;
	size_t cap;
};

static int ns_out_add(struct ns_out* b, const void* s, size_t n) {
	if (!b || b->n + n >= b->cap)
		return -1;
	memcpy(b->p + b->n, (void*)s, n);
	b->n += n;
	b->p[b->n] = 0;
	return 0;
}

static int ns_out_str(struct ns_out* b, const char* s) {
	return ns_out_add(b, s, strlen(s));
}

static int ns_out_esc(struct ns_out* b, const char* s) {
	for (; *s; s++) {
		const char* rep = NULL;
		char one[2];
		unsigned char c = (unsigned char)*s;
		if (c == '&')
			rep = "&amp;";
		else if (c == '<')
			rep = "&lt;";
		else if (c == '>')
			rep = "&gt;";
		else if (c == '"')
			rep = "&quot;";
		else if (c < 32)
			rep = " ";
		if (rep) {
			if (ns_out_str(b, rep) < 0)
				return -1;
			continue;
		}
		one[0] = (char)c;
		one[1] = 0;
		if (ns_out_add(b, one, 1) < 0)
			return -1;
	}
	return 0;
}

static int ns_http_href(const char* s) {
	size_t i;
	if (ns_ci_eq(s, "https://", 8))
		s += 8;
	else if (ns_ci_eq(s, "http://", 7))
		s += 7;
	else
		return 0;
	if (!*s)
		return 0;
	for (i = 0; s[i]; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c <= 32 || c == '"' || c == '<' || c == '>' || c == '\\')
			return 0;
	}
	return 1;
}

/* Up to 10 hits: h1 plus div.hit (title link, url, snippet). Returns the count. */
static int NsBingRssToHtml(const char* xml, size_t len, char* out, size_t outsz) {
	const char* end;
	const char* item;
	struct ns_out b;
	char heading[240];
	int hits = 0;
	if (!xml || !out || outsz < 64)
		return 0;
	end = xml + len;
	if (!ns_find_ci(xml, end, "<rss") && !ns_find_ci(xml, end, "<item"))
		return 0;
	b.p = out;
	b.n = 0;
	b.cap = outsz;
	out[0] = 0;
	heading[0] = 0;
	{
		const char* first = ns_find_ci(xml, end, "<item");
		const char* head_end = first ? first : end;
		if (ns_xml_field(xml, head_end, "title", heading, sizeof(heading)) != 0 || !heading[0])
			snprintf(heading, sizeof(heading), "Search");
	}
	if (ns_out_str(&b, "<!DOCTYPE html><html><body><h1>") < 0)
		return 0;
	if (ns_out_esc(&b, heading) < 0)
		return 0;
	if (ns_out_str(&b, "</h1>") < 0)
		return 0;
	item = xml;
	while (hits < 10) {
		char title[240];
		char link[700];
		char snip[400];
		const char* open;
		const char* close;
		const char* inner;
		open = ns_find_ci(item, end, "<item");
		if (!open)
			break;
		inner = open;
		while (inner < end && *inner != '>')
			inner++;
		if (inner >= end)
			break;
		inner++;
		close = ns_find_ci(inner, end, "</item>");
		if (!close)
			break;
		title[0] = 0;
		link[0] = 0;
		snip[0] = 0;
		ns_xml_field(inner, close, "title", title, sizeof(title));
		ns_xml_field(inner, close, "link", link, sizeof(link));
		ns_xml_field(inner, close, "description", snip, sizeof(snip));
		item = close + 7;
		if (!title[0] || !ns_http_href(link))
			continue;
		if (ns_out_str(&b, "<div class=\"hit\"><a href=\"") < 0)
			break;
		if (ns_out_esc(&b, link) < 0)
			break;
		if (ns_out_str(&b, "\">") < 0)
			break;
		if (ns_out_esc(&b, title) < 0)
			break;
		if (ns_out_str(&b, "</a><div class=\"url\">") < 0)
			break;
		if (ns_out_esc(&b, link) < 0)
			break;
		if (ns_out_str(&b, "</div><div class=\"snip\">") < 0)
			break;
		if (ns_out_esc(&b, snip) < 0)
			break;
		if (ns_out_str(&b, "</div></div>") < 0)
			break;
		hits++;
	}
	ns_out_str(&b, "</body></html>");
	return hits;
}

/* A host with no scheme is fetched as https://. http:// stays plain HTTP. */
static void NsEnsureHttps(char* url, size_t n) {
	char tmp[NS_URL_MAX];
	if (!url || !url[0] || n < 10)
		return;
	if (!strncmp(url, "https://", 8) || !strncmp(url, "http://", 7))
		return;
	snprintf(tmp, sizeof(tmp), "https://%s", url);
	snprintf(url, n, "%s", tmp);
}

static void NsLogNav(const char* url) {
	if (url && strstr(url, "sg_ss="))
		_KePrint("[js] navigate %d sg=1\n", (int)strlen(url));
	else
		_KePrint("[js] navigate %s\n", url ? url : "");
}

/* The script's document URL has to be the host that sent the body.
 * A google.com request that lands on www.google.com otherwise stores
 * a host-only cookie the next request never sends. */
static void NsAdoptLanded(char* url) {
	char landed[3072];
	size_t n;
	if (!url)
		return;
	landed[0] = 0;
	xe_http_last_url(landed, sizeof(landed));
	n = strlen(landed);
	if (n == 0 || n >= NS_URL_MAX)
		return;
	if (strncmp(landed, "https://", 8) != 0 && strncmp(landed, "http://", 7) != 0)
		return;
	if (strcmp(landed, url) == 0)
		return;
	snprintf(url, NS_URL_MAX, "%s", landed);
	if (url != s_url)
		snprintf(s_url, sizeof(s_url), "%s", landed);
	s_urlCursor = strlen(s_url);
	NsShowUrl();
}

static int NsFetch(char* url) {
	char status[NS_URL_MAX + 64];
	char came_from[NS_URL_MAX];
	char navraw[NS_URL_MAX];
	char navabs[NS_URL_MAX];
	char fetch_url[NS_URL_MAX];
	xe_http_response resp;
	const char* body;
	size_t blen;
	int left_tls = 0;
	int used_tls = 0;
	int nav_hops = 0;
	int bing_feed = 0;

	snprintf(came_from, sizeof(came_from), "%s", s_docUrl);
again:
	s_capAnchorMiss = 0;
	s_capAnchorUrl[0] = 0;
	s_query[0] = 0;
	s_qCursor = 0;
	s_qFocus = 0;
	NsEnsureHttps(url, NS_URL_MAX);
	xe_http_set_referer(came_from);
	snprintf(s_docUrl, sizeof(s_docUrl), "%s", url);
	NsShowUrl();
	if (strstr(url, "sg_ss="))
		snprintf(status, sizeof(status), "Fetching ...");
	else
		snprintf(status, sizeof(status), "Fetching %s ...", url);
	NsShowStatus(status);

	bing_feed = 0;
	snprintf(fetch_url, sizeof(fetch_url), "%s", url);
	if (NsBingIsSearch(url) && NsBingFeedUrl(url, fetch_url, sizeof(fetch_url)) == 0) {
		bing_feed = 1;
		_KePrint("[search] bing feed\n");
	}

	memset(&resp, 0, sizeof(resp));
	int rc = xe_http_get(fetch_url, "GET", kNsUa, &resp, NS_BODY_MAX);
	if (rc != 0 && (!resp.body || !resp.body_len)) {
		NsShowStatus(resp.error[0] ? resp.error : "Fetch failed.");
		xe_http_response_free(&resp);
		return -1;
	}
	if (!resp.body) {
		NsShowStatus(resp.error[0] ? resp.error : "Empty response.");
		xe_http_response_free(&resp);
		return -1;
	}
	_KePrint("[css] fetched %d bytes tls=%d status=%d\n", (int)resp.body_len, resp.tls, resp.status);
	xe_heap_check("post-fetch");

	blen = resp.body_len;
	if (blen > NS_RAW_MAX)
		blen = NS_RAW_MAX;
	memcpy(s_body, resp.body, blen);
	s_body[blen] = 0;
	body = s_body;
	left_tls = resp.https_to_http;
	used_tls = resp.tls && !left_tls;
	xe_http_response_free(&resp);

	/* Fresh JS context per page; drops the previous page's timers.
	 * Point it at the URL after redirects so a cookie set here matches
	 * the host that will be requested next. A Bing search keeps the
	 * typed URL: the body is the RSS list, not the address that was fetched. */
	s_postPage = false;
	if (bing_feed) {
		int hits = NsBingRssToHtml(s_body, blen, s_text, sizeof(s_text));
		if (hits > 0) {
			size_t hl = strlen(s_text);
			if (hl > NS_RAW_MAX)
				hl = NS_RAW_MAX;
			memcpy(s_body, s_text, hl);
			s_body[hl] = 0;
			blen = hl;
			body = s_body;
			_KePrint("[search] bing %d\n", hits);
		} else {
			_KePrint("[search] bing rss 0\n");
			bing_feed = 0;
		}
	}
	if (!bing_feed)
		NsAdoptLanded(url);
	snprintf(s_docUrl, sizeof(s_docUrl), "%s", url);
	NsJsBegin(url);
	NsJsScanTitle(body, blen);

	NsCollectCss(body, blen);
	NsCollectCssLinks(body, blen, url);
	_KePrint("[css] author sheet %d bytes\n", (int)strlen(s_cssBuf));
	xe_heap_check("post-collect");
	NsCssBegin();
	NsCssSetViewport(s_pageBox ? s_pageBox->wid.w : 632,
					 s_pageBox ? s_pageBox->wid.h : 420);
	_KePrint("[css] begin ok\n");
	xe_heap_check("post-begin");
	NsCssAddSheet(s_cssBuf, strlen(s_cssBuf));
	_KePrint("[css] sheet added\n");
	xe_heap_check("post-sheet");
	NsShowStatus("Laying out...");
	NsHtmlToText(body, blen, s_text, sizeof(s_text));
	_KePrint("[css] rendered %d bytes\n", (int)strlen(s_text));

	/* The search wall's script sets a cookie and then location.replace()s
	 * the same query with sei=. Follow that before painting the wall. */
	if (strstr(body, "knitsail"))
		_KePrint("[js] knitsail %d\n", NsJsDefined("knitsail"));
	if (nav_hops < 3 && NsJsConsumeNav(navraw, sizeof(navraw))) {
		NsResolveUrl(s_docUrl, navraw, navabs, sizeof(navabs));
		if (navabs[0] && strcmp(navabs, url) != 0 &&
			(!strncmp(navabs, "https://", 8) || !strncmp(navabs, "http://", 7))) {
			snprintf(came_from, sizeof(came_from), "%s", s_docUrl);
			snprintf(url, NS_URL_MAX, "%s", navabs);
			if (url != s_url)
				snprintf(s_url, sizeof(s_url), "%s", url);
			s_urlCursor = strlen(s_url);
			NsShowUrl();
			nav_hops++;
			NsLogNav(url);
			goto again;
		}
	}

	/* Real DOM + CSS box layout.  The old text pass above still runs so
	 * scripts, image fetching and the fallback renderer keep working. */
	NsJsSetDocument(NULL);
	if (s_layoutActive || s_domRoot) {
		NsLayoutDestroy();
		if (s_domRoot) {
			NsCssNodeDestroyTree(s_domRoot);
			s_domRoot = NULL;
		}
		s_layoutActive = 0;
	}
	{
		NsCssNode* root = NsDomParse(body, blen);
		if (root) {
			s_domRoot = root;
			NsJsSetDocument(root);
			NsCaptchaOffer(root);
			if (NsLayoutBuild(root,
							  s_pageBox ? s_pageBox->wid.w : 632,
							  s_pageBox ? s_pageBox->wid.h : 420) == 0) {
				s_layoutActive = 1;
				s_layoutScroll = 0;
			} else {
				NsJsSetDocument(NULL);
				NsCssNodeDestroyTree(s_domRoot);
				s_domRoot = NULL;
			}
		}
	}
	/* Leave the select context up. The search page's timer clears an
	 * inline style two seconds later and the tree has to be reselected. */
	NsJsFlushConsole();
	/* Inline scripts ran during the parse; the context (and any timers
	 * they armed) outlives the fetch until the next page. */
	if (NsJsActive())
		_KePrint("[js] %s ok, timers=%d\n", NsJsVersion(), NsJsTimerCount());
	s_postPage = true; /* later document.write appends at page end */
	s_googleHome = strstr(url, "google.") && !strstr(url, "/search");
	if (s_googleHome)
		g_pageCenter = 1;
	if (!s_text[0])
		strcpy(s_text, "(empty page)");
	NsShowStatus(s_text);
	return 0;
}

/* "cats" or "weather london" is a search. example.com and https:// stay URLs. */
static int NsIsSearch(const char* s) {
	if (!s || !s[0])
		return 0;
	if (strstr(s, "://"))
		return 0;
	if (strchr(s, ' ') || strchr(s, '\t'))
		return 1;
	if (!strchr(s, '.'))
		return 1;
	return 0;
}

static void NsEncodeQuery(const char* in, char* out, size_t outsz) {
	size_t o = 0;
	static const char* hex = "0123456789ABCDEF";
	for (; *in && o + 4 < outsz; in++) {
		unsigned char c = (unsigned char)*in;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.') {
			out[o++] = (char)c;
		} else if (c == ' ' || c == '\t') {
			out[o++] = '+';
		} else {
			out[o++] = '%';
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 15];
		}
	}
	out[o] = 0;
}

static void NsSubmitQuery(int lucky) {
	char q[NS_URL_MAX];
	NsEncodeQuery(s_query, q, sizeof(q));
	if (lucky)
		snprintf(s_url, sizeof(s_url), "https://www.google.com/search?q=%s&btnI=1&gbv=1&hl=en", q);
	else
		snprintf(s_url, sizeof(s_url), "https://www.google.com/search?q=%s&gbv=1&hl=en", q);
	s_urlCursor = strlen(s_url);
	NsShowUrl();
	NsFetch(s_url);
}

static void NsGoHandler(ChWidget* wid, ChWindow* win) {
	char q[NS_URL_MAX];
	(void)wid;
	(void)win;
	if (NsIsSearch(s_url)) {
		NsEncodeQuery(s_url, q, sizeof(q));
		snprintf(s_url, sizeof(s_url), "https://www.google.com/search?q=%s&gbv=1&hl=en", q);
		s_urlCursor = strlen(s_url);
		NsShowUrl();
	}
	NsFetch(s_url);
}

static void NsUrlClampCursor(void) {
	size_t len = strlen(s_url);
	if (s_urlCursor > len)
		s_urlCursor = len;
}

/* ---- layout-mode form controls ---- */

static void NsNodeTextAppend(NsCssNode* n, char* out, size_t cap, size_t* len) {
	if (!n || !out || !len)
		return;
	if (n->is_text) {
		if (n->text) {
			size_t l = n->text_len;
			if (*len + l >= cap)
				l = cap - *len - 1;
			memcpy(out + *len, n->text, l);
			*len += l;
			out[*len] = 0;
		}
		return;
	}
	for (NsCssNode* c = n->first_child; c; c = c->next)
		NsNodeTextAppend(c, out, cap, len);
}

static void NsControlValue(NsCssNode* n, char* out, size_t outsz) {
	size_t len = 0;
	if (!out || outsz == 0)
		return;
	out[0] = 0;
	if (!n)
		return;
	if (n == s_formFocus) {
		strncpy(out, s_formValue, outsz - 1);
		out[outsz - 1] = 0;
		return;
	}
	if (!strcmp(n->tag, "textarea")) {
		NsNodeTextAppend(n, out, outsz, &len);
		out[len] = 0;
		return;
	}
	const char* v = NsCssNodeAttr(n, "value");
	if (v) {
		strncpy(out, v, outsz - 1);
		out[outsz - 1] = 0;
	}
}

static void NsFormClearFocus(void) {
	int had = s_formFocus != NULL;
	s_formFocus = NULL;
	s_formValue[0] = 0;
	s_formCursor = 0;
	NsLayoutSetEdit(NULL, NULL, 0, 0);
	if (had && s_layoutActive && s_pageBox && s_mainWin)
		NsPageRender();
}

static int NsFormEditable(NsCssNode* n) {
	if (!n || n->is_text)
		return 0;
	if (!strcmp(n->tag, "textarea"))
		return 1;
	if (!strcmp(n->tag, "input")) {
		const char* type = NsCssNodeAttr(n, "type");
		if (!type || !type[0])
			return 1;
		if (!strcmp(type, "text") || !strcmp(type, "search") || !strcmp(type, "email") ||
		    !strcmp(type, "url") || !strcmp(type, "password") || !strcmp(type, "number") ||
		    !strcmp(type, "tel"))
			return 1;
	}
	return 0;
}

static int NsFormSubmitControl(NsCssNode* n) {
	if (!n || n->is_text)
		return 0;
	if (!strcmp(n->tag, "button")) {
		const char* type = NsCssNodeAttr(n, "type");
		return !type || !type[0] || !strcmp(type, "submit");
	}
	if (!strcmp(n->tag, "input")) {
		const char* type = NsCssNodeAttr(n, "type");
		return type && (!strcmp(type, "submit") || !strcmp(type, "button") ||
		                !strcmp(type, "reset") || !strcmp(type, "image"));
	}
	return 0;
}

static NsCssNode* NsFormAncestor(NsCssNode* n) {
	for (; n; n = n->parent) {
		if (!n->is_text && !strcmp(n->tag, "form"))
			return n;
	}
	return NULL;
}

static void NsFormFocusControl(NsCssNode* n) {
	if (!NsFormEditable(n))
		return;
	s_formFocus = n;
	NsControlValue(n, s_formValue, sizeof(s_formValue));
	s_formCursor = strlen(s_formValue);
	NsLayoutSetEdit(n, s_formValue, (int)s_formCursor, 1);
	NsPageRender();
}

static void NsAppendQuery(char* url, size_t cap, const char* name, const char* value) {
	char en[512];
	char ev[512];
	size_t len;
	if (!url || cap == 0 || !name || !name[0])
		return;
	NsEncodeQuery(name, en, sizeof(en));
	NsEncodeQuery(value ? value : "", ev, sizeof(ev));
	len = strlen(url);
	if (len + 4 >= cap)
		return;
	snprintf(url + len, cap - len, "%c%s=%s", strchr(url, '?') ? '&' : '?', en, ev);
}

static void NsFormAppendFields(NsCssNode* n, NsCssNode* submitNode, char* url, size_t cap) {
	if (!n)
		return;
	for (NsCssNode* c = n->first_child; c; c = c->next) {
		if (c->is_text)
			continue;
		if (NsCssNodeAttr(c, "disabled"))
			continue;
		if (!strcmp(c->tag, "input")) {
			const char* type = NsCssNodeAttr(c, "type");
			const char* name = NsCssNodeAttr(c, "name");
			char value[512];
			if (!type || !type[0])
				type = "text";
			if (!strcmp(type, "submit") || !strcmp(type, "button") || !strcmp(type, "reset") ||
			    !strcmp(type, "image")) {
				if (c != submitNode)
					continue;
			}
			if ((!strcmp(type, "checkbox") || !strcmp(type, "radio")) &&
			    !NsCssNodeAttr(c, "checked"))
				continue;
			NsControlValue(c, value, sizeof(value));
			if (!value[0] && (!strcmp(type, "checkbox") || !strcmp(type, "radio")))
				strcpy(value, "on");
			if (name && !strcmp(name, "btnI") && c == submitNode)
				strcpy(value, "1");
			NsAppendQuery(url, cap, name, value);
		} else if (!strcmp(c->tag, "textarea")) {
			const char* name = NsCssNodeAttr(c, "name");
			char value[512];
			NsControlValue(c, value, sizeof(value));
			NsAppendQuery(url, cap, name, value);
		} else if (!strcmp(c->tag, "select")) {
			const char* name = NsCssNodeAttr(c, "name");
			const char* value = NULL;
			char valuebuf[256];
			valuebuf[0] = 0;
			for (NsCssNode* o = c->first_child; o; o = o->next) {
				if (o->is_text || strcmp(o->tag, "option"))
					continue;
				if (!value || NsCssNodeAttr(o, "selected")) {
					size_t len = 0;
					NsNodeTextAppend(o, valuebuf, sizeof(valuebuf), &len);
					valuebuf[len] = 0;
					value = valuebuf;
					if (NsCssNodeAttr(o, "selected"))
						break;
				}
			}
			NsAppendQuery(url, cap, name, value ? value : "");
		} else if (!strcmp(c->tag, "button")) {
			if (c == submitNode) {
				const char* name = NsCssNodeAttr(c, "name");
				const char* value = NsCssNodeAttr(c, "value");
				char text[256];
				size_t len = 0;
				text[0] = 0;
				if (!value) {
					NsNodeTextAppend(c, text, sizeof(text), &len);
					text[len] = 0;
					value = text;
				}
				NsAppendQuery(url, cap, name, value);
			}
		}
		NsFormAppendFields(c, submitNode, url, cap);
	}
}

/* Google's search wall and the unusual-traffic page. The real checkbox
 * lives in an iframe this browser does not paint, so the clickable stand-in
 * is an ordinary button. The click talks to the same captcha endpoint. */
static char s_capSite[160];
static char s_capS[1600];
static char s_capQ[2500];
static char s_capCont[2500];
static char s_capAction[600];
/* Same document already got 429. Another click would ask again. */
static char s_capBlockUrl[NS_URL_MAX];
static int s_capBlockCode;

/* isalnum is not declared in this libc. A name byte is a letter, digit, '-' or '_'. */
static int NsNameByte(unsigned char c) {
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       c == '-' || c == '_';
}

static int NsCaptchaPage(const char* html) {
	if (!html)
		return 0;
	/* The knitsail wall hides "trouble accessing Google Search" in a div
	 * and has no widget. Offering a checkbox there only draws a 429. */
	if (strstr(html, "g-recaptcha") || strstr(html, "captcha-form"))
		return 1;
	if (strstr(html, "unusual traffic from your computer network"))
		return 1;
	return 0;
}

static void NsHtmlUnesc(const char* in, size_t n, char* out, size_t outsz) {
	size_t i = 0;
	size_t o = 0;
	if (!out || outsz == 0)
		return;
	if (!in)
		n = 0;
	while (i < n && o + 1 < outsz) {
		if (in[i] == '&') {
			if (i + 5 <= n && !strncmp(in + i, "&amp;", 5)) {
				out[o++] = '&';
				i += 5;
				continue;
			}
			if (i + 4 <= n && !strncmp(in + i, "&lt;", 4)) {
				out[o++] = '<';
				i += 4;
				continue;
			}
			if (i + 4 <= n && !strncmp(in + i, "&gt;", 4)) {
				out[o++] = '>';
				i += 4;
				continue;
			}
			if (i + 6 <= n && !strncmp(in + i, "&quot;", 6)) {
				out[o++] = '"';
				i += 6;
				continue;
			}
			if (i + 6 <= n && !strncmp(in + i, "&apos;", 6)) {
				out[o++] = '\'';
				i += 6;
				continue;
			}
			if (i + 5 <= n && !strncmp(in + i, "&#39;", 5)) {
				out[o++] = '\'';
				i += 5;
				continue;
			}
		}
		out[o++] = in[i++];
	}
	out[o] = 0;
}

/* Copy attr="value" or attr='value'. The name must end at '=' so
 * data-s does not match data-sitekey. */
static int NsHtmlAttr(const char* html, const char* attr, char* out, size_t outsz) {
	size_t al;
	const char* p;
	if (!html || !attr || !out || outsz == 0)
		return 0;
	out[0] = 0;
	al = strlen(attr);
	p = html;
	while ((p = strstr(p, attr)) != NULL) {
		const char* q;
		char quote;
		const char* e;
		if (p > html) {
			unsigned char prev = (unsigned char)p[-1];
			if (NsNameByte(prev)) {
				p += al;
				continue;
			}
		}
		q = p + al;
		while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
			q++;
		if (*q != '=') {
			p += al;
			continue;
		}
		q++;
		while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
			q++;
		quote = 0;
		if (*q == '"' || *q == '\'') {
			quote = *q;
			q++;
		}
		e = q;
		if (quote) {
			while (*e && *e != quote)
				e++;
		} else {
			while (*e && *e != ' ' && *e != '\t' && *e != '>' && *e != '\n' && *e != '\r')
				e++;
		}
		if ((size_t)(e - q) + 1 > outsz) {
			out[0] = 0;
			return 0;
		}
		NsHtmlUnesc(q, (size_t)(e - q), out, outsz);
		return out[0] ? 1 : 0;
	}
	return 0;
}

static int NsInputNamed(const char* html, const char* name, char* out, size_t outsz) {
	char pat[80];
	const char* pats[2];
	int i;
	if (!html || !name || !out || outsz == 0)
		return 0;
	out[0] = 0;
	snprintf(pat, sizeof(pat), "name=\"%s\"", name);
	pats[0] = pat;
	pats[1] = NULL;
	for (i = 0; i < 2; i++) {
		char pat2[80];
		const char* p;
		const char* tag;
		const char* end;
		char slice[4096];
		size_t n;
		if (i == 1)
			snprintf(pat2, sizeof(pat2), "name='%s'", name);
		p = strstr(html, i == 0 ? pat : pat2);
		if (!p)
			continue;
		tag = p;
		while (tag > html && tag[-1] != '<')
			tag--;
		end = strchr(p, '>');
		if (!end || end < tag)
			continue;
		n = (size_t)(end - tag);
		if (n >= sizeof(slice))
			n = sizeof(slice) - 1;
		memcpy(slice, (void*)tag, n);
		slice[n] = 0;
		if (NsHtmlAttr(slice, "value", out, outsz))
			return 1;
	}
	return 0;
}

static int NsCaptchaUrlKey(const char* html, const char* key, char* out, size_t outsz);

/* data-sitekey, or the sitekey a script passes to enterprise.render. */
static int NsCaptchaSitekey(const char* html, char* out, size_t outsz) {
	const char* p;
	if (!html || !out || outsz == 0)
		return 0;
	out[0] = 0;
	if (NsHtmlAttr(html, "data-sitekey", out, outsz))
		return 1;
	p = html;
	while ((p = strstr(p, "sitekey")) != NULL) {
		const char* q;
		char quote;
		const char* e;
		size_t n;
		if (p > html && NsNameByte((unsigned char)p[-1])) {
			p += 7;
			continue;
		}
		q = p + 7;
		while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
			q++;
		if (*q != ':' && *q != '=') {
			p += 7;
			continue;
		}
		q++;
		while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
			q++;
		if (*q != '"' && *q != '\'') {
			p += 7;
			continue;
		}
		quote = *q++;
		e = q;
		while (*e && *e != quote && NsNameByte((unsigned char)*e))
			e++;
		n = (size_t)(e - q);
		if (*e == quote && n >= 20 && n + 1 <= outsz) {
			memcpy(out, (void*)q, n);
			out[n] = 0;
			return 1;
		}
		p += 7;
	}
	return NsCaptchaUrlKey(html, "k", out, outsz);
}

/* ?k= or &s= on a recaptcha URL. The name is "k" or "s". */
static int NsCaptchaUrlKey(const char* html, const char* key, char* out, size_t outsz) {
	const char* p;
	size_t klen;
	if (!html || !key || !out || outsz < 8)
		return 0;
	out[0] = 0;
	klen = strlen(key);
	p = html;
	while ((p = strstr(p, "recaptcha")) != NULL) {
		const char* e = p;
		const char* lim;
		int i;
		for (i = 0; i < 1200 && *e; i++)
			e++;
		lim = e;
		e = p;
		while (e + klen + 2 < lim) {
			if ((e[0] == '?' || e[0] == '&') && !strncmp(e + 1, key, klen) && e[1 + klen] == '=') {
				const char* v = e + 2 + klen;
				size_t n = 0;
				while (v[n] && v[n] != '&' && v[n] != '"' && v[n] != '\'' && v[n] != ' ' &&
					   v[n] != '<' && n + 1 < outsz)
					n++;
				if (n >= 20) {
					memcpy(out, (void*)v, n);
					out[n] = 0;
					return 1;
				}
			}
			e++;
		}
		p += 9;
	}
	return 0;
}

static int NsCaptchaParse(const char* html, const char* base) {
	char action[600];
	const char* form;
	s_capSite[0] = 0;
	s_capS[0] = 0;
	s_capQ[0] = 0;
	s_capCont[0] = 0;
	s_capAction[0] = 0;
	if (!html)
		return 0;
	if (!NsCaptchaSitekey(html, s_capSite, sizeof(s_capSite)))
		return 0;
	if (!NsHtmlAttr(html, "data-s", s_capS, sizeof(s_capS)))
		NsCaptchaUrlKey(html, "s", s_capS, sizeof(s_capS));
	NsInputNamed(html, "q", s_capQ, sizeof(s_capQ));
	NsInputNamed(html, "continue", s_capCont, sizeof(s_capCont));
	action[0] = 0;
	form = strstr(html, "captcha-form");
	if (form) {
		const char* tag = form;
		const char* end;
		char slice[1024];
		size_t n;
		while (tag > html && tag[-1] != '<')
			tag--;
		end = strchr(form, '>');
		if (end && end > tag) {
			n = (size_t)(end - tag);
			if (n >= sizeof(slice))
				n = sizeof(slice) - 1;
			memcpy(slice, (void*)tag, n);
			slice[n] = 0;
			NsHtmlAttr(slice, "action", action, sizeof(action));
		}
	}
	if (!action[0] || !strcmp(action, "index") || !strcmp(action, "/sorry/index"))
		snprintf(s_capAction, sizeof(s_capAction), "https://www.google.com/sorry/index");
	else if (!strncmp(action, "https://", 8) || !strncmp(action, "http://", 7))
		snprintf(s_capAction, sizeof(s_capAction), "%s", action);
	else
		NsResolveUrl(base && base[0] ? base : "https://www.google.com/sorry/index", action,
					 s_capAction, sizeof(s_capAction));
	return s_capSite[0] ? 1 : 0;
}

static int NsEncFull(const char* in, char* out, size_t outsz) {
	if (!out || outsz == 0)
		return 0;
	NsEncodeQuery(in ? in : "", out, outsz);
	if (in && in[0] && !out[0])
		return 0;
	if (strlen(out) + 4 >= outsz)
		return 0;
	return 1;
}

/* co is the page origin, scheme://host, with a port only when it is not the
 * default. XEClib snprintf has no %.*s: that specifier printed ".*" and then
 * the host pointer as a number, so the anchor was the invalid-domain page. */
static int NsCaptchaOrigin(const char* url, char* out, size_t outsz) {
	const char* host;
	const char* end;
	const char* colon;
	const char* scheme;
	char hostbuf[256];
	int port;
	int defport;
	size_t hlen;
	int n;
	if (!url || !out || outsz < 8)
		return 0;
	out[0] = 0;
	if (!strncmp(url, "https://", 8)) {
		scheme = "https";
		host = url + 8;
		port = 443;
		defport = 443;
	} else if (!strncmp(url, "http://", 7)) {
		scheme = "http";
		host = url + 7;
		port = 80;
		defport = 80;
	} else {
		return 0;
	}
	end = host;
	while (*end && *end != '/' && *end != '?' && *end != '#')
		end++;
	colon = host;
	while (colon < end && *colon != ':')
		colon++;
	hlen = (size_t)(colon - host);
	if (!hlen || hlen >= sizeof(hostbuf))
		return 0;
	memcpy(hostbuf, (void*)host, hlen);
	hostbuf[hlen] = 0;
	if (colon < end && colon[1] >= '0' && colon[1] <= '9')
		port = atoi(colon + 1);
	if (port == defport)
		n = snprintf(out, outsz, "%s://%s", scheme, hostbuf);
	else
		n = snprintf(out, outsz, "%s://%s:%d", scheme, hostbuf, port);
	if (n < 0 || (size_t)n >= outsz)
		return 0;
	return out[0] ? 1 : 0;
}

static int NsB64(const char* in, char* out, size_t outsz) {
	static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t len;
	size_t i = 0;
	size_t o = 0;
	if (!in || !out || outsz == 0)
		return 0;
	len = strlen(in);
	while (i < len) {
		unsigned a = (unsigned char)in[i++];
		unsigned b = 0;
		unsigned c = 0;
		int nb = 0;
		int nc = 0;
		if (i < len) {
			b = (unsigned char)in[i++];
			nb = 1;
		}
		if (i < len) {
			c = (unsigned char)in[i++];
			nc = 1;
		}
		if (o + 5 >= outsz) {
			out[0] = 0;
			return 0;
		}
		out[o++] = tab[a >> 2];
		out[o++] = tab[((a & 3) << 4) | (b >> 4)];
		out[o++] = nb ? tab[((b & 15) << 2) | (c >> 6)] : '=';
		out[o++] = nc ? tab[c & 63] : '=';
	}
	out[o] = 0;
	return 1;
}

static NsCssNode* NsFindTag(NsCssNode* n, const char* tag) {
	NsCssNode* c;
	NsCssNode* found;
	if (!n || !tag)
		return NULL;
	if (!n->is_text && !strcmp(n->tag, tag))
		return n;
	for (c = n->first_child; c; c = c->next) {
		found = NsFindTag(c, tag);
		if (found)
			return found;
	}
	return NULL;
}

static void NsPrependChild(NsCssNode* parent, NsCssNode* child) {
	if (!parent || !child)
		return;
	child->parent = parent;
	child->prev = NULL;
	child->next = parent->first_child;
	if (parent->first_child)
		parent->first_child->prev = child;
	else
		parent->last_child = child;
	parent->first_child = child;
}

static void NsCaptchaOffer(NsCssNode* root) {
	NsCssNode* host;
	NsCssNode* btn;
	if (!root || !NsCaptchaPage(s_body))
		return;
	if (NsCssNodeById(root, "ns-captcha"))
		return;
	host = NsCssNodeById(root, "recaptcha");
	if (!host)
		host = NsFindTag(root, "body");
	if (!host)
		host = root;
	btn = NsCssNodeCreate(NULL, "input");
	if (!btn)
		return;
	NsCssNodeAddAttribute(btn, "type", "button");
	NsCssNodeAddAttribute(btn, "value", "I'm not a robot");
	NsCssNodeSetId(btn, "ns-captcha");
	NsPrependChild(host, btn);
	NsCssReselectTree(btn);
	_KePrint("[captcha] button\n");
}

static void NsCaptchaNote(const char* label) {
	NsCssNode* btn;
	if (!s_domRoot || !s_layoutActive)
		return;
	btn = NsCssNodeById(s_domRoot, "ns-captcha");
	if (btn && label && label[0])
		NsCssNodeAddAttribute(btn, "value", label);
	if (NsLayoutBuild(s_domRoot, s_pageBox ? s_pageBox->wid.w : 632,
					  s_pageBox ? s_pageBox->wid.h : 420) == 0)
		s_layoutActive = 1;
	else
		s_layoutActive = 0;
	NsPageRender();
}

struct NsCapWork {
	char ver[80];
	char co[180];
	char anchor[3072];
	char token[16 * 1024];
	char rtok[16 * 1024];
	char body[40 * 1024];
	char enc[20 * 1024];
	char sorry[3072];
	char finalu[3072];
};

static int NsCapAppend(struct NsCapWork* w, int* first, const char* key, const char* val) {
	size_t used;
	int n;
	if (!w || !first || !key)
		return 0;
	if (!NsEncFull(val ? val : "", w->enc, sizeof(w->enc)))
		return 0;
	used = strlen(w->body);
	n = snprintf(w->body + used, sizeof(w->body) - used, "%s%s=%s", *first ? "" : "&", key, w->enc);
	if (n < 0 || (size_t)n >= sizeof(w->body) - used)
		return 0;
	*first = 0;
	return 1;
}

/* 1 copied, 0 absent, -1 present but longer than out. */
static int NsCaptchaTake(const char* v, const char* e, char* out, size_t outsz) {
	if (!v || !e || e <= v || !out || outsz == 0)
		return 0;
	if ((size_t)(e - v) + 1 > outsz) {
		_KePrint("[captcha] token fit\n");
		return -1;
	}
	NsHtmlUnesc(v, (size_t)(e - v), out, outsz);
	if (!out[0])
		return 0;
	_KePrint("[captcha] token %d\n", (int)strlen(out));
	return 1;
}

/* value= may sit before id="recaptcha-token". Read that one tag. */
static int NsCaptchaToken(const char* html, char* out, size_t outsz) {
	const char* p;
	if (!html || !out || outsz == 0)
		return 0;
	out[0] = 0;
	p = html;
	while ((p = strstr(p, "recaptcha-token")) != NULL) {
		const char* tag = p;
		const char* end;
		const char* scan;
		int saw = 0;
		/* value= can be a long attribute in front of the id. Stop at the
		 * tag edges so a later value= on the page is not read. */
		while (tag > html && tag[-1] != '<' && tag[-1] != '>')
			tag--;
		end = strchr(p, '>');
		if (tag > html && tag[-1] == '<' && end && (size_t)(end - tag) < 40000) {
			scan = tag;
			while (scan + 6 < end) {
				const char* v;
				const char* e;
				char quote;
				if (!((scan == tag || scan[-1] == ' ' || scan[-1] == '\t' || scan[-1] == '\n' ||
					   scan[-1] == '\r' || scan[-1] == '<') &&
					  !strncmp(scan, "value", 5))) {
					scan++;
					continue;
				}
				v = scan + 5;
				while (v < end && (*v == ' ' || *v == '\t'))
					v++;
				if (v >= end || *v != '=') {
					scan += 5;
					continue;
				}
				v++;
				while (v < end && (*v == ' ' || *v == '\t'))
					v++;
				quote = 0;
				if (v < end && (*v == '"' || *v == '\'')) {
					quote = *v;
					v++;
				}
				e = v;
				if (quote) {
					while (e < end && *e != quote)
						e++;
				} else {
					while (e < end && *e != ' ' && *e != '\t' && *e != '>')
						e++;
				}
				saw = 1;
				{
					int took = NsCaptchaTake(v, e, out, outsz);
					if (took != 0)
						return took;
				}
				break;
			}
			if (!saw) {
				const char* t = end + 1;
				const char* te = strchr(t, '<');
				if (te && te > t) {
					while (t < te && (*t == ' ' || *t == '\n' || *t == '\r' || *t == '\t'))
						t++;
					while (te > t &&
						   (te[-1] == ' ' || te[-1] == '\n' || te[-1] == '\r' || te[-1] == '\t'))
						te--;
					if (te > t) {
						int took = NsCaptchaTake(t, te, out, outsz);
						if (took != 0)
							return took;
					}
				}
			}
		}
		p += 15;
	}
	_KePrint("[captcha] token 0\n");
	return 0;
}

static int NsCapHttp(const char* url) {
	return url && url[0] &&
		   (!strncmp(url, "https://", 8) || !strncmp(url, "http://", 7));
}

static int NsCapSorry(const char* url) {
	return url && strstr(url, "/sorry");
}

/* A sorry link already on the page carries the q token. continue= alone does not. */
static int NsCaptchaSorryUrl(const char* html, const char* base, char* out, size_t outsz) {
	const char* p;
	char raw[3072];
	if (!html || !out || outsz < 8)
		return 0;
	out[0] = 0;
	p = html;
	while ((p = strstr(p, "sorry/index")) != NULL) {
		const char* s = p;
		size_t n = 0;
		while (s > html && s[-1] != '"' && s[-1] != '\'' && s[-1] != ' ' && s[-1] != '\n' &&
			   s[-1] != '\r' && s[-1] != '=' && s[-1] != '>' && s[-1] != '<')
			s--;
		while (s[n] && s[n] != '"' && s[n] != '\'' && s[n] != ' ' && s[n] != '<' && s[n] != '>' &&
			   s[n] != '\n' && s[n] != '\r' && n + 1 < sizeof(raw))
			n++;
		if (n < 11) {
			p += 11;
			continue;
		}
		NsHtmlUnesc(s, n, raw, sizeof(raw));
		if (!strncmp(raw, "https://", 8) || !strncmp(raw, "http://", 7)) {
			snprintf(out, outsz, "%s", raw);
			return out[0] ? 1 : 0;
		}
		if (raw[0] == '/' || !strncmp(raw, "sorry/", 6)) {
			NsResolveUrl(base && base[0] ? base : "https://www.google.com/sorry/index", raw, out,
						 outsz);
			return out[0] ? 1 : 0;
		}
		p += 11;
	}
	return 0;
}

/* Search words from q=. The sorry payload also uses q= and starts with Eg. */
static int NsCaptchaQuery(const char* url, char* out, size_t outsz) {
	const char* p;
	size_t n = 0;
	if (!url || !out || outsz < 2)
		return 0;
	out[0] = 0;
	p = strstr(url, "q=");
	while (p) {
		if (p == url || p[-1] == '?' || p[-1] == '&' || p[-1] == ';')
			break;
		p = strstr(p + 2, "q=");
	}
	if (!p)
		return 0;
	p += 2;
	while (p[n] && p[n] != '&' && p[n] != '#' && n + 1 < outsz)
		n++;
	if (!n || (n > 40 && p[0] == 'E' && p[1] == 'g'))
		return 0;
	memcpy(out, (void*)p, n);
	out[n] = 0;
	return 1;
}

static int NsCaptchaSearchUrl(const char* doc, char* out, size_t outsz) {
	char q[400];
	int n;
	if (!NsCaptchaQuery(doc, q, sizeof(q)))
		return 0;
	n = snprintf(out, outsz, "https://www.google.com/search?q=%s&hl=en", q);
	return n > 0 && (size_t)n < outsz;
}

static int NsCaptchaRresp(const char* html, char* out, size_t outsz) {
	const char* p;
	const char* e;
	if (!html || !out || outsz == 0)
		return 0;
	out[0] = 0;
	p = strstr(html, "\"rresp\"");
	if (!p)
		return 0;
	p += 7;
	while (*p && *p != '"')
		p++;
	if (*p != '"')
		return 0;
	p++;
	e = p;
	while (*e && *e != '"')
		e++;
	if (e == p || (size_t)(e - p) + 1 > outsz)
		return 0;
	memcpy(out, (void*)p, (size_t)(e - p));
	out[e - p] = 0;
	return 1;
}

static void NsCaptchaActivate(void) {
	struct NsCapWork* w;
	xe_http_response resp;
	char note[96];
	char dest[3072];
	const char* base;
	int navigated = 0;
	int rc;

	_KePrint("[captcha] click\n");
	NsCaptchaNote("Checking...");
	snprintf(note, sizeof(note), "Check failed, click to try again");
	dest[0] = 0;
	w = (struct NsCapWork*)malloc(sizeof(*w));
	if (!w) {
		NsCaptchaNote(note);
		return;
	}
	memset(w, 0, sizeof(*w));
	memset(&resp, 0, sizeof(resp));
	base = s_docUrl[0] ? s_docUrl : s_url;
	xe_http_set_referer(base);

	if (!NsCaptchaParse(s_body, base)) {
		int n;
		char search[600];
		/* This document already drew a 429. Asking sorry/index again digs in. */
		if (s_capBlockCode == 429 && base[0] && !strcmp(s_capBlockUrl, base)) {
			snprintf(note, sizeof(note), "Google refused the check (429)");
			_KePrint("[captcha] skip %d\n", s_capBlockCode);
			free(w);
			NsCaptchaNote(note);
			return;
		}
		/* A full homepage URL as continue is what sorry/index answers with 429.
		 * The check wants the plain search address. */
		search[0] = 0;
		if (!NsCaptchaSorryUrl(s_body, base, w->sorry, sizeof(w->sorry))) {
			if (!NsCaptchaSearchUrl(base, search, sizeof(search)) ||
				!NsEncFull(search, w->enc, sizeof(w->enc))) {
				snprintf(note, sizeof(note), "Could not open the check");
				free(w);
				NsCaptchaNote(note);
				return;
			}
			n = snprintf(w->sorry, sizeof(w->sorry),
						 "https://www.google.com/sorry/index?continue=%s", w->enc);
			if (n < 0 || (size_t)n >= sizeof(w->sorry)) {
				snprintf(note, sizeof(note), "Check link is too long");
				free(w);
				NsCaptchaNote(note);
				return;
			}
		}
		if (search[0])
			xe_http_set_referer(search);
		rc = xe_http_get(w->sorry, "GET", kNsUa, &resp, NS_BODY_MAX);
		_KePrint("[captcha] sorry %d %d\n", resp.status, resp.body ? (int)resp.body_len : 0);
		if (!resp.body || !resp.body_len || !NsCaptchaParse(resp.body, w->sorry)) {
			if (resp.status == 429) {
				snprintf(note, sizeof(note), "Google refused the check (429)");
				s_capBlockCode = 429;
				snprintf(s_capBlockUrl, sizeof(s_capBlockUrl), "%s", base);
			} else {
				snprintf(note, sizeof(note), "Could not open the check");
			}
			_KePrint("[captcha] nosite\n");
			xe_http_response_free(&resp);
			free(w);
			NsCaptchaNote(note);
			(void)rc;
			return;
		}
		s_capBlockCode = 0;
		s_capBlockUrl[0] = 0;
		xe_http_response_free(&resp);
		memset(&resp, 0, sizeof(resp));
	}
	_KePrint("[captcha] site %d\n", (int)strlen(s_capSite));
	if (s_capAnchorMiss && base[0] && !strcmp(s_capAnchorUrl, base)) {
		snprintf(note, sizeof(note), "Checkbox was not accepted");
		_KePrint("[captcha] token skip\n");
		free(w);
		NsCaptchaNote(note);
		return;
	}

	rc = xe_http_get("https://www.google.com/recaptcha/enterprise.js", "GET", kNsUa, &resp,
					 NS_BODY_MAX);
	if (resp.body) {
		const char* p = resp.body;
		const char* hit;
		while ((hit = strstr(p, "releases/")) != NULL) {
			const char* s = hit + 9;
			size_t n = 0;
			while (s[n] && n < sizeof(w->ver) - 1 && NsNameByte((unsigned char)s[n]))
				n++;
			if (n >= 8 && s[n] == '/') {
				memcpy(w->ver, (void*)s, n);
				w->ver[n] = 0;
				break;
			}
			p = hit + 9;
		}
	}
	_KePrint("[captcha] api %d ver %d\n", resp.status, (int)strlen(w->ver));
	xe_http_response_free(&resp);
	memset(&resp, 0, sizeof(resp));
	(void)rc;
	if (!w->ver[0]) {
		snprintf(note, sizeof(note), "Could not start the check");
		goto done;
	}
	{
		char origin[180];
		size_t colen;
		if (!NsCaptchaOrigin(base, origin, sizeof(origin)) ||
			!NsB64(origin, w->co, sizeof(w->co))) {
			snprintf(note, sizeof(note), "Could not start the check");
			goto done;
		}
		/* Padding turns the origin into the invalid-domain anchor, which
		 * is 16803 bytes and has no token. */
		colen = strlen(w->co);
		while (colen > 0 && w->co[colen - 1] == '=')
			w->co[--colen] = 0;
		_KePrint("[captcha] co %d\n", (int)colen);
	}
	{
		char sk[200];
		char coe[200];
		int n;
		if (!NsEncFull(s_capSite, sk, sizeof(sk)) || !NsEncFull(w->co, coe, sizeof(coe))) {
			snprintf(note, sizeof(note), "Could not start the check");
			goto done;
		}
		n = snprintf(w->anchor, sizeof(w->anchor),
					 "https://www.google.com/recaptcha/enterprise/anchor?ar=1&k=%s&co=%s&hl=en&v=%s&"
					 "size=normal",
					 sk, coe, w->ver);
		if (n < 0 || (size_t)n >= sizeof(w->anchor)) {
			snprintf(note, sizeof(note), "Check link is too long");
			goto done;
		}
		if (s_capS[0]) {
			size_t used;
			if (!NsEncFull(s_capS, w->enc, sizeof(w->enc))) {
				snprintf(note, sizeof(note), "Could not start the check");
				goto done;
			}
			used = strlen(w->anchor);
			n = snprintf(w->anchor + used, sizeof(w->anchor) - used, "&s=%s", w->enc);
			if (n < 0 || (size_t)n >= sizeof(w->anchor) - used) {
				snprintf(note, sizeof(note), "Check link is too long");
				goto done;
			}
		}
	}
	if (strlen(w->anchor) >= 3000) {
		snprintf(note, sizeof(note), "Check link is too long");
		goto done;
	}
	rc = xe_http_get(w->anchor, "GET", kNsUa, &resp, NS_BODY_MAX);
	_KePrint("[captcha] anchor %d %d\n", resp.status, resp.body ? (int)resp.body_len : 0);
	if (resp.body && strstr(resp.body, "ErrorMain"))
		_KePrint("[captcha] anchor error\n");
	if (!resp.body || NsCaptchaToken(resp.body, w->token, sizeof(w->token)) != 1) {
		if (resp.body && base[0]) {
			s_capAnchorMiss = 1;
			snprintf(s_capAnchorUrl, sizeof(s_capAnchorUrl), "%s", base);
		}
		snprintf(note, sizeof(note), "Checkbox was not accepted");
		xe_http_response_free(&resp);
		goto done;
	}
	xe_http_response_free(&resp);
	memset(&resp, 0, sizeof(resp));

	{
		int first = 1;
		char reload[320];
		w->body[0] = 0;
		if (!NsCapAppend(w, &first, "v", w->ver) || !NsCapAppend(w, &first, "reason", "q") ||
			!NsCapAppend(w, &first, "c", w->token) || !NsCapAppend(w, &first, "k", s_capSite) ||
			!NsCapAppend(w, &first, "co", w->co) || !NsCapAppend(w, &first, "hl", "en") ||
			!NsCapAppend(w, &first, "size", "normal") ||
			(s_capS[0] && !NsCapAppend(w, &first, "s", s_capS))) {
			_KePrint("[captcha] reload fit\n");
			snprintf(note, sizeof(note), "Checkbox was not accepted");
			goto done;
		}
		snprintf(reload, sizeof(reload), "https://www.google.com/recaptcha/enterprise/reload?k=%s",
				 s_capSite);
		xe_http_set_body("application/x-www-form-urlencoded", w->body, strlen(w->body));
		rc = xe_http_get(reload, "POST", kNsUa, &resp, NS_BODY_MAX);
	}
	_KePrint("[captcha] reload %d %d\n", resp.status, resp.body ? (int)resp.body_len : 0);
	if (!resp.body || !NsCaptchaRresp(resp.body, w->rtok, sizeof(w->rtok))) {
		/* No checkbox token in the reload body. Do not fetch the anchor again. */
		if (resp.body && base[0]) {
			s_capAnchorMiss = 1;
			snprintf(s_capAnchorUrl, sizeof(s_capAnchorUrl), "%s", base);
		}
		snprintf(note, sizeof(note), "Checkbox was not accepted");
		xe_http_response_free(&resp);
		goto done;
	}
	xe_http_response_free(&resp);
	memset(&resp, 0, sizeof(resp));

	{
		int first = 1;
		w->body[0] = 0;
		if ((s_capQ[0] && !NsCapAppend(w, &first, "q", s_capQ)) ||
			(s_capCont[0] && !NsCapAppend(w, &first, "continue", s_capCont)) ||
			!NsCapAppend(w, &first, "g-recaptcha-response", w->rtok)) {
			snprintf(note, sizeof(note), "Could not send the check");
			goto done;
		}
	}
	xe_http_set_body("application/x-www-form-urlencoded", w->body, strlen(w->body));
	rc = xe_http_get(s_capAction[0] ? s_capAction : "https://www.google.com/sorry/index", "POST",
					 kNsUa, &resp, NS_BODY_MAX);
	xe_http_last_url(w->finalu, sizeof(w->finalu));
	_KePrint("[captcha] post %d\n", resp.status);
	{
		/* A sorry URL, or another unusual-traffic document, is this page again. */
		int blocked = !resp.body || !resp.body_len || NsCaptchaPage(resp.body);
		const char* go = NULL;
		if (!blocked && NsCapHttp(w->finalu) && !NsCapSorry(w->finalu))
			go = w->finalu;
		else if (!blocked && NsCapHttp(s_capCont) && !NsCapSorry(s_capCont))
			go = s_capCont;
		xe_http_response_free(&resp);
		memset(&resp, 0, sizeof(resp));
		(void)rc;
		if (go) {
			snprintf(dest, sizeof(dest), "%s", go);
			navigated = 1;
		} else {
			snprintf(note, sizeof(note), "Check failed, click to try again");
			_KePrint("[captcha] stay\n");
		}
	}

done:
	xe_http_response_free(&resp);
	free(w);
	if (navigated)
		NsFetch(dest);
	else
		NsCaptchaNote(note);
}

static void NsFormSubmit(NsCssNode* submitNode) {
	if (submitNode && submitNode->id[0] && !strcmp(submitNode->id, "ns-captcha")) {
		NsCaptchaActivate();
		return;
	}
	NsCssNode* form = NsFormAncestor(submitNode ? submitNode : s_formFocus);
	char action[NS_URL_MAX * 2];
	char url[NS_URL_MAX * 2];
	const char* attr;
	if (!form)
		return;
	attr = NsCssNodeAttr(form, "action");
	action[0] = 0;
	if (attr && attr[0]) {
		NsResolveUrl(s_docUrl[0] ? s_docUrl : s_url, attr, action, sizeof(action));
	} else {
		snprintf(action, sizeof(action), "%s", s_docUrl[0] ? s_docUrl : s_url);
	}
	if (!action[0])
		return;
	snprintf(url, sizeof(url), "%s", action);
	NsFormAppendFields(form, submitNode, url, sizeof(url));
	snprintf(s_url, sizeof(s_url), "%s", url);
	s_urlCursor = strlen(s_url);
	NsShowUrl();
	NsFormClearFocus();
	NsFetch(s_url);
}

static void NsHandleKey(int raw) {
	/* Layout-mode form editing takes precedence over URL-bar editing. */
	if (s_formFocus && s_layoutActive) {
		size_t len = strlen(s_formValue);
		if (s_formCursor > len)
			s_formCursor = len;
		if (raw == NS_KEY_LEFT || raw == NS_KEY_HOME) {
			if (raw == NS_KEY_HOME)
				s_formCursor = 0;
			else if (s_formCursor > 0)
				s_formCursor--;
			NsLayoutSetEdit(s_formFocus, s_formValue, (int)s_formCursor, 1);
			NsPageRender();
			return;
		}
		if (raw == NS_KEY_RIGHT || raw == NS_KEY_END) {
			if (raw == NS_KEY_END)
				s_formCursor = len;
			else if (s_formCursor < len)
				s_formCursor++;
			NsLayoutSetEdit(s_formFocus, s_formValue, (int)s_formCursor, 1);
			NsPageRender();
			return;
		}
		if (raw == NS_KEY_UP) {
			NsPageScroll(-1);
			return;
		}
		if (raw == NS_KEY_DOWN) {
			NsPageScroll(1);
			return;
		}
		if (raw == NS_KEY_PGUP || raw == NS_KEY_PGDN) {
			int vis = NsVisibleLines();
			NsPageScroll(raw == NS_KEY_PGUP ? (vis > 1 ? -(vis - 1) : -1)
			                                : (vis > 1 ? (vis - 1) : 1));
			return;
		}
		if (raw == 0x01) { /* Escape: leave the field. */
			NsFormClearFocus();
			return;
		}
		char rawkey = ChitralekhaGetKeyPress(raw);
		if (rawkey == KEY_RETURN) {
			NsFormSubmit(NULL);
			return;
		}
		if (rawkey == KEY_BACKSPACE) {
			if (s_formCursor > 0) {
				memmove(s_formValue + s_formCursor - 1, s_formValue + s_formCursor,
				        len - s_formCursor + 1);
				s_formCursor--;
				NsLayoutSetEdit(s_formFocus, s_formValue, (int)s_formCursor, 1);
				NsPageRender();
			}
			return;
		}
		if (raw == 0xE053 || raw == KEY_DELETE) {
			if (s_formCursor < len) {
				memmove(s_formValue + s_formCursor, s_formValue + s_formCursor + 1,
				        len - s_formCursor);
				NsLayoutSetEdit(s_formFocus, s_formValue, (int)s_formCursor, 1);
				NsPageRender();
			}
			return;
		}
		int fc = ChitralekhaKeyToASCII(raw);
		if (fc >= 32 && fc < 127 && len + 1 < sizeof(s_formValue)) {
			memmove(s_formValue + s_formCursor + 1, s_formValue + s_formCursor,
			        len - s_formCursor + 1);
			s_formValue[s_formCursor] = (char)fc;
			s_formCursor++;
			NsLayoutSetEdit(s_formFocus, s_formValue, (int)s_formCursor, 1);
			NsPageRender();
		}
		return;
	}
	if (s_qFocus) {
		size_t len = strlen(s_query);
		if (s_qCursor > len)
			s_qCursor = len;
		if (raw == NS_KEY_LEFT || raw == NS_KEY_HOME) {
			if (raw == NS_KEY_HOME)
				s_qCursor = 0;
			else if (s_qCursor > 0)
				s_qCursor--;
			NsPageRender();
			return;
		}
		if (raw == NS_KEY_RIGHT || raw == NS_KEY_END) {
			if (raw == NS_KEY_END)
				s_qCursor = len;
			else if (s_qCursor < len)
				s_qCursor++;
			NsPageRender();
			return;
		}
		if (raw == NS_KEY_UP) {
			NsPageScroll(-1);
			return;
		}
		if (raw == NS_KEY_DOWN) {
			NsPageScroll(1);
			return;
		}
		char rawkey = ChitralekhaGetKeyPress(raw);
		if (rawkey == KEY_RETURN) {
			NsSubmitQuery(0);
			return;
		}
		if (rawkey == KEY_BACKSPACE) {
			if (s_qCursor > 0) {
				memmove(s_query + s_qCursor - 1, s_query + s_qCursor, len - s_qCursor + 1);
				s_qCursor--;
				NsPageRender();
			}
			return;
		}
		int qc = ChitralekhaKeyToASCII(raw);
		if (qc >= 32 && qc < 127 && len + 1 < sizeof(s_query)) {
			memmove(s_query + s_qCursor + 1, s_query + s_qCursor, len - s_qCursor + 1);
			s_query[s_qCursor] = (char)qc;
			s_qCursor++;
			NsPageRender();
		}
		return;
	}
	/* URL line navigation. Up/Down/PgUp/PgDn scroll the page instead. */
	if (raw == NS_KEY_LEFT || raw == NS_KEY_HOME) {
		NsUrlClampCursor();
		if (raw == NS_KEY_HOME)
			s_urlCursor = 0;
		else if (s_urlCursor > 0)
			s_urlCursor--;
		NsShowUrl();
		return;
	}
	if (raw == NS_KEY_RIGHT || raw == NS_KEY_END) {
		NsUrlClampCursor();
		if (raw == NS_KEY_END)
			s_urlCursor = strlen(s_url);
		else if (s_urlCursor < strlen(s_url))
			s_urlCursor++;
		NsShowUrl();
		return;
	}
	if (raw == NS_KEY_UP) {
		NsPageScroll(-1);
		return;
	}
	if (raw == NS_KEY_DOWN) {
		NsPageScroll(1);
		return;
	}
	if (raw == NS_KEY_PGUP) {
		int vis = NsVisibleLines();
		NsPageScroll(vis > 1 ? -(vis - 1) : -1);
		return;
	}
	if (raw == NS_KEY_PGDN) {
		int vis = NsVisibleLines();
		NsPageScroll(vis > 1 ? (vis - 1) : 1);
		return;
	}
	/* Terminal's pattern: GetKeyPress for special keys, KeyToASCII for
     * printables. Releases fall through to 0 and are ignored. */
	char rawkey = ChitralekhaGetKeyPress(raw);
	if (rawkey == KEY_RETURN) { /* Enter -> fetch */
		NsFetch(s_url);
		return;
	}
	if (rawkey == KEY_BACKSPACE) {
		NsUrlClampCursor();
		if (s_urlCursor > 0) {
			size_t len = strlen(s_url);
			memmove(s_url + s_urlCursor - 1, s_url + s_urlCursor, len - s_urlCursor + 1);
			s_urlCursor--;
			NsShowUrl();
		}
		return;
	}
	int c = ChitralekhaKeyToASCII(raw);
	if (c >= 32 && c < 127) {
		NsUrlClampCursor();
		size_t len = strlen(s_url);
		if (len + 1 < sizeof(s_url)) {
			memmove(s_url + s_urlCursor + 1, s_url + s_urlCursor, len - s_urlCursor + 1);
			s_url[s_urlCursor] = (char)c;
			s_urlCursor++;
			NsShowUrl();
		}
	}
}

static void WindowHandleMessage(PostEvent* e) {
	switch (e->type) {
	case DEODHAI_REPLY_MOUSE_EVENT: {
		int handle = e->dword4;
		int mx = e->dword;
		int my = e->dword2;
		int button = e->dword3;
		ChWindow* mouseWin = ChGetWindowByHandle(s_mainWin, handle);
		/* Kernel tablet wheel is the value 5 (up) or 6 (down). The
		 * vendored input path (--bypass-virtio-input) reports the
		 * same wheel as bit 0x10 (up) or 0x20 (down). 5 overlaps
		 * left and middle, so those two stay equality tests. */
		if (button == 0x05 || button == 0x06 || (button & 0x10) || (button & 0x20)) {
			int down = (button == 0x06) || (button & 0x20);
			NsPageScroll(down ? 3 : -3);
			memset(e, 0, sizeof(PostEvent));
			break;
		}
		ChWindowHandleMouse(mouseWin, mx, my, button);
		if (button && s_mainWin && s_mainWin->info) {
			int lx = mx - s_mainWin->info->x;
			int ly = my - s_mainWin->info->y;
			/* DOM layout links and form controls are pixel rectangles.
			 * Resolve links against the page URL that produced this document,
			 * not the URL bar. */
			if (s_layoutActive && s_pageBox && button < 5) {
				int px = lx - s_pageBox->wid.x;
				int py = ly - s_pageBox->wid.y;
				if (px >= 0 && px < s_pageBox->wid.w && py >= 0 && py < s_pageBox->wid.h) {
					NsCssNode* ctrl = NULL;
					if (NsLayoutHitControl(px, py + s_layoutScroll, &ctrl)) {
						if (NsFormEditable(ctrl)) {
							NsFormFocusControl(ctrl);
						} else if (NsFormSubmitControl(ctrl)) {
							NsFormSubmit(ctrl);
						}
						memset(e, 0, sizeof(PostEvent));
						break;
					}
					char href[640];
					if (NsLayoutHitLink(px, py + s_layoutScroll, href, sizeof(href))) {
						char abs[NS_URL_MAX];
						NsResolveUrl(s_docUrl, href, abs, sizeof(abs));
						if (abs[0]) {
							snprintf(s_url, sizeof(s_url), "%s", abs);
							s_urlCursor = strlen(s_url);
							NsShowUrl();
							NsFetch(s_url);
							memset(e, 0, sizeof(PostEvent));
							break;
						}
					}
					/* A click on the page background leaves form editing. */
					NsFormClearFocus();
				} else if (ly < s_pageBox->wid.y) {
					NsFormClearFocus();
				}
			}
			if (!s_layoutActive && s_qHave && lx >= s_qX && lx < s_qX + s_qW && ly >= s_qY && ly < s_qY + s_qH) {
				int rel = lx - s_qX - 8;
				size_t i = 0;
				int w = 0;
				ChFont* f = s_pageBox ? s_pageBox->font : NULL;
				s_qFocus = 1;
				if (rel < 0)
					rel = 0;
				if (f) {
					while (s_query[i]) {
						int cw = ChFontGetWidthChar(f, s_query[i]);
						if (w + cw / 2 > rel)
							break;
						w += cw;
						i++;
					}
				} else {
					i = strlen(s_query);
				}
				s_qCursor = i;
				NsPageRender();
			} else if (s_btnHave && lx >= s_btnX && lx < s_btnX + s_btnW && ly >= s_btnY &&
					   ly < s_btnY + s_btnH) {
				NsSubmitQuery(0);
			} else if (s_luckyHave && lx >= s_luckyX && lx < s_luckyX + s_luckyW &&
					   ly >= s_luckyY && ly < s_luckyY + s_luckyH) {
				NsSubmitQuery(1);
			} else if (button) {
				int hit = -1;
				for (int li = 0; li < s_nlinks; li++) {
					if (!s_links[li].have)
						continue;
					if (lx >= s_links[li].x && lx < s_links[li].x + s_links[li].w &&
						ly >= s_links[li].y && ly < s_links[li].y + s_links[li].h) {
						hit = li;
						break;
					}
				}
				if (hit >= 0) {
					const char* h = s_links[hit].href;
					char abs[NS_URL_MAX];
					if (!strncmp(h, "https://", 8) || !strncmp(h, "http://", 7)) {
						snprintf(abs, sizeof(abs), "%s", h);
					} else if (h[0] == '/' && h[1] == '/') {
						snprintf(abs, sizeof(abs), "https:%s", h);
					} else if (h[0] == '/') {
						const char* sc = strstr(s_url, "://");
						const char* host = sc ? sc + 3 : s_url;
						const char* slash = strchr(host, '/');
						size_t origin = slash ? (size_t)(slash - s_url) : strlen(s_url);
						if (origin + strlen(h) + 1 < sizeof(abs)) {
							memcpy(abs, s_url, origin);
							strcpy(abs + origin, h);
						} else {
							snprintf(abs, sizeof(abs), "%s", h);
						}
					} else {
						snprintf(abs, sizeof(abs), "%s", h);
					}
					snprintf(s_url, sizeof(s_url), "%s", abs);
					s_urlCursor = strlen(s_url);
					NsShowUrl();
					NsFetch(s_url);
				} else if (s_qFocus) {
					s_qFocus = 0;
					NsPageRender();
				}
			}
		}
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	case DEODHAI_REPLY_KEY_EVENT: {
		int code = e->dword;
		ChitralekhaProcessKey(code);
		NsHandleKey(code);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	case DEODHAI_REPLY_FOCUS_CHANGED: {
		int focus_val = e->dword;
		int handle = e->dword2;
		ChWindow* focWin = ChGetWindowByHandle(s_mainWin, handle);
		if (focWin)
			ChWindowHandleFocus(focWin, focus_val, handle);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	default:
		memset(e, 0, sizeof(PostEvent));
		break;
	}
}

int main(int argc, char* argv[]) {
	/* JS sink is process-wide: both the GUI page pass and the headless
	 * --selftest run route console/document.write output through it. */
	NsJsSetSink(NsJsSink);
	NsLayoutSetImageLookup(NsImageLookup);
	NsLayoutSetInlineSvgLookup(NsInlineSvgLookup);

	/* Headless pipeline self-test: fetch + CSS + render with serial
     * markers, no GUI. Xeneva drops the program name: the app sees only
     * user args, so scan everything (argv[0] is already "--selftest"
     * under init's TERM runner). */
	for (int a = 0; a < argc; a++) {
		if (argv[a] &&
			(!strcmp(argv[a], "--selftest") || !strcmp(argv[a], "selftest")) &&
			a + 1 < argc && argv[a + 1] && argv[a + 1][0]) {
			char turl[NS_URL_MAX];
			strncpy(turl, argv[a + 1], sizeof(turl) - 1);
			turl[sizeof(turl) - 1] = 0;
			_KePrint("[cssselftest] fetch %s\n", turl);
			int trc = -1;
			for (int retry = 0; retry < 6 && trc != 0; retry++) {
				if (retry > 0) {
					_KePrint("[cssselftest] retry %d\n", retry);
					_KeProcessSleep(2000);
				}
				trc = NsFetch(turl);
			}
			if (trc != 0) {
				_KePrint("[cssselftest] FAIL: fetch never completed\n");
				return 1;
			}
			/* "click" runs the checkbox once, then returns. Lengths only:
			 * the page text can carry a continue link. */
			int do_click = 0;
			for (int b = 0; b < argc; b++) {
				if (argv[b] && !strcmp(argv[b], "click"))
					do_click = 1;
			}
			_KePrint("[cssselftest] done, text %d bytes\n", (int)strlen(s_text));
			if (do_click) {
				if (s_layoutActive) {
					int nb = 0, nl = 0, nr = 0, nk = 0;
					NsLayoutStats(&nb, &nl, &nr, &nk);
					_KePrint("[layouttest] text=%d blocks=%d lines=%d runs=%d links=%d content=%d\n",
							(int)strlen(NsLayoutText()), nb, nl, nr, nk, NsLayoutContentHeight());
				} else {
					_KePrint("[layouttest] no layout (fallback renderer)\n");
				}
				if (NsCaptchaPage(s_body)) {
					_KePrint("[selftest] click\n");
					NsCaptchaActivate();
					_KePrint("[selftest] clicked\n");
				} else {
					_KePrint("[selftest] nocaptcha\n");
				}
				_KePrint("[cssselftest] PASS\n");
				return 0;
			}
			{
				char head[121];
				strncpy(head, s_text, 120);
				head[120] = 0;
				_KePrint("%s\n", head);
			}
			if (s_layoutActive) {
				int nb = 0, nl = 0, nr = 0, nk = 0;
				const char* lt = NsLayoutText();
				NsLayoutStats(&nb, &nl, &nr, &nk);
				_KePrint("[layouttest] text=%d blocks=%d lines=%d runs=%d links=%d content=%d\n",
						(int)strlen(lt), nb, nl, nr, nk, NsLayoutContentHeight());
				if (lt[0]) {
					char lhead[161];
					strncpy(lhead, lt, 160);
					lhead[160] = 0;
					_KePrint("[layouttest] first: %s\n", lhead);
				}
				NsLayoutPrintLinks(8);
				NsLayoutDumpBoxes();
				{
					int top = NsLayoutPaintTest(632, 420, 0);
					int bottom = NsLayoutPaintTest(632, 420,
							NsLayoutContentHeight() > 420 ? NsLayoutContentHeight() - 420 : 0);
					_KePrint("[painttest] top_nonbg=%d bottom_nonbg=%d (%s)\n", top, bottom,
							top > 0 && bottom > 0 ? "PASS" : "FAIL");
					{
						int d1 = NsLayoutDumpPpm("/data/ltop.ppm", 632, 420, 0);
						int d2 = NsLayoutDumpPpm("/data/lbot.ppm", 632, 420,
								NsLayoutContentHeight() > 420 ? NsLayoutContentHeight() - 420 : 0);
						_KePrint("[dumptest] top=%d bottom=%d\n", d1, d2);
					}
				}
				_KePrint("[layouttest] PASS\n");
			} else {
				_KePrint("[layouttest] no layout (fallback renderer)\n");
			}
			_KePrint("[cssselftest] PASS\n");
			return 0;
		}
	}
	for (int a = 0; a < argc; a++) {
		if (argv[a] && argv[a][0] && argv[a][0] != '-') {
			strncpy(s_url, argv[a], sizeof(s_url) - 1);
			s_url[sizeof(s_url) - 1] = 0;
			break;
		}
	}
	s_urlCursor = strlen(s_url);

	s_app = ChitralekhaStartApp(argc, argv);
	s_mainWin = ChCreateWindow(s_app, WINDOW_FLAG_MOVABLE, "NetSurf", 100, 80, 640, 480);
	s_mainWin->color = 0xFF252525;

	s_urlBox = ChCreateTextBox(s_mainWin, 4, 2, 640 - 92, 24);
	s_urlBox->editable = true;
	s_urlBox->textBackgroundColor = 0xFFFFFFFF;
	s_urlBox->textColor = 0xFF000000;
	ChFont* font = ChInitialiseFont(CALIBRI);
	ChTextBoxSetFont(s_urlBox, font);
	ChTextBoxSetFontSize(s_urlBox, 14);
	s_urlBox->wid.ChPaintHandler = NsUrlPaint;
	s_urlBox->wid.ChMouseEvent = NsUrlMouseEvent;
	ChWindowAddWidget(s_mainWin, (ChWidget*)s_urlBox);

	ChButton* go = ChCreateButton(640 - 82, 2, 74, 24, (char*)"Go");
	go->base.ChActionHandler = NsGoHandler;
	ChWindowAddWidget(s_mainWin, (ChWidget*)go);

	s_pageBox = ChCreateTextBox(s_mainWin, 4, 30, 640 - 8, 480 - 60);
	s_pageBox->editable = false;
	s_pageBox->textBackgroundColor = s_mainWin->color;
	s_pageBox->textColor = WHITE;
	ChTextBoxSetFont(s_pageBox, font);
	ChTextBoxSetFontSize(s_pageBox, 14);
	s_pageBox->wid.ChPaintHandler = NsPagePaint;
	ChWindowAddWidget(s_mainWin, (ChWidget*)s_pageBox);

	ChWindowBroadcastIcon(s_app, "/icons/netsurf.bmp");

	/* Second size bucket for CSS-enlarged runs (headings). Per-object
     * atlases, so sizes coexist safely. */
	s_fontBig = ChInitialiseFont(CALIBRI);
	if (s_fontBig)
		ChFontSetSize(s_fontBig, 20);

	NsShowUrl();
	NsShowStatus("NetSurf for XenevaOS (bootstrap).\n\nType a host like example.com,\n"
				 "press Enter or Go.\n\nLeft/Right/Home/End move in the URL bar.\n"
				 "Up/Down/PgUp/PgDn scroll the page.\n\n"
				 "A name with a dot opens that site.\n"
				 "Words with no dot search Google.\n");

	ChWindowPaint(s_mainWin);

	/* A URL passed on the command line opens immediately; the box is
	 * already populated, so this only saves the user pressing Enter. */
	if (s_url[0])
		NsFetch(s_url);

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));

	setjmp(s_mainWin->jump);
	while (1) {
		int err = _KeFileIoControl(s_app->postboxfd, POSTBOX_GET_EVENT, &e);
		WindowHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT) {
			/* Idle: fire due page timers and promise jobs. A script that
			 * navigates is fetched here once the callback has returned. */
			int stepped = NsJsTick();
			char navraw[NS_URL_MAX];
			char navabs[NS_URL_MAX];
			if (NsJsConsumeNav(navraw, sizeof(navraw))) {
				NsResolveUrl(s_docUrl[0] ? s_docUrl : s_url, navraw, navabs, sizeof(navabs));
				if (navabs[0] &&
					(!strncmp(navabs, "https://", 8) || !strncmp(navabs, "http://", 7))) {
					snprintf(s_url, sizeof(s_url), "%s", navabs);
					s_urlCursor = strlen(s_url);
					NsShowUrl();
					NsLogNav(s_url);
					NsFetch(s_url);
				}
			} else if (stepped) {
				if (NsJsConsumeDirty() && s_domRoot) {
					_KePrint("[js] restyle\n");
					NsCssReselectTree(s_domRoot);
					if (NsLayoutBuild(s_domRoot,
									  s_pageBox ? s_pageBox->wid.w : 632,
									  s_pageBox ? s_pageBox->wid.h : 420) == 0)
						s_layoutActive = 1;
					else
						s_layoutActive = 0;
				}
				NsPageRender();
			}
			_KeProcessSleep(10);
		}
	}
	return 0;
}
