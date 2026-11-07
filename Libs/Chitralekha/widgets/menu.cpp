/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#include "menu.h"
#include "../font.h"
#include <sys/_keproc.h>

/**
 * TODO : Needs reimplementation of menu widgets
 * it's buggy
 *
 */

extern void ChPopupMenuPaint(ChPopupMenu* popup);

/*
 * Popup menus are created once and live until the app exits; after the
 * first ChMenuShow they only toggle between shown and hidden. Nothing on
 * the show/hide/hover paths allocates -- the only allocations are the
 * popup window and its single widget-list node, made on the first show.
 */

/*
 * ChMenuIsVisible -- true when the menu has a popup window that is shown
 */
static bool ChMenuIsVisible(ChPopupMenu* menu) {
	return menu && menu->backWindow && !menu->backWindow->info->hide;
}

/*
 * ChMenuClearHover -- drop every item's hover state, returns true
 * if anything changed (so the caller knows a repaint is needed)
 */
static bool ChMenuClearHover(ChPopupMenu* menu) {
	bool changed = false;
	for (int i = 0; i < menu->MenuItems->pointer; i++) {
		ChMenuItem* mi = (ChMenuItem*)list_get_at(menu->MenuItems, i);
		if (mi->wid.hover) {
			mi->wid.hover = false;
			mi->wid.KillFocus = true;
			changed = true;
		}
	}
	return changed;
}

/*
 * ChMenuHideTree -- hide a menu and every submenu opened under it
 * @param menu -- Pointer to popup menu
 */
static void ChMenuHideTree(ChPopupMenu* menu) {
	if (!menu || !menu->backWindow)
		return;
	for (int i = 0; i < menu->MenuItems->pointer; i++) {
		ChMenuItem* mi = (ChMenuItem*)list_get_at(menu->MenuItems, i);
		if (mi->menu)
			ChMenuHideTree(mi->menu);
	}
	ChMenuClearHover(menu);
	/* ChPopupWindowHide is a no-op for an already hidden window, so a
	 * menu is never "hidden" twice -- the compositor used to treat a
	 * second hide as a show */
	ChPopupWindowHide(menu->backWindow, menu->mainWindow);
}

void ChPopupMenuMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	ChPopupMenu* pm = (ChPopupMenu*)wid;
	/* the compositor can still route a few events to a popup that the
	 * client has just hidden; ignore them, or a held button would fire
	 * the same item's action again */
	if (!ChMenuIsVisible(pm))
		return;
	bool _need_paint = false;
	for (int i = 0; i < pm->MenuItems->pointer; i++) {
		ChMenuItem* mi = (ChMenuItem*)list_get_at(pm->MenuItems, i);
		if (x >= (win->info->x + mi->wid.x) && x < (win->info->x + mi->wid.x + mi->wid.w) &&
			y >= (win->info->y + mi->wid.y) && y < (win->info->y + mi->wid.y + mi->wid.h)) {
			/* repaint only when the hovered item changes, not on every
			 * mouse move inside the same item */
			if (!mi->wid.hover) {
				mi->wid.hover = true;
				_need_paint = true;
			}
			mi->wid.KillFocus = false;
			if (mi->wid.ChMouseEvent)
				mi->wid.ChMouseEvent((ChWidget*)mi, win, x, y, button);
			/* the item may have hidden this menu (leaf clicked) */
			if (!ChMenuIsVisible(pm))
				return;
		} else {
			if (mi->wid.hover) {
				mi->wid.hover = false;
				mi->wid.KillFocus = true;
				_need_paint = true;
			}
		}
	}

	if (_need_paint) {
		ChPopupMenuPaint(pm);
		ChWindowUpdate(pm->backWindow, 0, 0, pm->wid.w, pm->wid.h, 1, 0);
	}
}

/*
 * ChPopupMenuDestroy -- destroy handler for popup menu
 * @param widget -- Pointer to popup menu widget
 * @param win -- Pointer to main window
 */
