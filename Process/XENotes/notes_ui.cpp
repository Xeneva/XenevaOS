#include "notes_ui.h"
#include "notes_fs.h"
#include "custom_btn.h"
#include <widgets/window.h>
#include <widgets/sidebar.h>
#include <widgets/textbox.h>
#include <widgets/toolbar.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <keycode.h>

ChSidebar* notesSidebar = NULL;
ChNotesEditor* notesEditor = NULL;
ChNotesTitleBar* notesTitleBar = NULL;
int currentNoteSection = -1;
int currentNoteIdx = -1;

/* Title bar widget paint handler */
static void _title_bar_paint(ChWidget* wid, ChWindow* win) {
	ChNotesTitleBar* tb = (ChNotesTitleBar*)wid;

	// Fill background
	ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, 0xFFFBFBFB);

	// Bottom border
	ChDrawRect(win->canv, wid->x, wid->y + wid->h - 1, wid->w, 1, 0xFFE2E8F0);

	ChRect clip = { wid->x, wid->y, wid->w, wid->h };

	// Draw "Title: " prefix in muted gray
	ChFontSetSize(win->app->baseFont, 13);
	int prefixW = ChFontGetWidth(win->app->baseFont, (char*)"Title: ");
	int ty = wid->y + 20;
	ChFontDrawTextClipped(win->canv, win->app->baseFont, (char*)"Title: ", wid->x + 12, ty, 0xFF64748B, &clip);

	// Draw title text
	int titleX = wid->x + 12 + prefixW;
	uint32_t titleColor = tb->focused ? 0xFF1D4ED8 : 0xFF0F172A;
	ChFontDrawTextClipped(win->canv, win->app->baseFont, tb->title, titleX, ty, titleColor, &clip);

	// If focused, draw cursor and focus underline
	if (tb->focused) {
		int titleW = ChFontGetWidth(win->app->baseFont, tb->title);
		int curX = titleX + titleW + 2;
		if (curX < wid->x + wid->w - 10) {
			ChDrawRect(win->canv, curX, wid->y + 6, 2, 16, 0xFF2563EB);
		}
		int underlineW = titleW > 40 ? titleW : 40;
		ChDrawRect(win->canv, titleX, wid->y + wid->h - 2, underlineW + 4, 2, 0xFF3B82F6);
	}
}

/* Title bar widget mouse handler */
static void _title_bar_mouse(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	ChNotesTitleBar* tb = (ChNotesTitleBar*)wid;
	if (button == 1) {
		tb->focused = true;
		if (notesEditor) notesEditor->focused = false;
		tb->cursorIndex = strlen(tb->title);
		if (wid->ChPaintHandler) {
			wid->ChPaintHandler(wid, win);
			ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
		}
	}
}

/* Sidebar header paint handler */
typedef struct _sidebar_header_ {
	ChWidget base;
} ChSidebarHeader;

static void _sidebar_header_paint(ChWidget* wid, ChWindow* win) {
	// Header area background
	ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, 0xFFF0F0F0);
	// Vertical line on right edge
	ChDrawRect(win->canv, wid->x + wid->w - 1, wid->y, 1, wid->h, 0xFFD1D1D6);
	// Bottom horizontal line
	ChDrawRect(win->canv, wid->x, wid->y + wid->h - 1, wid->w, 1, 0xFFD1D1D6);
}

/* Format action handlers */
void OnBoldClicked(ChWidget* wid, ChWindow* win) {
	ChNotesEditorApplyFormat(notesEditor, FORMAT_BOLD, 0);
}

void OnItalicClicked(ChWidget* wid, ChWindow* win) {
	ChNotesEditorApplyFormat(notesEditor, FORMAT_ITALIC, 0);
}

void OnSizeUpClicked(ChWidget* wid, ChWindow* win) {
	ChNotesEditorApplyFormat(notesEditor, FORMAT_SIZE_UP, 0);
}

void OnSizeDownClicked(ChWidget* wid, ChWindow* win) {
	ChNotesEditorApplyFormat(notesEditor, FORMAT_SIZE_DOWN, 0);
}

void OnListClicked(ChWidget* wid, ChWindow* win) {
	ChNotesEditorApplyFormat(notesEditor, FORMAT_LIST, 0);
}

