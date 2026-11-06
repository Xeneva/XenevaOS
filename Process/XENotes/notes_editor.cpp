#include "notes_editor.h"
#include "notes_ui.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <keycode.h>

void ChNotesEditorApplyFormat(ChNotesEditor* editor, int formatType, uint32_t color) {
	if (editor->selectionStart != -1 && editor->selectionEnd != -1 &&
		editor->selectionStart != editor->selectionEnd) {
		int start = editor->selectionStart < editor->selectionEnd ? editor->selectionStart
																  : editor->selectionEnd;
		int end = editor->selectionStart > editor->selectionEnd ? editor->selectionStart
																: editor->selectionEnd;
		for (int i = start; i < end; i++) {
			if (formatType == FORMAT_BOLD) {
				editor->textBuffer[i].bold = !editor->textBuffer[i].bold;
			} else if (formatType == FORMAT_ITALIC) {
				editor->textBuffer[i].italic = !editor->textBuffer[i].italic;
			} else if (formatType == FORMAT_COLOR) {
				editor->textBuffer[i].color = color;
			} else if (formatType == FORMAT_SIZE_UP) {
				editor->textBuffer[i].size =
					editor->textBuffer[i].size < 48 ? editor->textBuffer[i].size + 2 : 48;
			} else if (formatType == FORMAT_SIZE_DOWN) {
				editor->textBuffer[i].size =
					editor->textBuffer[i].size > 8 ? editor->textBuffer[i].size - 2 : 8;
			} else if (formatType == FORMAT_LIST) {
				editor->textBuffer[i].is_bullet = !editor->textBuffer[i].is_bullet;
			}
		}
	} else {
		if (formatType == FORMAT_BOLD) {
			editor->currentBold = !editor->currentBold;
		} else if (formatType == FORMAT_ITALIC) {
			editor->currentItalic = !editor->currentItalic;
		} else if (formatType == FORMAT_COLOR) {
			editor->currentColor = color;
		} else if (formatType == FORMAT_SIZE_UP) {
			editor->currentSize = editor->currentSize < 48 ? editor->currentSize + 2 : 48;
		} else if (formatType == FORMAT_SIZE_DOWN) {
			editor->currentSize = editor->currentSize > 8 ? editor->currentSize - 2 : 8;
		} else if (formatType == FORMAT_LIST) {
			editor->currentBullet = !editor->currentBullet;
			// Also apply to current line if cursor is on it
			int startOfLine = editor->cursorIndex;
			while (startOfLine > 0 && editor->textBuffer[startOfLine - 1].c != '\n') {
				startOfLine--;
			}
			if (startOfLine < editor->textLength) {
				editor->textBuffer[startOfLine].is_bullet = editor->currentBullet;
			}
		}
	}

	if (editor->wid.ChPaintHandler) {
		editor->wid.ChPaintHandler(&editor->wid, editor->win);
		ChWindowUpdate(
			editor->win, editor->wid.x, editor->wid.y, editor->wid.w, editor->wid.h, 0, 1);
	}
}

