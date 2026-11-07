#include "custom_btn.h"
#include <stdlib.h>
#include <string.h>
#include <widgets/button.h>

void _custom_btn_paint(ChWidget* wid, ChWindow* win) {
	CustomBtn* btn = (CustomBtn*)wid;

	uint32_t bg;
	if (btn->normalBg != 0) {
		bg = btn->base.clicked ? btn->clickedBg : (btn->base.hover ? btn->hoverBg : btn->normalBg);
	} else {
		// Default toolbar dark theme
		bg = btn->base.clicked ? 0xFF1A1A1A : (btn->base.hover ? 0xFF3D3D3D : 0xFF2D2D2D);
	}
	ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, bg);

	if (btn->borderColor != 0) {
		ChDrawRectUnfilled(win->canv, wid->x, wid->y, wid->w, wid->h, btn->borderColor);
	}

	if (btn->isColorBtn) {
		// Draw the color square with a white outline
		ChDrawRect(win->canv, wid->x + 6, wid->y + 6, wid->w - 12, wid->h - 12, btn->colorSquare);
		ChDrawRectUnfilled(win->canv, wid->x + 5, wid->y + 5, wid->w - 10, wid->h - 10, 0xFFFFFFFF);
	} else {
		ChRect clip;
		clip.x = wid->x;
		clip.y = wid->y;
		clip.w = wid->w;
		clip.h = wid->h;

		int textW = ChFontGetWidth(win->app->baseFont, (char*)btn->text);
		int tx = wid->x + (wid->w - textW) / 2;
		int fontSize = (btn->normalBg != 0) ? 12 : 14;
		ChFontSetSize(win->app->baseFont, fontSize);
		int ty = wid->y + (wid->h / 2) + (fontSize / 2) - 1;
		uint32_t tColor = /*(btn->normalBg != 0) ? btn->textColor :*/ 0xFFFFFFFF;
		ChFontDrawTextClipped(
			win->canv, win->app->baseFont, (char*)btn->text, tx, ty, tColor, &clip);
		if (strcmp(btn->text, "B") == 0) {
			// Fake bold by drawing again offset by 1 pixel
			ChFontDrawTextClipped(
				win->canv, win->app->baseFont, (char*)btn->text, tx + 1, ty, tColor, &clip);
		}
	}
}

void _custom_btn_mouse(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	CustomBtn* btn = (CustomBtn*)wid;
	if (button == 1) {
		btn->base.clicked = true;
	} else if (button == 0) {
		if (btn->base.clicked && btn->base.ChActionHandler) {
			btn->base.ChActionHandler(wid, win);
		}
		btn->base.clicked = false;
	}
	if (wid->ChPaintHandler) {
		wid->ChPaintHandler(wid, win);
		ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
	}
}

CustomBtn*
CreateCustomBtn(int x, int y, int w, int h, const char* text, uint32_t colorSq, bool isCol) {
	CustomBtn* btn = (CustomBtn*)malloc(sizeof(CustomBtn));
	memset(btn, 0, sizeof(CustomBtn));
	btn->base.x = x;
	btn->base.y = y;
	btn->base.w = w;
	btn->base.h = h;
	btn->base.ChPaintHandler = _custom_btn_paint;
	btn->base.ChMouseEvent = _custom_btn_mouse;
	btn->text = text;
	btn->colorSquare = colorSq;
	btn->isColorBtn = isCol;
	return btn;
}

CustomBtn* CreateStyledBtn(int x,
						   int y,
						   int w,
						   int h,
						   const char* text,
						   uint32_t normalBg,
						   uint32_t hoverBg,
						   uint32_t clickedBg,
						   uint32_t textColor,
						   uint32_t borderColor) {
	CustomBtn* btn = (CustomBtn*)malloc(sizeof(CustomBtn));
	memset(btn, 0, sizeof(CustomBtn));
	btn->base.x = x;
	btn->base.y = y;
	btn->base.w = w;
	btn->base.h = h;
	btn->base.ChPaintHandler = _custom_btn_paint;
	btn->base.ChMouseEvent = _custom_btn_mouse;
	btn->text = text;
	btn->normalBg = normalBg;
	btn->hoverBg = hoverBg;
	btn->clickedBg = clickedBg;
	btn->textColor = textColor;
	btn->borderColor = borderColor;
	return btn;
}