void OnColorRedClicked(ChWidget* wid, ChWindow* win) {
	ChNotesEditorApplyFormat(notesEditor, FORMAT_COLOR, 0xFFFF0000);
}

void OnColorBlueClicked(ChWidget* wid, ChWindow* win) {
	ChNotesEditorApplyFormat(notesEditor, FORMAT_COLOR, 0xFF0000FF);
}

void OnColorGreenClicked(ChWidget* wid, ChWindow* win) {
	ChNotesEditorApplyFormat(notesEditor, FORMAT_COLOR, 0xFF00C000);
}

void OnColorBlackClicked(ChWidget* wid, ChWindow* win) {
	ChNotesEditorApplyFormat(notesEditor, FORMAT_COLOR, 0xFF000000);
}

/* Sidebar selection handler */
void OnNoteSelected(ChSidebarItem* item, ChWindow* mainWin) {
	if (!item || item->label[0] == '\0') return;

	/* Determine current item pointer from stored indices (safe against realloc) */
	ChSidebarItem* prevNoteItem = NULL;
	if (currentNoteSection >= 0 && currentNoteSection < notesSidebar->sectionCount) {
		if (currentNoteIdx >= 0 && currentNoteIdx < notesSidebar->sections[currentNoteSection].itemCount) {
			prevNoteItem = &notesSidebar->sections[currentNoteSection].items[currentNoteIdx];
		}
	}

	/* Save text of previous note before switching */
	if (prevNoteItem && notesEditor && notesEditor->textBuffer) {
		if (prevNoteItem->data) {
			free(prevNoteItem->data);
		}
		prevNoteItem->data = ChNotesEditorGetText(notesEditor);
	}

	/* Update stored indices to the newly selected item */
	currentNoteSection = -1;
	currentNoteIdx = -1;
	for (int s = 0; s < notesSidebar->sectionCount; s++) {
		for (int i = 0; i < notesSidebar->sections[s].itemCount; i++) {
			if (&notesSidebar->sections[s].items[i] == item) {
				currentNoteSection = s;
				currentNoteIdx = i;
				break;
			}
		}
		if (currentNoteSection != -1) break;
	}

	/* Update Title Bar */
	if (notesTitleBar) {
		strncpy(notesTitleBar->title, item->label, SIDEBAR_MAX_LABEL - 1);
		notesTitleBar->title[SIDEBAR_MAX_LABEL - 1] = '\0';
		notesTitleBar->cursorIndex = strlen(notesTitleBar->title);
		notesTitleBar->focused = false;
		if (notesTitleBar->base.ChPaintHandler) {
			notesTitleBar->base.ChPaintHandler(&notesTitleBar->base, mainWin);
			ChWindowUpdate(mainWin, notesTitleBar->base.x, notesTitleBar->base.y, notesTitleBar->base.w, notesTitleBar->base.h, 0, 1);
		}
	}

	/* Update Editor Content */
	if (item->data) {
		ChNotesEditorSetText(notesEditor, (char*)item->data);
	} else {
		item->data = strdup("");
		ChNotesEditorSetText(notesEditor, (char*)"");
	}
}

/* Action: + New Note */
void OnNewNoteClicked(ChWidget* wid, ChWindow* win) {
	if (!notesSidebar || notesSidebar->sectionCount == 0) return;
	ChSidebarSection* sec = &notesSidebar->sections[0];

	// Save active note text before creating new
	if (currentNoteSection >= 0 && currentNoteSection < notesSidebar->sectionCount) {
		if (currentNoteIdx >= 0 && currentNoteIdx < notesSidebar->sections[currentNoteSection].itemCount) {
			ChSidebarItem* prev = &notesSidebar->sections[currentNoteSection].items[currentNoteIdx];
			if (notesEditor && notesEditor->textBuffer) {
				if (prev->data) free(prev->data);
				prev->data = ChNotesEditorGetText(notesEditor);
			}
		}
	}

	char title[SIDEBAR_MAX_LABEL];
	snprintf(title, sizeof(title), "Note %d", sec->itemCount + 1);

	NotesCreateNew(title, win);

	if (notesTitleBar) {
		notesTitleBar->focused = true;
		if (notesTitleBar->base.ChPaintHandler) {
			notesTitleBar->base.ChPaintHandler(&notesTitleBar->base, win);
			ChWindowUpdate(win, notesTitleBar->base.x, notesTitleBar->base.y, notesTitleBar->base.w, notesTitleBar->base.h, 0, 1);
		}
	}
	if (notesEditor) {
		notesEditor->focused = false;
	}
}