void ChPopupMenuDestroy(ChWidget* widget, ChWindow* win) {
	_KePrint("Popup menu destroying \r\n");
	ChPopupMenu* pm = (ChPopupMenu*)widget;
	for (int i = 0; i < pm->MenuItems->pointer; i++) {
		ChMenuItem* mi = (ChMenuItem*)list_get_at(pm->MenuItems, i);
		if (mi->wid.ChDestroy)
			mi->wid.ChDestroy((ChWidget*)mi, win);
	}
	list_clear_all(pm->MenuItems);
	free(pm->MenuItems);
	free(pm);
	_KePrint("Popup Menu Destroyed \r\n");
}

/*
 * ChCreatePopupMenu -- create a new popup menu
 * @param mainWin -- Pointer to Main Window
 */
ChPopupMenu* ChCreatePopupMenu(ChWindow* mainWin, ChPopupMenu* parent) {
	ChPopupMenu* pm = (ChPopupMenu*)malloc(sizeof(ChPopupMenu));
	memset(pm, 0, sizeof(ChPopupMenu));
	pm->mainWindow = mainWin;
	pm->wid.ChMouseEvent = ChPopupMenuMouseEvent;
	pm->MenuItems = initialize_list();
	pm->wid.ChDestroy = ChPopupMenuDestroy;
	pm->parent = parent;
	return pm;
}

void ChMenuItemDestroy(ChWidget* wid, ChWindow* win) {
	ChMenuItem* mi = (ChMenuItem*)wid;
	_KePrint("MenuItem destroying %s\r\n", mi->title);
	free(mi->title);
	/* A submenu that was ever shown is in its own popup window's widget
	 * list, and that window frees it when it closes (popup windows close
	 * in creation order, parent menu's window first). Freeing it here as
	 * well was a double free. Only a never-shown submenu is ours to free. */
	if (mi->menu && !mi->menu->backWindow) {
		if (mi->menu->wid.ChDestroy)
			mi->menu->wid.ChDestroy((ChWidget*)mi->menu, win);
	}
	free(mi);
}

void ChMenuItemMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	ChMenuItem* item = (ChMenuItem*)wid;
	ChPopupMenu* pm = item->parent;

	if (!button)
		return;

	_KePrint("Item clicked %s \r\n", item->title);
	if (item->menu) {
		/* submenus attached without a parent still need one, so that
		 * ChMenuHide can walk up and close the whole chain */
		if (!item->menu->parent)
			item->menu->parent = pm;

		/* only one submenu open per level */
		for (int i = 0; i < pm->MenuItems->pointer; i++) {
			ChMenuItem* sibling = (ChMenuItem*)list_get_at(pm->MenuItems, i);
			if (sibling != item && sibling->menu)
				ChMenuHideTree(sibling->menu);
		}

		if (!ChMenuIsVisible(item->menu))
			ChMenuShow(item->menu, pm->x_loc + item->wid.x + item->wid.w, pm->y_loc + item->wid.y);
		return;
	}

	/* leaf item: close the whole menu chain, then run the action */
	ChMenuHide(pm);
	/* SET_FLAGS/BRING_FRONT aren't handled by DeodhaiXR and the
	 * menu's hide is shared-memory/frame-synced -- these sleeps
	 * used to block every menu click for ~1.5s waiting on
	 * nothing --axiss */
	ChWindowSetFlags(pm->mainWindow, (pm->mainWindow->flags & ~(WINDOW_FLAG_STATIC)));
	ChWindowSetFocused(pm->mainWindow);

	/* hand the action the app's main window, not the internal popup
	 * window the click happened to arrive on */
	if (item->wid.ChActionHandler)
		item->wid.ChActionHandler((ChWidget*)item, pm->mainWindow);
}
/*
 * ChCreateMenuItem -- create a new menu item
 * @param title -- title of the menu item
 * @param pm -- Pointer to popup menu
 */
