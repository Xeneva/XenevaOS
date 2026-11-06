/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

/*
 * cards.cpp -- one AHEntryList card.
 *
 *   paper card                      solid card / hero
 *   +---------------------------+   +---------------------------+
 *   | <> KEEP              [ic] |   | <> LISTEN . NOW PLAYING  :|
 *   |                           |   | [icon]                    |
 *   | files                     |   | accent                    |
 *   | everything you keep    ** |   | Asilu Kotenu . Zubeen     |
 *   +---------------------------+   +=====progress==============+
 *
 * A tiny lozenge leads the kicker; a faint phul is woven into the paper
 * corner. On hover two threads weave in under the name (left to right),
 * the kicker steps right and the corner phul becomes the unpin knot.
 *
 * Painting is O(card pixels). Text comes from the glyph sets, icons are
 * pre-scaled; nothing here allocates.
 */

#include "launcher.h"
#include <livetile.h>
#include <string.h>

static const uint16_t _phul[9] = { 0x010, 0x038, 0x092, 0x0D6, 0x1FF, 0x0D6, 0x092, 0x038, 0x010 };

static void _phul_at(ChCanvas* c, int x, int y, int s, uint32_t col, uint32_t a, const XlRect* clip) {
	for (int r = 0; r < 9; r++)
		for (int cc = 0; cc < 9; cc++)
			if (_phul[r] & (1u << (8 - cc)))
				XlFillAlpha(c, x + cc * s, y + r * s, s, s, col, a, clip);
}

typedef struct {
	uint32_t bg, ink, soft, kick, thread, thread2, live;
} CardInk;

static CardInk _ink(const XlCard* k, const XlApp* a) {
	CardInk i;
	if (k->style == XL_STYLE_PAPER) {
		i.bg = XL_PAPER;
		i.ink = XL_INK;
		i.soft = XL_INK_SOFT;
		i.kick = XL_RED;
		i.thread = XL_RED;
		i.thread2 = XL_WEAVE_LINE;
		i.live = XL_RED_DEEP;
	} else {
		i.bg = XlTileColor(a->color);
		i.ink = XL_WHITE;
		i.soft = XlBlend(i.bg, XL_WHITE, 200);
		i.kick = XL_WHITE;
		i.thread = XL_WHITE;
		i.thread2 = XlBlend(i.bg, XL_WHITE, 150);
		i.live = XL_WHITE;
	}
	return i;
}

void XlCardUnpinRect(const XlCard* k, int x, int y, XlRect* r) {
	r->x = x + k->w - 34;
	r->y = y + k->h - 34;
	r->w = 30;
	r->h = 30;
}

void XlCardDirtyRect(const XlCard* k, XlRect* r) {
	XlCardRect(k, r);
	r->y -= 3;
	r->w += 4;
	r->h += 9;
}

/* the unpin knot: a stitched lozenge with a cross, faded in by hover */
static void _unpin_knot(ChCanvas* c, int cx, int cy, uint32_t col, uint32_t mark, uint32_t a, const XlRect* clip) {
	for (int dy = -8; dy <= 8; dy++) {
		int hw = 8 - (dy < 0 ? -dy : dy);
		XlFillAlpha(c, cx - hw, cy + dy, 2 * hw + 1, 1, col, a, clip);
	}
	for (int i = -3; i <= 3; i++) {
		XlFillAlpha(c, cx + i, cy + i, 1, 1, mark, a, clip);
		XlFillAlpha(c, cx + i, cy - i, 1, 1, mark, a, clip);
	}
}

/* old live line slides up and out, the new one slides in from below */
static void _ticker(ChCanvas* c, const XlApp* a, int x, int base, int w, uint32_t col, uint32_t fade,
					const XlRect* clip) {
	XlRect lc = { x, base - 14, w, 19 };
	if (!XlClipRect(c, &lc, clip))
		return;
	char now[sizeof(a->tickOld)], buf[sizeof(a->tickOld)];
	XlLiveLine(a, now, sizeof(now));
	if (a->tick >= 255 || !a->tickOld[0]) {
		XlTextFit(XL_GS_BODY, now, w, buf, sizeof(buf));
		XlText(c, XL_GS_BODY, buf, x, base, col, fade, &lc);
		return;
	}
	int t = a->tick; /* ease in-out on 0..255 */
	int e = t < 128 ? (2 * t * t) / 255 : 255 - (2 * (255 - t) * (255 - t)) / 255;
	int off = 19 * e / 255;
	XlTextFit(XL_GS_BODY, a->tickOld, w, buf, sizeof(buf));
	XlText(c, XL_GS_BODY, buf, x, base - off, col, fade, &lc);
	XlTextFit(XL_GS_BODY, now, w, buf, sizeof(buf));
	XlText(c, XL_GS_BODY, buf, x, base + 19 - off, col, fade, &lc);
}