/* Action: Save */
void OnSaveNoteClicked(ChWidget* wid, ChWindow* win) {
	if (!notesSidebar || notesSidebar->sectionCount == 0) return;
	ChSidebarSection* sec = &notesSidebar->sections[0];
	if (currentNoteIdx < 0 || currentNoteIdx >= sec->itemCount) return;

	ChSidebarItem* item = &sec->items[currentNoteIdx];

	// Sync editor buffer into item data
	if (notesEditor && notesEditor->textBuffer) {
		if (item->data) free(item->data);
		item->data = ChNotesEditorGetText(notesEditor);
	}

	const char* content = item->data ? (const char*)item->data : "";
	NotesSaveNote(item->label, content);
}

/* Action: Rename */
void OnRenameNoteClicked(ChWidget* wid, ChWindow* win) {
	if (notesTitleBar) {
		notesTitleBar->focused = true;
		if (notesEditor) notesEditor->focused = false;
		if (notesTitleBar->base.ChPaintHandler) {
			notesTitleBar->base.ChPaintHandler(&notesTitleBar->base, win);
			ChWindowUpdate(win, notesTitleBar->base.x, notesTitleBar->base.y, notesTitleBar->base.w, notesTitleBar->base.h, 0, 1);
		}
	}
}

/* Action: Delete */
void OnDeleteNoteClicked(ChWidget* wid, ChWindow* win) {
	if (!notesSidebar || notesSidebar->sectionCount == 0) return;
	ChSidebarSection* sec = &notesSidebar->sections[0];
	if (sec->itemCount == 0 || currentNoteIdx < 0 || currentNoteIdx >= sec->itemCount) return;

	ChSidebarItem* item = &sec->items[currentNoteIdx];

	// Delete file from disk
	NotesDeleteNote(item->label);

	// Free note text
	if (item->data) {
		free(item->data);
		item->data = NULL;
	}

	// Remove item from section items array
	for (int i = currentNoteIdx; i < sec->itemCount - 1; i++) {
		sec->items[i] = sec->items[i + 1];
	}
	sec->itemCount--;

	if (sec->itemCount == 0) {
		// Automatically create a new note so the app always has an active note
		NotesCreateNew("Untitled Note", win);
	} else {
		if (currentNoteIdx >= sec->itemCount) {
			currentNoteIdx = sec->itemCount - 1;
		}
		for (int i = 0; i < sec->itemCount; i++) {
			sec->items[i].selected = (i == currentNoteIdx) ? 1 : 0;
		}

		ChSidebarItem* newActive = &sec->items[currentNoteIdx];
		if (notesTitleBar) {
			strncpy(notesTitleBar->title, newActive->label, SIDEBAR_MAX_LABEL - 1);
			notesTitleBar->title[SIDEBAR_MAX_LABEL - 1] = '\0';
			notesTitleBar->cursorIndex = strlen(notesTitleBar->title);
			notesTitleBar->focused = false;
			if (notesTitleBar->base.ChPaintHandler) {
				notesTitleBar->base.ChPaintHandler(&notesTitleBar->base, win);
				ChWindowUpdate(win, notesTitleBar->base.x, notesTitleBar->base.y, notesTitleBar->base.w, notesTitleBar->base.h, 0, 1);
			}
		}
		if (notesEditor) {
			ChNotesEditorSetText(notesEditor, newActive->data ? (char*)newActive->data : (char*)"");
		}

		if (notesSidebar->base.ChPaintHandler) {
			notesSidebar->base.ChPaintHandler(&notesSidebar->base, win);
			ChWindowUpdate(win, notesSidebar->base.x, notesSidebar->base.y, notesSidebar->base.w, notesSidebar->base.h, 0, 1);
		}
	}
}

