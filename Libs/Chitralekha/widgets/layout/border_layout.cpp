/**
* @file border_layout.cpp
* 
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

#include "border_layout.h"

/**
 * @brief CH_BL_Item -- get a border item at given position
 * @param l -- Pointer to Border Layout
 * @param i -- index position
 */
static ChBorderItem* CH_BL_Item(ChBorderLayout* l, size_t i) {
	return (ChBorderItem*)list_get_at(l->items, i);
}

#define GET_NUM_BORDER_ITEM_COUNT(l) ((l)->items->pointer)

/**
 * @brief CH_BL_Find -- find a widget by its alignment
 * @param l -- Pointer to Border Layout
 * @param a -- ALIGNMENT value
 */
static ChWidget* CH_BL_Find(ChBorderLayout* l, ChBorderAlignment a) {
	ChWidget* found = NULL;
	for (size_t i = 0; i < GET_NUM_BORDER_ITEM_COUNT(l); i++) {
		ChBorderItem* it = CH_BL_Item(l, i);
		if (it->align == a)
			found = it->widget;
	}
	return found;
}

/**
 * @brief CH_BL_FindItem -- find a widget by its alignment
 * @param l -- Pointer to Border Layout
 * @param a -- ALIGNMENT value
 */
static ChBorderItem* CH_BL_FindItem(ChBorderLayout* l, ChBorderAlignment a) {
	ChBorderItem* found = NULL;
	for (size_t i = 0; i < GET_NUM_BORDER_ITEM_COUNT(l); i++) {
		ChBorderItem* it = CH_BL_Item(l, i);
		if (it->align == a)
			found = it;
	}
	return found;
}

static int ChBL_Clamp(int v, int lo, int hi) {
	if (hi < lo)
		hi = lo;
	return v < lo ? lo : (v > hi ? hi : v);
}

void ChBorderLayoutDoLayout(ChBorderLayout* l) {
	ChBorderItem* top = CH_BL_FindItem(l, CH_BORDER_ALIGN_TOP);
	ChBorderItem* bottom = CH_BL_FindItem(l, CH_BORDER_ALIGN_BOTTOM);
	ChBorderItem* left = CH_BL_FindItem(l, CH_BORDER_ALIGN_LEFT);
	ChBorderItem* right = CH_BL_FindItem(l, CH_BORDER_ALIGN_RIGHT);
	ChBorderItem* center = CH_BL_FindItem(l, CH_BORDER_ALIGN_CENTER);

	int x = l->base.x, y = l->base.y;
	int w = l->base.w, h = l->base.h;

	int bar_fallback = l->defBarH ? l->defBarH : h / 8;
	int side_fallback = l->defSideW ? l->defSideW : w / 4;

	int curY = y, curBottom = y + h;
	int curX = x, curRight = x + w;

	int topH = 0, botH = 0, leftW = 0, rightW = 0;

	/** NORTH : following java.swing styled border layout */
	if (top) {
		int h = topH = top->prefH > 0 ? top->prefH : bar_fallback;
		h = ChBL_Clamp(h, 0, curBottom - curY);
		top->widget->x = x;
		top->widget->y = curY;
		top->widget->w = w;
		top->widget->h = h;
		curY += h + l->vgap;
	}

	if (bottom) {
		int h = bottom->prefH > 0 ? bottom->prefH : bar_fallback;
		h = ChBL_Clamp(h, 0, curBottom - curY);
		bottom->widget->x = x;
		bottom->widget->w = w;
		bottom->widget->h = h;
		bottom->widget->y = curBottom - h;
		curBottom -= h + l->vgap;
	}

	int midY = curY;
	int midH = curBottom - curY;
	if (midH < 0)
		midH = 0;

	if (left) {
		int w = left->prefW > 0 ? left->prefW : side_fallback;
		w = ChBL_Clamp(w, 0, curRight - curX);
		left->widget->x = curX;
		left->widget->y = midY;
		left->widget->w = w;
		left->widget->h = midH;
		curX += w + l->hgap;
	}

	if (right) {
		int w = right->prefW > 0 ? right->prefW : side_fallback;
		w = ChBL_Clamp(w, 0, curRight - curX);
		right->widget->w = w;
		right->widget->h = midH;
		right->widget->w = curRight - w;
		right->widget->y = midY;
		curRight -= w + l->hgap;
	}

	if (center) {
		int w = curRight - curX;
		center->widget->x = curX;
		center->widget->y = midY;
		center->widget->w = w > 0 ? w : 0;
		center->widget->h = midH;
	}

	/* nested layouts*/
	for (size_t i = 0; i < GET_NUM_BORDER_ITEM_COUNT(l); i++) {
		ChWidget* w = CH_BL_Item(l, i)->widget;
		if (w->type == CH_WIDEGT_BORDERLAYOUT)
			ChBorderLayoutDoLayout((ChBorderLayout*)w);
	}
}

/**
 * @brief ChBorderLayoutSetPreferred -- set preferred border 
 * width and height for widget geometry
 * @param l -- Pointer to Border Layout
 * @param w -- Pointer to widget
 * @param pw -- Preferred width
 * @param ph -- preferred height
 */
void ChBorderLayoutSetPreferred(ChBorderLayout* l, ChWidget* w, int pw, int ph) {
	for (size_t i = 0; i < GET_NUM_BORDER_ITEM_COUNT(l); i++) {
		ChBorderItem* it = CH_BL_Item(l, i);
		if (it->widget == w) {
			it->prefW = pw;
			it->prefH = ph;
			break;
		}
	}
}

