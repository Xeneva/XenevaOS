#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/window.h>
#include <string.h>
#include <stdlib.h>
#include <keycode.h>
#include "notes_ui.h"
#include "notes_fs.h"
#include "notes_editor.h"

ChitralekhaApp* app;
ChWindow* mainWin;

void NotesClose(ChWindow* win, ChWinGlobalControl* ctl) {
	OnSaveNoteClicked(NULL, win);
	ChWindowCloseWindow(win);
}

void NotesHandleKey(int ascii_code) {
	if (!notesEditor)
		return;

	if (NotesTitleBarHandleKey(ascii_code, mainWin)) {
		return;
	}

	int len = notesEditor->textLength;
	if (len < 32760) {
		ChNotesEditorHandleKey(notesEditor, ascii_code);
	}
}

void WindowHandleMessage(PostEvent* e) {
	switch (e->type) {
	case DEODHAI_REPLY_MOUSE_EVENT: {
		int handle = e->dword4;
		if (e->dword5 == WINDOW_HANDLE_TYPE_NORMAL) {
			ChWindow* mouseWin = ChGetWindowByHandle(mainWin, handle);
			ChWindowHandleMouse(mouseWin, e->dword, e->dword2, e->dword3);
		} else if (e->dword5 == WINDOW_HANDLE_TYPE_POPUP) {
			ChWindow* pw = ChGetPopupWindowByHandle(mainWin, handle);
			ChPopupWindowHandleMouse(pw, e->dword, e->dword2, e->dword3);
		}
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	case DEODHAI_REPLY_KEY_EVENT: {
		int code = e->dword;
		ChitralekhaProcessKey(code);
		char c = ChitralekhaKeyToASCII(code);
		NotesHandleKey(c);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	case DEODHAI_REPLY_FOCUS_CHANGED: {
		int focus_val = e->dword;
		int handle = e->dword2;
		ChWindow* focWin = ChGetWindowByHandle(mainWin, handle);
		ChWindowHandleFocus(focWin, focus_val, handle);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	}
}

int main(int argc, char* argv[]) {
	app = ChitralekhaStartApp(argc, argv);

	// Create a large, premium window
	mainWin = ChCreateWindow(app, WINDOW_FLAG_MOVABLE, (char*)"XENotes", 100, 100, 600, 480);
	mainWin->color = 0xFFF0F0F0; // Light grey/white background

	// Bind close button
	for (int i = 0; i < mainWin->GlobalControls->pointer; i++) {
		ChWinGlobalControl* ctl = (ChWinGlobalControl*)list_get_at(mainWin->GlobalControls, i);
		if (ctl->type == WINDOW_GLOBAL_CONTROL_CLOSE) {
			ctl->ChGlobalActionEvent = NotesClose;
			break;
		}
	}

	/* broadcast the note icon to broadcast listeners */
	ChWindowBroadcastIcon(app, "/icons/notes.bmp");

	// Initialize the custom UI (Sidebar and TextBox)
	NotesInitializeUI(mainWin);

	ChWindowPaint(mainWin);

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));

	// Setup the jump buffer for window destruction recovery
	setjmp(mainWin->jump);

	// Main event loop
	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		WindowHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT) {
			_KePauseThread();
		}
	}

	return 0;
}