/* Key handler for Title Bar */
bool NotesTitleBarHandleKey(int ascii_code, ChWindow* win) {
	if (!notesTitleBar || !notesTitleBar->focused) return false;

	int len = strlen(notesTitleBar->title);

	if (ascii_code == KEY_RETURN) {
		notesTitleBar->focused = false;
		if (notesEditor) notesEditor->focused = true;
		if (notesTitleBar->base.ChPaintHandler) {
			notesTitleBar->base.ChPaintHandler(&notesTitleBar->base, win);
			ChWindowUpdate(win, notesTitleBar->base.x, notesTitleBar->base.y, notesTitleBar->base.w, notesTitleBar->base.h, 0, 1);
		}
		return true;
	}

	if (ascii_code == KEY_BACKSPACE) {
		if (len > 0) {
			notesTitleBar->title[len - 1] = '\0';
			notesTitleBar->cursorIndex = len - 1;

			if (notesSidebar && currentNoteSection >= 0 && currentNoteSection < notesSidebar->sectionCount) {
				ChSidebarSection* sec = &notesSidebar->sections[currentNoteSection];
				if (currentNoteIdx >= 0 && currentNoteIdx < sec->itemCount) {
					strncpy(sec->items[currentNoteIdx].label, notesTitleBar->title, SIDEBAR_MAX_LABEL - 1);
					sec->items[currentNoteIdx].label[SIDEBAR_MAX_LABEL - 1] = '\0';
					if (notesSidebar->base.ChPaintHandler) {
						notesSidebar->base.ChPaintHandler(&notesSidebar->base, win);
						ChWindowUpdate(win, notesSidebar->base.x, notesSidebar->base.y, notesSidebar->base.w, notesSidebar->base.h, 0, 1);
					}
				}
			}
			if (notesTitleBar->base.ChPaintHandler) {
				notesTitleBar->base.ChPaintHandler(&notesTitleBar->base, win);
				ChWindowUpdate(win, notesTitleBar->base.x, notesTitleBar->base.y, notesTitleBar->base.w, notesTitleBar->base.h, 0, 1);
			}
		}
		return true;
	}

	if (ascii_code >= 32 && ascii_code <= 126) {
		if (len < SIDEBAR_MAX_LABEL - 2) {
			notesTitleBar->title[len] = (char)ascii_code;
			notesTitleBar->title[len + 1] = '\0';
			notesTitleBar->cursorIndex = len + 1;

			if (notesSidebar && currentNoteSection >= 0 && currentNoteSection < notesSidebar->sectionCount) {
				ChSidebarSection* sec = &notesSidebar->sections[currentNoteSection];
				if (currentNoteIdx >= 0 && currentNoteIdx < sec->itemCount) {
					strncpy(sec->items[currentNoteIdx].label, notesTitleBar->title, SIDEBAR_MAX_LABEL - 1);
					sec->items[currentNoteIdx].label[SIDEBAR_MAX_LABEL - 1] = '\0';
					if (notesSidebar->base.ChPaintHandler) {
						notesSidebar->base.ChPaintHandler(&notesSidebar->base, win);
						ChWindowUpdate(win, notesSidebar->base.x, notesSidebar->base.y, notesSidebar->base.w, notesSidebar->base.h, 0, 1);
					}
				}
			}
			if (notesTitleBar->base.ChPaintHandler) {
				notesTitleBar->base.ChPaintHandler(&notesTitleBar->base, win);
				ChWindowUpdate(win, notesTitleBar->base.x, notesTitleBar->base.y, notesTitleBar->base.w, notesTitleBar->base.h, 0, 1);
			}
		}
		return true;
	}

	return true;
}

void NotesCreateNew(const char* title, ChWindow* mainWin) {
	if (!notesSidebar || notesSidebar->sectionCount == 0) return;
	ChSidebarSection* sec = &notesSidebar->sections[0];
	ChSidebarItem* newItem = ChSidebarAddItem(sec, title, NULL, 0, 0, NULL);
	newItem->OnSelect = OnNoteSelected;
	newItem->data = strdup("");

	/* Unselect previous items and select new note */
	for (int s = 0; s < notesSidebar->sectionCount; s++) {
		for (int i = 0; i < notesSidebar->sections[s].itemCount; i++) {
			notesSidebar->sections[s].items[i].selected = 0;
		}
	}
	newItem->selected = 1;
	currentNoteSection = 0;
	currentNoteIdx = sec->itemCount - 1;

	if (notesTitleBar) {
		strncpy(notesTitleBar->title, title, SIDEBAR_MAX_LABEL - 1);
		notesTitleBar->title[SIDEBAR_MAX_LABEL - 1] = '\0';
		notesTitleBar->cursorIndex = strlen(notesTitleBar->title);
		if (notesTitleBar->base.ChPaintHandler) {
			notesTitleBar->base.ChPaintHandler(&notesTitleBar->base, mainWin);
			ChWindowUpdate(mainWin, notesTitleBar->base.x, notesTitleBar->base.y, notesTitleBar->base.w, notesTitleBar->base.h, 0, 1);
		}
	}
	if (notesEditor) {
		ChNotesEditorSetText(notesEditor, (char*)"");
	}

	/* Repaint sidebar and update window */
	if (notesSidebar->base.ChPaintHandler) {
		notesSidebar->base.ChPaintHandler(&notesSidebar->base, mainWin);
		ChWindowUpdate(mainWin, notesSidebar->base.x, notesSidebar->base.y, notesSidebar->base.w, notesSidebar->base.h, 0, 1);
	}
}