ChMenuItem* ChCreateMenuItem(char* title, ChPopupMenu* pm) {
	ChMenuItem* mi = (ChMenuItem*)malloc(sizeof(ChMenuItem));
	memset(mi, 0, sizeof(ChMenuItem));
	mi->wid.h = DEFAULT_MENU_ITEM_HEIGHT;
	mi->wid.x = 0;
	mi->wid.ChMouseEvent = ChMenuItemMouseEvent;
	mi->title = (char*)malloc(strlen(title) + 1);
	strcpy(mi->title, title);
	mi->menu = NULL;
	list_add(pm->MenuItems, mi);
	mi->wid.ChDestroy = ChMenuItemDestroy;
	mi->parent = pm;
	return mi;
}

void ChMenuRecalculateDimensions(ChPopupMenu* pm) {
	int w = 0;
	int h = 0;
	if (!pm->mainWindow)
		return;
	ChFontSetSize(pm->mainWindow->app->baseFont, 10);
	int pad_y = DEFAULT_MENU_ITEM_BUTTON_PADY;
	h = pad_y;
	for (int i = 0; i < pm->MenuItems->pointer; i++) {
		ChMenuItem* mi = (ChMenuItem*)list_get_at(pm->MenuItems, i);
		int stringW = ChFontGetWidth(pm->mainWindow->app->baseFont, mi->title);
		if (w < stringW)
			w = stringW;
		if (mi->seperator)
			h += DEFAULT_MENU_ITEM_HEIGHT + DEFAULT_MENU_ITEM_BUTTON_PADY + 2;
		else
			h += DEFAULT_MENU_ITEM_HEIGHT + DEFAULT_MENU_ITEM_BUTTON_PADY;
	}
	if (!w && !h) {
		w = DEFAULT_POPUP_MENU_WIDTH;
		h = DEFAULT_POPUP_MENU_HEIGHT;
	} else
		w += 20 * 2;

	pm->wid.w = w;
	pm->wid.h = h;
	for (int i = 0; i < pm->MenuItems->pointer; i++) {
		ChMenuItem* mi = (ChMenuItem*)list_get_at(pm->MenuItems, i);
		mi->wid.w = pm->wid.w;
	}
}

/*
 * ChMenuShow -- show a popup menu at x, y relative to its main window.
 * The popup window is created on the first show and reused afterwards.
 * @param menu -- Pointer to popup menu
 * @param x -- X location relative to main window
 * @param y -- Y location relative to main window
 */
void ChMenuShow(ChPopupMenu* menu, int x, int y) {
	if (!menu || !menu->mainWindow)
		return;

	if (!menu->backWindow) {
		ChMenuRecalculateDimensions(menu);
		menu->x_loc = x + 5;
		menu->y_loc = y;
		menu->backWindow = ChCreatePopupWindow(menu->mainWindow,
											   menu->x_loc,
											   menu->y_loc,
											   menu->wid.w,
											   menu->wid.h,
											   WINDOW_FLAG_POPUP,
											   "Popup");
		if (!menu->backWindow)
			return;
		/* the only allocation after creation: one list node, once */
		list_add(menu->backWindow->widgets, menu);
	} else {
		menu->x_loc = x + 5;
		menu->y_loc = y;
		ChPopupWindowUpdateLocation(menu->backWindow, menu->mainWindow, menu->x_loc, menu->y_loc);
	}

	/* An item highlighted when the menu was last hidden would otherwise
	 * come back highlighted. Repaint is always needed on show anyway,
	 * because the compositor only recomposes a re-shown window when it is
	 * asked to update the whole window. */
	ChMenuClearHover(menu);
	ChPopupMenuPaint(menu);
	ChWindowUpdate(menu->backWindow, 0, 0, menu->wid.w, menu->wid.h, 1, 0);
	ChPopupWindowShow(menu->backWindow, menu->mainWindow);
	/* raise above the main window and take pointer focus; without this a
	 * re-shown menu stayed behind the main window once that window had
	 * been clicked, and clicks fell through to the main window */
	ChWindowSetFocused(menu->backWindow);
}

/*
 * ChMenuHide -- Hide a popup menu together with every menu
 * above it and every submenu opened below it
 * @param menu -- Pointer to popup menu
 */
void ChMenuHide(ChPopupMenu* menu) {
	if (!menu)
		return;
	while (menu->parent)
		menu = menu->parent;
	ChMenuHideTree(menu);
}
