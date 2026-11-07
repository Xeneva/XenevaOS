#ifndef __NOTES_EDITOR_H__
#define __NOTES_EDITOR_H__

#include <chitralekha.h>
#include <widgets/window.h>
#include <font.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _rich_char_ {
	char c;
	uint8_t bold : 1;
	uint8_t italic : 1;
	uint8_t underline : 1;
	uint8_t is_bullet : 1;
	uint8_t size;
	uint32_t color;
} RichChar;

typedef struct _notes_editor_ {
	ChWidget wid;
	ChWindow* win;

	RichChar* textBuffer;
	int bufferCapacity;
	int textLength;

	int cursorIndex;
	int selectionStart;
	int selectionEnd;

	uint8_t currentBold;
	uint8_t currentItalic;
	uint8_t currentUnderline;
	uint8_t currentBullet;
	uint8_t currentSize;
	uint32_t currentColor;

	int scrollY;
	int contentHeight;

	ChFont* baseFont;

	uint32_t bgColor;
	uint32_t fgColor;
	uint32_t selBgColor;
	uint32_t selFgColor;

	bool focused;
	bool mouseDown;

	int lastX;
	int lastY;
	ChFont* italicFont;
} ChNotesEditor;

ChNotesEditor* ChCreateNotesEditor(ChWindow* win, int x, int y, int w, int h);
void ChNotesEditorSetText(ChNotesEditor* editor, const char* text);
char* ChNotesEditorGetText(ChNotesEditor* editor);
void ChNotesEditorApplyFormat(ChNotesEditor* editor, int formatType, uint32_t color);
void ChNotesEditorHandleKey(ChNotesEditor* editor, int ascii_code);

#define FORMAT_BOLD		 1
#define FORMAT_ITALIC	 2
#define FORMAT_UNDERLINE 3
#define FORMAT_COLOR	 4
#define FORMAT_SIZE_UP	 5
#define FORMAT_SIZE_DOWN 6
#define FORMAT_LIST		 7

#ifdef __cplusplus
}
#endif

#endif