void NotesInitializeUI(ChWindow* mainWin) {
	/* 1. Sidebar Header Area (Top of left pane) */
	ChSidebarHeader* sbHeader = (ChSidebarHeader*)malloc(sizeof(ChSidebarHeader));
	memset(sbHeader, 0, sizeof(ChSidebarHeader));
	sbHeader->base.x = 0;
	sbHeader->base.y = 26;
	sbHeader->base.w = 200;
	sbHeader->base.h = 66;
	sbHeader->base.ChPaintHandler = _sidebar_header_paint;

	/* Top prominent button: + New Note */
	CustomBtn* btnNew = CreateStyledBtn(10, 30, 180, 26, "+ New Note", 0xFF2563EB, 0xFF1D4ED8, 0xFF1E40AF, 0xFFFFFFFF, 0xFF1D4ED8);
	btnNew->base.ChActionHandler = OnNewNoteClicked;

	/* Row 2 action buttons: Save, Rename, Delete */
	CustomBtn* btnSave = CreateStyledBtn(10, 61, 56, 24, "Save", 0xFFFFFFFF, 0xFFE2E8F0, 0xFFCBD5E1, 0xFF1E293B, 0xFFCBD5E1);
	btnSave->base.ChActionHandler = OnSaveNoteClicked;

	CustomBtn* btnRename = CreateStyledBtn(70, 61, 62, 24, "Rename", 0xFFFFFFFF, 0xFFE2E8F0, 0xFFCBD5E1, 0xFF1E293B, 0xFFCBD5E1);
	btnRename->base.ChActionHandler = OnRenameNoteClicked;

	CustomBtn* btnDelete = CreateStyledBtn(136, 61, 54, 24, "Delete", 0xFFFFFFFF, 0xFFFEE2E2, 0xFFFECACA, 0xFFDC2626, 0xFFFCA5A5);
	btnDelete->base.ChActionHandler = OnDeleteNoteClicked;

	/* 2. Sidebar Notes List (Below action buttons) */
	notesSidebar = ChSidebarCreate(0, 92, 200, mainWin->info->height - 92);
	ChSidebarAddSection(notesSidebar, "NOTES");

	/* 3. Right Pane Layout */
	int tbX = 200;
	int tbW = mainWin->info->width - 200;

	// Formatting toolbar
	ChToolbar* toolbar = ChToolbarCreate(tbX, 26, tbW, 32, TOOLBAR_HORIZONTAL);

	CustomBtn* btnBold = CreateCustomBtn(0, 0, 32, 28, "B", 0, false);
	btnBold->base.ChActionHandler = OnBoldClicked;
	ChToolbarAddWidget(toolbar, (ChWidget*)btnBold, 32, 28);

	CustomBtn* btnItal = CreateCustomBtn(0, 0, 32, 28, "I", 0, false);
	btnItal->base.ChActionHandler = OnItalicClicked;
	ChToolbarAddWidget(toolbar, (ChWidget*)btnItal, 32, 28);

	ChToolbarAddSeparator(toolbar);

	CustomBtn* btnAUp = CreateCustomBtn(0, 0, 32, 28, "A+", 0, false);
	btnAUp->base.ChActionHandler = OnSizeUpClicked;
	ChToolbarAddWidget(toolbar, (ChWidget*)btnAUp, 32, 28);

	CustomBtn* btnADown = CreateCustomBtn(0, 0, 32, 28, "A-", 0, false);
	btnADown->base.ChActionHandler = OnSizeDownClicked;
	ChToolbarAddWidget(toolbar, (ChWidget*)btnADown, 32, 28);

	ChToolbarAddSeparator(toolbar);

	CustomBtn* btnList = CreateCustomBtn(0, 0, 48, 28, "List", 0, false);
	btnList->base.ChActionHandler = OnListClicked;
	ChToolbarAddWidget(toolbar, (ChWidget*)btnList, 48, 28);

	ChToolbarAddSeparator(toolbar);

	CustomBtn* btnRed = CreateCustomBtn(0, 0, 28, 28, "", 0xFFFF0000, true);
	btnRed->base.ChActionHandler = OnColorRedClicked;
	ChToolbarAddWidget(toolbar, (ChWidget*)btnRed, 28, 28);

	CustomBtn* btnBlue = CreateCustomBtn(0, 0, 28, 28, "", 0xFF0000FF, true);
	btnBlue->base.ChActionHandler = OnColorBlueClicked;
	ChToolbarAddWidget(toolbar, (ChWidget*)btnBlue, 28, 28);

	CustomBtn* btnGreen = CreateCustomBtn(0, 0, 28, 28, "", 0xFF00C000, true);
	btnGreen->base.ChActionHandler = OnColorGreenClicked;
	ChToolbarAddWidget(toolbar, (ChWidget*)btnGreen, 28, 28);

	CustomBtn* btnBlack = CreateCustomBtn(0, 0, 28, 28, "", 0xFF000000, true);
	btnBlack->base.ChActionHandler = OnColorBlackClicked;
	ChToolbarAddWidget(toolbar, (ChWidget*)btnBlack, 28, 28);

	// Title Bar Widget
	notesTitleBar = (ChNotesTitleBar*)malloc(sizeof(ChNotesTitleBar));
	memset(notesTitleBar, 0, sizeof(ChNotesTitleBar));
	notesTitleBar->base.x = tbX;
	notesTitleBar->base.y = 58;
	notesTitleBar->base.w = tbW;
	notesTitleBar->base.h = 30;
	notesTitleBar->base.ChPaintHandler = _title_bar_paint;
	notesTitleBar->base.ChMouseEvent = _title_bar_mouse;
	strncpy(notesTitleBar->title, "Untitled Note", SIDEBAR_MAX_LABEL - 1);
	notesTitleBar->cursorIndex = strlen(notesTitleBar->title);
	notesTitleBar->focused = false;

	// Rich Notes Editor
	notesEditor = ChCreateNotesEditor(mainWin, tbX, 88, tbW, mainWin->info->height - 88);
	ChNotesEditorSetText(notesEditor, (char*)"");

	// Auto-populate sidebar from /note or fallback to clean blank note
	NotesPopulateSidebar(mainWin);

	/* Add widgets to window in paint order */
	ChWindowAddWidget(mainWin, (ChWidget*)sbHeader);
	ChWindowAddWidget(mainWin, (ChWidget*)btnNew);
	ChWindowAddWidget(mainWin, (ChWidget*)btnSave);
	ChWindowAddWidget(mainWin, (ChWidget*)btnRename);
	ChWindowAddWidget(mainWin, (ChWidget*)btnDelete);
	ChWindowAddWidget(mainWin, (ChWidget*)notesSidebar);

	ChWindowAddWidget(mainWin, (ChWidget*)toolbar);
	ChWindowAddWidget(mainWin, (ChWidget*)btnBold);
	ChWindowAddWidget(mainWin, (ChWidget*)btnItal);
	ChWindowAddWidget(mainWin, (ChWidget*)btnAUp);
	ChWindowAddWidget(mainWin, (ChWidget*)btnADown);
	ChWindowAddWidget(mainWin, (ChWidget*)btnList);
	ChWindowAddWidget(mainWin, (ChWidget*)btnRed);
	ChWindowAddWidget(mainWin, (ChWidget*)btnBlue);
	ChWindowAddWidget(mainWin, (ChWidget*)btnGreen);
	ChWindowAddWidget(mainWin, (ChWidget*)btnBlack);

	ChWindowAddWidget(mainWin, (ChWidget*)notesTitleBar);
	ChWindowAddWidget(mainWin, (ChWidget*)notesEditor);
}