void XlCardPaint(ChCanvas* c, const XlCard* k, int x, int y, int reveal, uint32_t fade, bool pressed,
				 const XlRect* clip) {
	const XlApp* a = &xlnch.apps[k->app];
	int w = k->w, h = k->h;
	int vis = reveal >= 256 ? h : (h * reveal) >> 8;
	if (vis <= 0)
		return;
	XlRect cc = { x, y, w, vis };
	if (!XlClipRect(c, &cc, clip))
		return;
	CardInk in = _ink(k, a);
	XlFill(c, x, y, w, vis, in.bg, &cc);
	if (k->style == XL_STYLE_PAPER)
		XlFrame(c, x, y, w, vis < h ? vis + 1 : h, 1, XL_EDGE, &cc);
	uint32_t hov = a->hov;
	/* a low hero (small screens) moves its icon up to the corner, so the
	 * big thin name never runs into it */
	bool lowHero = k->kind == XL_CARD_HERO && h < 160;

	/* woven marks */
	if (k->kind == XL_CARD_HERO && !lowHero)
		_phul_at(c, x + w - 46, y + 14, 3, XL_WHITE, 46, &cc);
	else if (lowHero)
		_phul_at(c, x + w - 38, y + h - 34, 2, XL_WHITE, 46, &cc);
	else if (k->style == XL_STYLE_PAPER && hov < 255)
		_phul_at(c, x + w - 30, y + h - 30, 2, XL_FAINT, 255 - hov, &cc);

	/* kicker: a lozenge, then the label; steps right on hover */
	int kx = x + 16 + (int)(6 * hov / 255);
	XlLozenge(c, kx + 3, y + 24, 3, in.kick, &cc);
	char kick[XL_KICK_MAX + 16];
	strcpy(kick, a->kicker[0] ? a->kicker : "OPEN");
	if (a->live.valid && a->live.layout == XELNCH_TILE_NOWPLAYING) {
		size_t n = strlen(kick);
		if (n + 15 < sizeof(kick))
			strcpy(kick + n, "  .  NOW PLAYING");
	}
	/* the icon's left edge bounds the kicker */
	bool smallCard = h < 100;
	int ps = smallCard ? 28 : 36; /* icon plate */
	const XlIcon* solidIcon = (smallCard || !a->big.px) ? &a->small : &a->big;
	int iconLeft = k->style == XL_STYLE_PAPER ? x + w - ps - 14 : x + w - 16 - solidIcon->w;
	if (k->kind == XL_CARD_HERO && !lowHero)
		iconLeft = x + w - 50;
	char kfit[XL_KICK_MAX + 20];
	XlTextFit(XL_GS_SMALL, kick, iconLeft - 6 - (kx + 10), kfit, sizeof(kfit));
	int kw = XlText(c, XL_GS_SMALL, kfit, kx + 10, y + 28, in.kick, fade, &cc);
	if (a->live.valid && a->live.badge) {
		char num[8];
		int b = a->live.badge > 999 ? 999 : a->live.badge;
		int n = 0;
		if (b >= 100)
			num[n++] = (char)('0' + b / 100);
		if (b >= 10)
			num[n++] = (char)('0' + (b / 10) % 10);
		num[n++] = (char)('0' + b % 10);
		num[n] = 0;
		int bw = XlTextWidth(XL_GS_SMALL, num) + 10;
		int bx = kx + 10 + kw + 8;
		if (bx + bw > iconLeft - 4)
			bx = iconLeft - 4 - bw;
		XlFillAlpha(c, bx, y + 16, bw, 16, k->style == XL_STYLE_PAPER ? XL_CHARCOAL : XL_WHITE, fade, &cc);
		XlText(c, XL_GS_SMALL, num, bx + 5, y + 28, k->style == XL_STYLE_PAPER ? XL_WHITE : in.bg, fade, &cc);
	}

	/* icon */
	int iconBottom = y + 50;
	if (k->kind == XL_CARD_HERO && !lowHero) {
		const XlIcon* ic = a->big.px ? &a->big : &a->small;
		XlIconDraw(c, ic, x + 16, y + 40, fade, &cc);
		iconBottom = y;
	} else if (lowHero) {
		const XlIcon* ic = a->big.px ? &a->big : &a->small;
		XlIconDraw(c, ic, x + w - 16 - ic->w, y + 12, fade, &cc);
		iconBottom = y + 12 + ic->h;
	} else if (k->style == XL_STYLE_PAPER) {
		int px = iconLeft, py = y + (smallCard ? 10 : 14);
		XlFillAlpha(c, px, py, ps, ps, XlTileColor(a->color), fade, &cc);
		if (a->color == XL_COL_IVORY)
			XlFrame(c, px, py, ps, ps, 1, XL_EDGE, &cc);
		XlIconDraw(c, &a->small, px + (ps - a->small.w) / 2, py + (ps - a->small.h) / 2, fade, &cc);
		iconBottom = py + ps;
	} else {
		int py = y + (smallCard ? 10 : 14);
		XlIconDraw(c, solidIcon, iconLeft, py, fade, &cc);
		iconBottom = py + solidIcon->h;
	}

	/* name and deck */
	int set, nameBase, deckBase;
	if (k->kind == XL_CARD_HERO) {
		set = XL_GS_THIN;
		nameBase = y + h - 42;
		deckBase = y + h - 18;
	} else if (k->kind == XL_CARD_TALL) {
		set = XL_GS_NAME_L;
		nameBase = y + h - 42;
		deckBase = y + h - 18;
	} else {
		set = XL_GS_NAME_S;
		nameBase = y + h - 30;
		deckBase = y + h - 12;
	}
	int maxW = w - 28;
	if ((k->kind != XL_CARD_HERO || lowHero) && nameBase - XlTextAscent(set) < iconBottom + 2)
		maxW = w - 70; /* the name shares its line with the icon */
	char name[XL_TITLE_MAX + 4];
	XlTextFit(set, a->name, maxW, name, sizeof(name));
	int nw = XlText(c, set, name, x + 14, nameBase, in.ink, fade, &cc);
	if (hov) {
		int tw = nw * (int)hov / 255;
		XlFill(c, x + 14, nameBase + 7, tw, 2, in.thread, &cc);
		XlFill(c, x + 14, nameBase + 11, tw * 3 / 5, 1, in.thread2, &cc);
	}

	char line[XL_LIVE_TEXT_MAX * 2 + 8];
	int deckW = w - 32 - (k->style == XL_STYLE_PAPER ? 24 : 0);
	if (k->kind == XL_CARD_HERO) {
		if (a->live.valid && a->live.line1[0]) {
			strcpy(line, a->live.line1);
			if (a->live.line2[0]) {
				strcat(line, "  .  ");
				strcat(line, a->live.line2);
			}
		} else {
			strcpy(line, a->deck);
		}
		char fit[sizeof(line)];
		XlTextFit(XL_GS_BODY, line, deckW, fit, sizeof(fit));
		XlText(c, XL_GS_BODY, fit, x + 16, deckBase, in.ink, fade, &cc);
		if (a->live.valid && a->live.progress != XELNCH_NO_PROGRESS && vis >= h) {
			int p = a->live.progress > 1000 ? 1000 : a->live.progress;
			XlFill(c, x, y + h - 4, w, 4, XlBlend(in.bg, XL_WHITE, 110), &cc);
			XlFill(c, x, y + h - 4, w * p / 1000, 4, XL_WHITE, &cc);
		}
	} else if (a->live.valid && a->live.line1[0]) {
		if (k->kind == XL_CARD_TALL && h >= 150) {
			/* room for two lines under the kicker: what, then its detail
			 * (the calendar: "today 18:30" / "Bihu practice"); the deck stays */
			int lw = w - 32;
			char l[XL_LIVE_TEXT_MAX + 4];
			if (a->tick >= 255 || !a->tickOld[0]) {
				XlTextFit(XL_GS_BODY, a->live.line1, iconLeft - 8 - (x + 16), l, sizeof(l));
				XlText(c, XL_GS_BODY, l, x + 16, y + 66, in.live, fade, &cc);
				XlTextFit(XL_GS_MID, a->live.line2, lw, l, sizeof(l));
				XlText(c, XL_GS_MID, l, x + 16, y + 88, in.ink, fade, &cc);
			} else {
				_ticker(c, a, x + 16, y + 66, iconLeft - 8 - (x + 16), in.live, fade, &cc);
			}
			XlTextFit(XL_GS_BODY, a->deck, deckW, line, sizeof(line));
			XlText(c, XL_GS_BODY, line, x + 16, deckBase, in.soft, fade, &cc);
		} else {
			_ticker(c, a, x + 16, deckBase, deckW, in.live, fade, &cc);
		}
	} else {
		XlTextFit(XL_GS_BODY, a->deck, deckW, line, sizeof(line));
		XlText(c, XL_GS_BODY, line, x + 16, deckBase, in.soft, fade, &cc);
	}

	/* hover: the corner phul turns into the unpin knot */
	if (hov && k->kind != XL_CARD_HERO) {
		uint32_t kc = k->style == XL_STYLE_PAPER ? XL_RED : XL_WHITE;
		uint32_t mc = k->style == XL_STYLE_PAPER ? XL_WHITE : in.bg;
		_unpin_knot(c, x + w - 19, y + h - 19, kc, mc, hov, &cc);
	} else if (hov && k->kind == XL_CARD_HERO) {
		_unpin_knot(c, x + w - 19, y + h - 21, XL_WHITE, in.bg, hov, &cc);
	}
	if (pressed)
		XlFrame(c, x, y, w, vis, 2, k->style == XL_STYLE_PAPER ? XL_RED : XL_WHITE, &cc);
}
