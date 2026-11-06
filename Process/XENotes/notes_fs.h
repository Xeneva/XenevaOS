#ifndef __NOTES_FS_H__
#define __NOTES_FS_H__

#include <stdint.h>
#include <chitralekha.h>
#include <widgets/window.h>

/* Initialize notes filesystem (ensures /note exists) */
void NotesInitFS();

/* Save note content to disk by title in /note/<NAME>.NT */
void NotesSaveNote(const char* title, const char* content);

/* Delete note file from disk by title */
void NotesDeleteNote(const char* title);

/* Load a note file into the text box */
void NotesLoadFile(const char* filename, ChWindow* win);

/* Save the current text box content to a file */
void NotesSaveFile(const char* filename);

/* Populate the sidebar with note files from the disk */
void NotesPopulateSidebar(ChWindow* win);

#endif
