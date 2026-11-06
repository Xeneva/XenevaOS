#include "notes_fs.h"
#include "notes_ui.h"
#include <sys/_kefile.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

void NotesInitFS() {
	static bool initialized = false;
	if (initialized) return;
	_KeCreateDir((char*)"/note");
	initialized = true;
}

/*
 * _MakeSafePath -- converts any note title into a strict 8.3 FAT32 path:
 * Base: 1 to 8 uppercase alphanumeric characters
 * Ext:  .NT (2 characters)
 * Path: /note/<BASE8>.NT (total filename + extension <= 11 characters)
 */
static void _MakeSafePath(const char* title, char* outPath, size_t outSize) {
	char base[9];
	size_t j = 0;
	if (!title || title[0] == '\0') {
		strncpy(base, "NOTE", sizeof(base));
	} else {
		for (size_t i = 0; title[i] != '\0' && j < 8; i++) {
			char c = title[i];
			if (c >= 'a' && c <= 'z') {
				base[j++] = c - 'a' + 'A';
			} else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
				base[j++] = c;
			}
		}
		if (j == 0) {
			strcpy(base, "NOTE");
			j = 4;
		}
		base[j] = '\0';
	}
	snprintf(outPath, outSize, "/note/%s.NT", base);
}

void NotesSaveNote(const char* title, const char* content) {
	if (!title || !content) return;
	NotesInitFS();

	char path[80];
	_MakeSafePath(title, path, sizeof(path));

	int fd = _KeOpenFile(path, FILE_OPEN_WRITE | FILE_OPEN_CREAT);
	if (fd >= 0) {
		_KeWriteFile(fd, (void*)content, strlen(content));
		_KeCloseFile(fd);
		_KePrint("XENotes: Saved '%s' -> %s (%d bytes)\n", title, path, (int)strlen(content));
	} else {
		_KePrint("XENotes: Failed to open %s for write\n", path);
	}
}

void NotesDeleteNote(const char* title) {
	if (!title) return;
	char path[80];
	_MakeSafePath(title, path, sizeof(path));
	_KeRemoveFile(path);
	_KePrint("XENotes: Deleted %s\n", path);
}

void NotesLoadFile(const char* filename, ChWindow* win) {
	int fd = _KeOpenFile((char*)filename, FILE_OPEN_READ_ONLY);
	if (fd < 0) {
		return;
	}

	char buffer[32767];
	memset(buffer, 0, 32767);
	_KeReadFile(fd, buffer, 32766);
	_KeCloseFile(fd);

	if (notesEditor) {
		ChNotesEditorSetText(notesEditor, buffer);
	}
}

void NotesSaveFile(const char* filename) {
	if (!notesEditor) return;

	char* currentText = ChNotesEditorGetText(notesEditor);
	if (!currentText) return;

	int fd = _KeOpenFile((char*)filename, FILE_OPEN_WRITE | FILE_OPEN_CREAT);
	if (fd >= 0) {
		_KeWriteFile(fd, currentText, strlen(currentText));
		_KeCloseFile(fd);
	}
	free(currentText);
}

void NotesPopulateSidebar(ChWindow* win) {
	NotesInitFS();

	if (!notesSidebar || notesSidebar->sectionCount == 0) return;
	ChSidebarSection* sec = &notesSidebar->sections[0];

	int dirfd = _KeOpenDir((char*)"/note");
	int loadedCount = 0;

	if (dirfd >= 0) {
		XEDirectoryEntry dirent;
		memset(&dirent, 0, sizeof(XEDirectoryEntry));

		while (1) {
			if (dirent.index == -1) break;
			int code = _KeReadDir(dirfd, &dirent);
			if (code == -1) {
				if (dirent.index == -1) break;
				continue;
			}

			if (dirent.flags & FILE_GENERAL) {
				int nlen = strlen(dirent.filename);
				if (nlen > 3) {
					const char* ext = dirent.filename + nlen - 3;
					if (ext[0] == '.' &&
						(ext[1] == 'N' || ext[1] == 'n') &&
						(ext[2] == 'T' || ext[2] == 't')) {

						char noteTitle[SIDEBAR_MAX_LABEL];
						int baseLen = nlen - 3;
						if (baseLen >= SIDEBAR_MAX_LABEL) baseLen = SIDEBAR_MAX_LABEL - 1;
						strncpy(noteTitle, dirent.filename, baseLen);
						noteTitle[baseLen] = '\0';

						char filePath[64];
						snprintf(filePath, sizeof(filePath), "/note/%s", dirent.filename);
						int fd = _KeOpenFile(filePath, FILE_OPEN_READ_ONLY);
						_KePrint("XENotes: open '%s' fd=%d\n", filePath, fd);
						char* content = NULL;
						if (fd >= 0) {
							char buf[32768];
							memset(buf, 0, sizeof(buf));
							size_t nread = _KeReadFile(fd, buf, sizeof(buf) - 1);
							_KePrint("XENotes: read %d bytes, buf[0]=%d buf[1]=%d buf[2]=%d\n",
								(int)nread, (int)(unsigned char)buf[0], (int)(unsigned char)buf[1], (int)(unsigned char)buf[2]);
							_KeCloseFile(fd);
							content = strdup(buf);
						} else {
							content = strdup("");
						}

						ChSidebarItem* item = ChSidebarAddItem(sec, noteTitle, NULL, 0, 0, NULL);
						item->OnSelect = OnNoteSelected;
						item->data = content;
						loadedCount++;
						_KePrint("XENotes: Loaded note '%s' content_len=%d\n", noteTitle, (int)strlen(content));
					}
				}
			}
			memset(dirent.filename, 0, sizeof(dirent.filename));
		}
		_KeCloseFile(dirfd);
		_KePrint("XENotes: Populated %d notes from /note\n", loadedCount);
	}

	if (loadedCount == 0) {
		ChSidebarItem* item = ChSidebarAddItem(sec, "Untitled Note", NULL, 0, 0, NULL);
		item->OnSelect = OnNoteSelected;
		item->data = strdup("");
		item->selected = 1;
	} else {
		sec->items[0].selected = 1;
	}

	currentNoteSection = 0;
	currentNoteIdx = 0;

	if (notesTitleBar) {
		strncpy(notesTitleBar->title, sec->items[0].label, SIDEBAR_MAX_LABEL - 1);
		notesTitleBar->title[SIDEBAR_MAX_LABEL - 1] = '\0';
		notesTitleBar->cursorIndex = strlen(notesTitleBar->title);
		notesTitleBar->focused = false;
	}
	if (notesEditor) {
		ChNotesEditorSetText(notesEditor, sec->items[0].data ? (char*)sec->items[0].data : (char*)"");
	}
}