static void _editor_paint(ChWidget* wid, ChWindow* win) {
	ChNotesEditor* ed = (ChNotesEditor*)wid;

	// Fill background
	ChDrawRect(win->canv, ed->wid.x, ed->wid.y, ed->wid.w, ed->wid.h, ed->bgColor);

	// Draw outer border if hovered or focused
	uint32_t borderCol = ed->focused ? 0xFF6689D5 : (wid->hover ? 0xFF4067BA : 0xffdcdcdc);
	ChDrawRectUnfilled(win->canv, ed->wid.x, ed->wid.y, ed->wid.w, ed->wid.h, borderCol);
	if (ed->focused || wid->hover) {
		ChDrawRectUnfilled(
			win->canv, ed->wid.x + 1, ed->wid.y + 1, ed->wid.w - 2, ed->wid.h - 2, borderCol);
	}

	ChRect clip;
	clip.x = ed->wid.x + 4;
	clip.y = ed->wid.y + 4;
	clip.w = ed->wid.w - 8;
	clip.h = ed->wid.h - 8;

	int curX = clip.x;
	int curY = clip.y;

	char tempBuf[256];
	int tempLen = 0;

	uint8_t currentBold = 0;
	uint8_t currentItalic = 0;
	uint8_t currentSize = 14;
	uint32_t currentColor = ed->fgColor;

	int cursorScreenX = curX;
	int cursorScreenY = curY;

	int maxLineHeight = 20;
	bool isFirstOfLine = true;

	for (int i = 0; i <= ed->textLength; i++) {
		if (i == ed->cursorIndex) {
			cursorScreenX = curX;
			cursorScreenY = curY;
			if (tempLen > 0) {
				tempBuf[tempLen] = '\0';
				ChFont* drawFont = currentItalic && ed->italicFont ? ed->italicFont : ed->baseFont;
				ChFontSetSize(drawFont, currentSize);
				int textW = ChFontGetWidth(drawFont, tempBuf);
				cursorScreenX += textW;
			}
		}

		if (i == ed->textLength)
			break;

		RichChar rc = ed->textBuffer[i];
		uint8_t chSize = rc.size ? rc.size : 14;
		if (chSize + 6 > maxLineHeight)
			maxLineHeight = chSize + 6;

		if (isFirstOfLine && rc.is_bullet) {
			ChDrawRect(win->canv, curX + 6, curY + (maxLineHeight / 2) - 2, 4, 4, rc.color);
			curX += 15;
			if (i == ed->cursorIndex)
				cursorScreenX += 15;
		}
		isFirstOfLine = false;

		bool isSelected = false;
		if (ed->selectionStart != -1 && ed->selectionEnd != -1) {
			int start =
				ed->selectionStart < ed->selectionEnd ? ed->selectionStart : ed->selectionEnd;
			int end = ed->selectionStart > ed->selectionEnd ? ed->selectionStart : ed->selectionEnd;
			if (i >= start && i < end) {
				isSelected = true;
			}
		}

		bool styleChanged = (rc.bold != currentBold || rc.italic != currentItalic ||
							 rc.color != currentColor || chSize != currentSize);

		if (rc.c == '\n' || tempLen == 255 || styleChanged || isSelected) {
			if (tempLen > 0) {
				tempBuf[tempLen] = '\0';
				ChFont* drawFont = currentItalic && ed->italicFont ? ed->italicFont : ed->baseFont;
				ChFontSetSize(drawFont, currentSize ? currentSize : 14);
				int textW = ChFontGetWidth(drawFont, tempBuf);

				ChFontDrawTextClipped(win->canv,
									  drawFont,
									  tempBuf,
									  curX,
									  curY + (currentSize ? currentSize : 14),
									  currentColor,
									  &clip);
				if (currentBold) {
					ChFontDrawTextClipped(win->canv,
										  drawFont,
										  tempBuf,
										  curX + 1,
										  curY + (currentSize ? currentSize : 14),
										  currentColor,
										  &clip);
				}

				curX += textW;
				tempLen = 0;
			}

			currentBold = rc.bold;
			currentItalic = rc.italic;
			currentColor = rc.color;
			currentSize = chSize;
		}

		if (rc.c == '\n') {
			curX = clip.x;
			curY += maxLineHeight;
			maxLineHeight = 20;
			isFirstOfLine = true;
		} else {
			if (isSelected) {
				char single[2] = {rc.c, 0};
				ChFont* drawFont = currentItalic && ed->italicFont ? ed->italicFont : ed->baseFont;
				ChFontSetSize(drawFont, chSize);
				int charW = ChFontGetWidth(drawFont, single);
				ChDrawRect(win->canv, curX, curY, charW, chSize + 6, ed->selBgColor);

				ChFontDrawTextClipped(
					win->canv, drawFont, single, curX, curY + chSize, ed->selFgColor, &clip);
				if (currentBold) {
					ChFontDrawTextClipped(win->canv,
										  drawFont,
										  single,
										  curX + 1,
										  curY + chSize,
										  ed->selFgColor,
										  &clip);
				}
				curX += charW;
			} else {
				tempBuf[tempLen++] = rc.c;
			}
		}
	}

	if (tempLen > 0) {
		tempBuf[tempLen] = '\0';
		ChFont* drawFont = currentItalic && ed->italicFont ? ed->italicFont : ed->baseFont;
		ChFontSetSize(drawFont, currentSize ? currentSize : 14);
		int textW = ChFontGetWidth(drawFont, tempBuf);

		ChFontDrawTextClipped(win->canv,
							  drawFont,
							  tempBuf,
							  curX,
							  curY + (currentSize ? currentSize : 14),
							  currentColor,
							  &clip);
		if (currentBold) {
			ChFontDrawTextClipped(win->canv,
								  drawFont,
								  tempBuf,
								  curX + 1,
								  curY + (currentSize ? currentSize : 14),
								  currentColor,
								  &clip);
		}
		curX += textW;
	}

	if (ed->cursorIndex == ed->textLength) {
		cursorScreenX = curX;
		cursorScreenY = curY;
	}

	if (ed->focused) {
		ChDrawRect(win->canv, cursorScreenX, cursorScreenY + 2, 2, maxLineHeight - 4, ed->fgColor);
	}
}