/**
 * @brief ChBorderLayoutSetDefaults -- set default border geometry
 * @param l -- Pointer to Border Layout
 * @param sideW -- side width
 * @param barH -- bar height
 */
void ChBorderLayoutSetDefaults(ChBorderLayout* l, int sideW, int barH) {
	l->defSideW = sideW;
	l->defBarH = barH;
	ChBorderLayoutDoLayout(l);
}

/**
 * @brief ChBorderLayoutAdd -- add a widget to the border layout
 * by its alignment
 * @param l -- Pointer to border layout
 * @param w -- Pointer to the widget to add
 * @param align -- alignment number
 */
void ChBorderLayoutAdd(ChBorderLayout* l, ChWidget* w, ChBorderAlignment align) {
	ChBorderItem* it = (ChBorderItem*)malloc(sizeof(ChBorderItem));
	it->widget = w;
	it->align = align;
	it->prefW = w->w;
	it->prefH = w->h;
	list_add(l->items, it);
	ChBorderLayoutDoLayout(l);
}

/**
 * @brief ChBorderLayoutResize -- resize the border layout with given 
 * parameter 
 * @param l -- Pointer to border layout
 * @param x -- x location of the border layout
 * @param y -- y location of the border layout
 * @param w -- new width of the border layout
 * @param h -- new height of the border layout
 */
void ChBorderLayoutResize(ChBorderLayout* l, int x, int y, int w, int h) {
	l->base.x = x;
	l->base.y = y;
	l->base.w = w;
	l->base.h = h;
	ChBorderLayoutDoLayout(l);
}

static ChWidget* ChBLHitTest(ChBorderLayout* l, ChWindow* win, int x, int y) {
	for (size_t i = GET_NUM_BORDER_ITEM_COUNT(l); i-- > 0;) {
		ChWidget* w = CH_BL_Item(l, i)->widget;
		if (x >= win->info->x + w->x && x < win->info->x + w->x + w->w &&
			y >= win->info->y + w->y && y < win->info->y + w->y + w->h)
			return w;
	}
	return NULL;
}

static void ChBLPaint(ChWidget* self, ChWindow* win) {
	ChBorderLayout* l = (ChBorderLayout*)self;
	for (size_t i = 0; i < GET_NUM_BORDER_ITEM_COUNT(l); i++) {
		ChWidget* w = CH_BL_Item(l, i)->widget;
		if (w->ChPaintHandler)
			w->ChPaintHandler(w, win);
	}
}

static void ChBL_Mouse(ChWidget* self, ChWindow* win, int x, int y, int button) {
	ChBorderLayout* l = (ChBorderLayout*)self;
	ChWidget* hit = ChBLHitTest(l, win, x, y);

	for (size_t i = 0; i < GET_NUM_BORDER_ITEM_COUNT(l); i++) {
		ChWidget* w = CH_BL_Item(l, i)->widget;
		if (w != hit && w->hover) {
			w->hover = false;
			w->hoverPainted = false;
			if (w->ChPaintHandler)
				w->ChPaintHandler(w, win);
		}
	}

	if (hit && hit->ChMouseEvent) {
		hit->hover = self->hover;
		hit->KillFocus = self->KillFocus;
		if (hit->ChMouseEvent)
			hit->ChMouseEvent(hit, win, x, y, button);
	}
}

static void ChBL_Touch(ChWidget* self, ChWindow* win, int x, int y) {
	//not implemented
	return;
}

static void ChBL_Scroll(ChWidget* self, ChWindow* win, int val, uint8_t type) {
	//not implemented
	return;
}

/**
 * @brief ChBL_Destroy -- destroy the border layout and all
 * child layout
 * @param self -- Pointer to the Border Layout Widget
 * @param win -- Pointer to the backing Window
 */
static void ChBL_Destroy(ChWidget* self, ChWindow* win) {
	ChBorderLayout* l = (ChBorderLayout*)self;
	while (l->items->pointer) {
		ChBorderItem* it = (ChBorderItem*)list_remove(l->items, 0);
		if (it->widget->ChDestroy)
			it->widget->ChDestroy(it->widget, win);
		free(it);
	}

	free(l->items);
	free(l);
}

/**
 * @brief ChCreateBorderLayout -- create a new border layout with given parameters
 * @param x -- Border Layout X position
 * @param y -- Border Layout Y position
 * @param w -- Width of the Border Layout
 * @param h -- height of the Border Layout
 * @param hgap -- Horizontal padding between each alignment
 * @param vgap -- Verical padding between each alignment
 */
ChBorderLayout* ChCreateBorderLayout(int x, int y, int w, int h, int hgap, int vgap) {
	ChBorderLayout* l = (ChBorderLayout*)malloc(sizeof(ChBorderLayout));
	memset(l, 0, sizeof(ChBorderLayout));
	l->items = initialize_list();
	l->hgap = hgap;
	l->vgap = vgap;
	l->base.x = x;
	l->base.y = y;
	l->base.w = w;
	l->base.h = h;
	l->base.visible = true;
	l->defBarH = 0;
	l->defSideW = 0;
	l->base.type = CH_WIDEGT_BORDERLAYOUT;
	l->base.ChPaintHandler = ChBLPaint;
	l->base.ChMouseEvent = ChBL_Mouse;
	l->base.ChScrollEvent = ChBL_Scroll;
	l->base.ChTouchEvent = ChBL_Touch;
	l->base.ChDestroy = ChBL_Destroy;
	return l;
}