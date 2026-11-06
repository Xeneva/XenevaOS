/**
* @file box_layout.h
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

#include "box_layout.h"
#include "border_layout.h"

#define BX_COUNT(l) ((l)->items->pointer)

static ChBoxItem* BX_Item(ChBoxLayout* l, size_t i) {
	return (ChBoxItem*)list_get_at(l->items, i);
}

static void ChBoxSetBounds(ChWidget* w, int x, int y, int ww, int hh) {
	if (w->x != x || w->y != y || w->w != ww || w->h != hh) {
		w->x = x;
		w->y = y;
		w->w = ww;
		w->h = hh;
		w->hover = false;
		w->hoverPainted = false;
		w->lastMouseX = x;
		w->lastMouseY = y;
	}
}

void ChBoxLayoutDoLayout(ChBoxLayout* l) {
	bool horiz = (l->axis == CH_BOX_AXIS_X);
	int x = l->base.x + l->pad, y = l->base.y + l->pad;
	int w = l->base.w - 2 * l->pad, h = l->base.h - 2 * l->pad;
	if (w < 0)
		w = 0;
	if (h < 0)
		h = 0;

	int mainSize = horiz ? w : h;
	int mainPos = horiz ? x : y;
	int mainEnd = mainPos + mainSize;

	int n = 0, fixed = 0, weightSum = 0;
	for (size_t i = 0; i < BX_COUNT(l); i++) {
		ChBoxItem* it = BX_Item(l, i);
		if (!it->widget->visible)
			continue;
		int pref = horiz ? it->prefW : it->prefH;
		n++;
		if (pref > 0)
			fixed += pref;
		else
			weightSum += it->weight;
	}

	if (n == 0)
		return;

	int avail = mainSize - l->gap * (n - 1) - fixed;
	if (avail < 0)
		avail = 0;
	int weightLeft = weightSum;

	/* place all the children now */
	for (size_t i = 0; i < BX_COUNT(l); i++) {
		ChBoxItem* it = BX_Item(l, i);
		ChWidget* wid = it->widget;
		if (!wid->visible)
			continue;
		int pref = horiz ? it->prefW : it->prefH;
		int size;
		if (pref > 0) {
			size = pref;
		} else {
			/* last flexible child absorbs the rounding remainder */
			size = weightLeft > 0 ? avail * it->weight / weightLeft : 0;
			avail -= size;
			weightLeft -= it->weight;
		}
		if (mainPos + size > mainEnd)
			size = mainEnd - mainPos;
		if (size < 0)
			size = 0;
		if (horiz)
			ChBoxSetBounds(wid, mainPos, y, size, h);
		else
			ChBoxSetBounds(wid, x, mainPos, w, size);

		mainPos += size + l->gap;
	}

	for (size_t i = 0; i < BX_COUNT(l); i++) {
		ChWidget* w = BX_Item(l, i)->widget;
		if (w->type == CH_WIDGET_BOXLAYOUT)
			ChBoxLayoutDoLayout((ChBoxLayout*)w);
		else if (w->type == CH_WIDEGT_BORDERLAYOUT) {
			ChBorderLayoutDoLayout((ChBorderLayout*)w);
		}
	}
}

/**
 * @brief ChBoxLayoutAdd -- add a widget to box layout
 * @param l -- Pointer to Box Layout
 * @param w -- Pointer to addable widget
 */
void ChBoxLayoutAdd(ChBoxLayout* l, ChWidget* w) {
	ChBoxItem* it = (ChBoxItem*)malloc(sizeof(ChBoxItem));
	it->widget = w;
	it->prefW = w->w;
	it->prefH = w->h;
	it->weight = 1;
	list_add(l->items, it);
	ChBoxLayoutDoLayout(l);
}
