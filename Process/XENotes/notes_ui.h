#ifndef __NOTES_UI_H__
#define __NOTES_UI_H__

#include <chitralekha.h>
#include <widgets/window.h>
#include <widgets/sidebar.h>
#include <widgets/textbox.h>
#include <widgets/button.h>
#include "notes_editor.h"

typedef struct _notes_title_bar_ {
    ChWidget base;
    char title[SIDEBAR_MAX_LABEL];
    bool focused;
    int cursorIndex;
} ChNotesTitleBar;

extern ChSidebar* notesSidebar;
extern ChNotesEditor* notesEditor;
extern ChNotesTitleBar* notesTitleBar;
extern int currentNoteSection;
extern int currentNoteIdx;

/* Initialize the XENotes User Interface */
void NotesInitializeUI(ChWindow* mainWin);

/* Callback when a sidebar item is selected */
void OnNoteSelected(ChSidebarItem* item, ChWindow* mainWin);

/* Create and add a new note to the sidebar */
void NotesCreateNew(const char* title, ChWindow* mainWin);

/* Action callbacks for sidebar buttons */
void OnNewNoteClicked(ChWidget* wid, ChWindow* win);
void OnSaveNoteClicked(ChWidget* wid, ChWindow* win);
void OnRenameNoteClicked(ChWidget* wid, ChWindow* win);
void OnDeleteNoteClicked(ChWidget* wid, ChWindow* win);

/* Handle key input for title bar if focused */
bool NotesTitleBarHandleKey(int ascii_code, ChWindow* win);

#endif