static int _editor_get_index_from_pos(ChNotesEditor* ed, int mx, int my) {
	int curX = ed->wid.x + 4;
	int curY = ed->wid.y + 4;

	int maxLineHeight = 20;
	for (int j = 0; j < ed->textLength; j++) {
		if (ed->textBuffer[j].c == '\n')
			break;
		uint8_t sz = ed->textBuffer[j].size ? ed->textBuffer[j].size : 14;
		if (sz + 6 > maxLineHeight)
			maxLineHeight = sz + 6;
	}

	bool isFirstOfLine = true;

	for (int i = 0; i < ed->textLength; i++) {
		RichChar rc = ed->textBuffer[i];

		if (isFirstOfLine && rc.is_bullet) {
			curX += 15;
		}
		isFirstOfLine = false;

		if (my >= curY && my < curY + maxLineHeight) {
			if (rc.c == '\n')
				return i;

			char single[2] = {rc.c, 0};
			ChFont* drawFont = rc.italic && ed->italicFont ? ed->italicFont : ed->baseFont;
			uint8_t sz = rc.size ? rc.size : 14;
			ChFontSetSize(drawFont, sz);
			int charW = ChFontGetWidth(drawFont, single);

			if (mx < curX)
				return i;
			if (mx >= curX && mx <= curX + charW) {
				return (mx < curX + charW / 2) ? i : i + 1;
			}
			curX += charW;
		} else {
			if (rc.c == '\n') {
				curY += maxLineHeight;
				curX = ed->wid.x + 4;
				isFirstOfLine = true;

				maxLineHeight = 20;
				for (int j = i + 1; j < ed->textLength; j++) {
					if (ed->textBuffer[j].c == '\n')
						break;
					uint8_t sz = ed->textBuffer[j].size ? ed->textBuffer[j].size : 14;
					if (sz + 6 > maxLineHeight)
						maxLineHeight = sz + 6;
				}
			}
		}
	}
	return ed->textLength;
}

static void _editor_mouse_event(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	ChNotesEditor* ed = (ChNotesEditor*)wid;

	if (button == 1) { // Left click down
		ed->mouseDown = true;
		ed->focused = true;
		win->focusedWidget = wid;

		if (notesTitleBar && notesTitleBar->focused) {
			notesTitleBar->focused = false;
			if (notesTitleBar->base.ChPaintHandler) {
				notesTitleBar->base.ChPaintHandler(&notesTitleBar->base, win);
				ChWindowUpdate(win,
							   notesTitleBar->base.x,
							   notesTitleBar->base.y,
							   notesTitleBar->base.w,
							   notesTitleBar->base.h,
							   0,
							   1);
			}
		}

		int localX = x - win->info->x;
		int localY = y - win->info->y;
		int idx = _editor_get_index_from_pos(ed, localX, localY);
		ed->cursorIndex = idx;
		ed->selectionStart = idx;
		ed->selectionEnd = idx;

		if (wid->ChPaintHandler) {
			wid->ChPaintHandler(wid, win);
			ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
		}
	} else if (button == 0) { // Mouse release
		ed->mouseDown = false;
		if (ed->selectionStart == ed->selectionEnd) {
			ed->selectionStart = -1;
			ed->selectionEnd = -1;
		}
	}

	// Mouse drag
	if (ed->mouseDown && (x != wid->lastMouseX || y != wid->lastMouseY)) {
		int localX = x - win->info->x;
		int localY = y - win->info->y;
		int idx = _editor_get_index_from_pos(ed, localX, localY);
		ed->selectionEnd = idx;
		ed->cursorIndex = idx;

		if (wid->ChPaintHandler) {
			wid->ChPaintHandler(wid, win);
			ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
		}
	}

	if (!wid->hoverPainted && wid->hover) {
		if (wid->ChPaintHandler) {
			wid->ChPaintHandler(wid, win);
			ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
		}
		wid->hoverPainted = true;
	}

	if (!wid->hover && wid->hoverPainted) {
		if (wid->ChPaintHandler) {
			wid->ChPaintHandler(wid, win);
			ChWindowUpdate(win, wid->x, wid->y, wid->w, wid->h, 0, 1);
		}
		wid->hoverPainted = false;
	}
}

void ChNotesEditorHandleKey(ChNotesEditor* ed, int ascii_code) {
	if (!ed || !ed->focused)
		return;

	if (ascii_code == KEY_BACKSPACE) {
		if (ed->selectionStart != -1 && ed->selectionStart != ed->selectionEnd) {
			int start =
				ed->selectionStart < ed->selectionEnd ? ed->selectionStart : ed->selectionEnd;
			int end = ed->selectionStart > ed->selectionEnd ? ed->selectionStart : ed->selectionEnd;
			int count = end - start;
			for (int i = start; i < ed->textLength - count; i++) {
				ed->textBuffer[i] = ed->textBuffer[i + count];
			}
			ed->textLength -= count;
			ed->cursorIndex = start;
			ed->selectionStart = -1;
			ed->selectionEnd = -1;
		} else if (ed->cursorIndex > 0) {
			for (int i = ed->cursorIndex - 1; i < ed->textLength - 1; i++) {
				ed->textBuffer[i] = ed->textBuffer[i + 1];
			}
			ed->cursorIndex--;
			ed->textLength--;
		}
	} else if (ascii_code == KEY_RETURN) {
		ascii_code = '\n';
		goto insert_char;
	} else if (ascii_code >= 32 && ascii_code <= 126) {
	insert_char:
		if (ed->selectionStart != -1 && ed->selectionStart != ed->selectionEnd) {
			int start =
				ed->selectionStart < ed->selectionEnd ? ed->selectionStart : ed->selectionEnd;
			int end = ed->selectionStart > ed->selectionEnd ? ed->selectionStart : ed->selectionEnd;
			int count = end - start;
			for (int i = start; i < ed->textLength - count; i++) {
				ed->textBuffer[i] = ed->textBuffer[i + count];
			}
			ed->textLength -= count;
			ed->cursorIndex = start;
			ed->selectionStart = -1;
			ed->selectionEnd = -1;
		}

		if (ed->textLength >= ed->bufferCapacity) {
			ed->bufferCapacity = ed->bufferCapacity == 0 ? 1024 : ed->bufferCapacity * 2;
			ed->textBuffer =
				(RichChar*)realloc(ed->textBuffer, ed->bufferCapacity * sizeof(RichChar));
		}
		for (int i = ed->textLength; i > ed->cursorIndex; i--) {
			ed->textBuffer[i] = ed->textBuffer[i - 1];
		}
		ed->textBuffer[ed->cursorIndex].c = ascii_code;
		ed->textBuffer[ed->cursorIndex].bold = ed->currentBold;
		ed->textBuffer[ed->cursorIndex].italic = ed->currentItalic;
		ed->textBuffer[ed->cursorIndex].underline = ed->currentUnderline;
		ed->textBuffer[ed->cursorIndex].color = ed->currentColor;
		ed->textBuffer[ed->cursorIndex].size = ed->currentSize;

		bool isFirstOfLine =
			(ed->cursorIndex == 0 || ed->textBuffer[ed->cursorIndex - 1].c == '\n');
		ed->textBuffer[ed->cursorIndex].is_bullet = isFirstOfLine ? ed->currentBullet : 0;

		ed->cursorIndex++;
		ed->textLength++;
	}

	if (ed->wid.ChPaintHandler) {
		ed->wid.ChPaintHandler(&ed->wid, ed->win);
		ChWindowUpdate(ed->win, ed->wid.x, ed->wid.y, ed->wid.w, ed->wid.h, 0, 1);
	}
}

static void _editor_destroy(ChWidget* widget, ChWindow* win) {
	ChNotesEditor* ed = (ChNotesEditor*)widget;
	if (win->focusedWidget == widget)
		win->focusedWidget = NULL;
	if (ed->textBuffer)
		free(ed->textBuffer);
	free(ed);
}

ChNotesEditor* ChCreateNotesEditor(ChWindow* win, int x, int y, int w, int h) {
	ChNotesEditor* ed = (ChNotesEditor*)malloc(sizeof(ChNotesEditor));
	memset(ed, 0, sizeof(ChNotesEditor));
	ed->wid.x = x;
	ed->wid.y = 26 + y;
	ed->wid.w = w;
	ed->wid.h = h;
	ed->wid.ChDestroy = _editor_destroy;
	ed->wid.ChMouseEvent = _editor_mouse_event;
	ed->wid.ChPaintHandler = _editor_paint;
	ed->win = win;
	ed->focused = true;
	ed->bgColor = 0xFFFFFFFF;
	ed->fgColor = 0xFF000000;
	ed->selBgColor = 0xFF4067BA;
	ed->selFgColor = 0xFFFFFFFF;
	ed->currentColor = ed->fgColor;
	ed->currentSize = 14;

	if (win) {
		ed->baseFont = win->app->baseFont;
		ed->italicFont = ChInitialiseFont((char*)ROBOTO_LIGHT_ITALIC);
	}

	return ed;
}

void ChNotesEditorSetText(ChNotesEditor* editor, const char* text) {
	if (!editor)
		return;
	int len = text ? strlen(text) : 0;
	if (len >= editor->bufferCapacity) {
		editor->bufferCapacity = len + 1024;
		editor->textBuffer =
			(RichChar*)realloc(editor->textBuffer, editor->bufferCapacity * sizeof(RichChar));
	}
	editor->textLength = len;
	editor->cursorIndex = len;
	uint8_t defSize = editor->currentSize ? editor->currentSize : 14;
	for (int i = 0; i < len; i++) {
		editor->textBuffer[i].c = text[i];
		editor->textBuffer[i].bold = 0;
		editor->textBuffer[i].italic = 0;
		editor->textBuffer[i].underline = 0;
		editor->textBuffer[i].is_bullet = 0;
		editor->textBuffer[i].size = defSize;
		editor->textBuffer[i].color = editor->fgColor;
	}

	if (editor->wid.ChPaintHandler) {
		editor->wid.ChPaintHandler(&editor->wid, editor->win);
		ChWindowUpdate(
			editor->win, editor->wid.x, editor->wid.y, editor->wid.w, editor->wid.h, 0, 1);
	}
}

char* ChNotesEditorGetText(ChNotesEditor* editor) {
	char* buf = (char*)malloc(editor->textLength + 1);
	for (int i = 0; i < editor->textLength; i++) {
		buf[i] = editor->textBuffer[i].c;
	}
	buf[editor->textLength] = '\0';
	return buf;
}
