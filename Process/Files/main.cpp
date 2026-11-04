/**
* BSD 2-Clause License
*
* Copyright (c) 2024, Manas Kamal Choudhury
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

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/button.h>
#include <widgets/scrollpane.h>
#include <widgets/slider.h>
#include <widgets/progress.h>
#include <keycode.h>
#include <widgets/window.h>
#include <widgets/msgbox.h>
#include <widgets/view.h>
#include <string.h>
#include <widgets/menubar.h>
#include <stdlib.h>
#include <time.h>
#include <widgets/menu.h>
#include "partition.h"
#include <widgets/base.h>
#include <ctype.h>
#include <widgets/scrollbar.h>
#include <widgets/listview.h>

ChitralekhaApp* app;
ChWindow* mainWin;
ChWindow* win2;
jmp_buf jmp;
ChPopupMenu* pm;
ChListView* lv;
ListView* fileListView;
ChIcon* dirico;
ChIcon* docico;
ChIcon* exeico;
ChIcon* imgico;
ChIcon* dllico;
char* path;
char* history;

typedef struct _address_bar_ {
	ChWidget base;
} FileAddressBar;

FileAddressBar* addressbar;

void DirListItemAction(ChListView* lv, ChListItem* li);
void DocumentItemActionHandler(ChListView* lv, ChListItem* li);
extern void ExtensionManagerSpawn(ChListItem* li);
void RefreshFileView(const char* dirpath, ListView* lview);
void HandleDirectoryNavigation(ListView* lv, ListItem* li);
void OnListViewItemAction(ListView* lv, int index, ListItem* item);
void PathNavigateBack();
void PathEnterForward();
void PartitionSelectedHandler(FileManagerPartitionButton* pbut);
/*
 * FileAddressBarMouseEvent -- handle mouse event for address bar
 */
void FileAddressBarMouseEvent(ChWidget* wid, ChWindow* win, int x, int y, int button) {
	if (wid->hover) {
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
		wid->hoverPainted = true;
	}
}

#define FILE_ADDRESS_BAR_COLOR 0xFFFFFFFF

void FileAddressBarPaintHandler(ChWidget* wid, ChWindow* win) {
	FileAddressBar* bar = (FileAddressBar*)wid;
	ChDrawRect(
		win->canv, bar->base.x, bar->base.y, bar->base.w, bar->base.h, FILE_ADDRESS_BAR_COLOR);
	ChDrawRectUnfilled(win->canv, bar->base.x, bar->base.y, bar->base.w, bar->base.h, GRAY);
	ChFontSetSize(win->app->baseFont, 13);
	ChFontDrawText(
		win->canv, win->app->baseFont, path, bar->base.x + 10, bar->base.y + 22, 15, GRAY);
	if (wid->hover) {
		ChDrawRectUnfilled(
			win->canv, bar->base.x, bar->base.y, bar->base.w, bar->base.h, 0xFF4067BA);
		ChDrawRectUnfilled(win->canv,
						   bar->base.x + 1,
						   bar->base.y + 1,
						   bar->base.w - 2,
						   bar->base.h - 2,
						   0xFF6689D5);
	}
}

void FileAddressBarRepaint(FileAddressBar* bar) {
	bar->base.ChPaintHandler((ChWidget*)bar, mainWin);
	ChWindowUpdate(mainWin, bar->base.x, bar->base.y, bar->base.w, bar->base.h, 0, 1);
}

void FileAddressBarDestroy(ChWidget* wid, ChWindow* win) {}
FileAddressBar* FileCreateAddressBar(int x, int y, int w, int h) {
	FileAddressBar* addrbar = (FileAddressBar*)malloc(sizeof(FileAddressBar));
	memset(addrbar, 0, sizeof(FileAddressBar));
	addrbar->base.x = CHITRALEKHA_WINDOW_DEFAULT_PAD_X + x;
	addrbar->base.y = CHITRALEKHA_WINDOW_DEFAULT_PAD_Y + y;
	addrbar->base.w = w;
	addrbar->base.h = h;
	addrbar->base.ChMouseEvent = FileAddressBarMouseEvent;
	addrbar->base.ChPaintHandler = FileAddressBarPaintHandler;
	addrbar->base.ChDestroy = FileAddressBarDestroy;
	return addrbar;
}

void FileHandleKeyEvent(char c) {
	switch (c) {
	case KEY_BACKSPACE:
		PathNavigateBack();
		break;
	case KEY_RETURN:
		PathEnterForward();
		break;
	default:
		break;
	}
}
/*
 * WindowHandleMessage -- handles incoming deodhai messages
 * @param e -- PostBox event message structure
 */
void WindowHandleMessage(PostEvent* e) {
	switch (e->type) {
	/* handle mouse event from deodhai */
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
		/* handle key events from deodhai */
	case DEODHAI_REPLY_KEY_EVENT: {
		int code = e->dword;
		ChitralekhaProcessKey(code);
		char c = ChitralekhaKeyToASCII(code);
		FileHandleKeyEvent(c);
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
	case DEODHAI_REPLY_MOUSE_LEAVE: {
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	}
}

/*
 * VerifyDocExtension -- checks if given document name
 * matches with given extension
 * @param filename -- filename
 * @param extension -- desired extension
 */
bool VerifyDocExtension(char* filename, char* extension) {
	int i = strlen(filename);
	int j = strlen(extension);

	do {
		if (filename[i] != extension[j])
			break;
		if (j == 0)
			return 1;
		if (i == 0)
			break;
		i--;
		j--;
	} while (1);
	return false;
}

void PrintParentDir(char* pathname) {
	int len = strlen(pathname);
	char dir[16];
	memset(dir, 0, 16);
	char* subpath = (char*)malloc(strlen(pathname) + 1);
	strcpy(subpath, pathname);
    if (len > 1 && subpath[len - 1] == '/')
        subpath[len - 1] = '\0';

    char* last_slash = strrchr(subpath, '/');
    if (last_slash) {
        strncpy(dir, last_slash + 1, 15);
    } else {
        strncpy(dir, subpath, 15);
    }

	_KePrint("Dir %s \r\n", dir);
	free(subpath);
}

const char* get_extension(const char* filename) {
	const char* dot = strrchr(filename, '.');
	if (!dot || dot == filename)
		return "";
	return dot + 1;
}

int stricmp_custom(const char* a, const char* b) {
	while (*a && *b) {
		if (tolower(*a) != tolower(*b))
			return 0;
		a++;
		b++;
	}
	return *a == *b;
}

ChIcon* GetIconByName(char* filename) {
	const char* ext = get_extension(filename);

	if (stricmp_custom(ext, "bmp") || stricmp_custom(ext, "jpg") || stricmp_custom(ext, "png")) {
		return imgico;
	}

	if (stricmp_custom(ext, "exe"))
		return exeico;

	if (stricmp_custom(ext, "dll"))
		return dllico;

	return docico;
}

typedef struct _FileEntryItem_ {
	char filename[64];
	char size_str[32];
	char type_str[32];
	char date_str[32];
	char access_str[16];
	bool is_dir;
	ChIcon* icon;
} FileEntryItem;

static void AddPendingFileEntry(FileEntryItem** list, size_t* count, size_t* cap,
								const char* name, const char* size, const char* type,
								const char* date, const char* access, bool is_dir, ChIcon* ico) {
	if (*count >= *cap) {
		*cap = (*cap == 0) ? 32 : (*cap * 2);
		*list = (FileEntryItem*)realloc(*list, sizeof(FileEntryItem) * (*cap));
	}
	FileEntryItem* it = &(*list)[(*count)++];
	strncpy(it->filename, name, sizeof(it->filename) - 1);
	it->filename[sizeof(it->filename) - 1] = '\0';
	strncpy(it->size_str, size, sizeof(it->size_str) - 1);
	it->size_str[sizeof(it->size_str) - 1] = '\0';
	strncpy(it->type_str, type, sizeof(it->type_str) - 1);
	it->type_str[sizeof(it->type_str) - 1] = '\0';
	strncpy(it->date_str, date, sizeof(it->date_str) - 1);
	it->date_str[sizeof(it->date_str) - 1] = '\0';
	strncpy(it->access_str, access, sizeof(it->access_str) - 1);
	it->access_str[sizeof(it->access_str) - 1] = '\0';
	it->is_dir = is_dir;
	it->icon = ico;
}

/*
 * RefreshFileView -- iterate directory and add items to the ListView
 * @param dirpath -- directory path to enumerate
 * @param lview -- pointer to ListView
 */
void RefreshFileView(const char* dirpath, ListView* lview) {
	if (!lview || !dirpath)
		return;

	ListViewClear(lview);

	int dirfd = _KeOpenDir((char*)dirpath);
	if (dirfd < 0) {
		printf("RefreshFileView: failed to open dir %s (fd=%d)\n", dirpath, dirfd);
		return;
	}

	FileEntryItem* dirs = NULL;
	size_t dir_count = 0, dir_cap = 0;
	FileEntryItem* files = NULL;
	size_t file_count = 0, file_cap = 0;

	XEDirectoryEntry dirent;
	memset(&dirent, 0, sizeof(XEDirectoryEntry));

	int prev_index = -1;
	int loop_guard = 0;
	while (loop_guard++ < 4096) {
		if (dirent.index == -1)
			break;
		prev_index = dirent.index;
		dirent.flags = 0;
		dirent.size = 0;
		dirent.date = 0;
		dirent.time = 0;
		memset(dirent.filename, 0, sizeof(dirent.filename));

		int code = _KeReadDir(dirfd, &dirent);
		if (dirent.index == -1)
			break;
		if (code == -1 && dirent.index == prev_index)
			break;
		if (code == -1)
			continue;

		if (dirent.filename[0] == '\0')
			continue;

		if (strcmp(dirent.filename, ".") == 0 || strcmp(dirent.filename, "..") == 0)
			continue;

		char date_str[32];
		if (dirent.date != 0) {
			snprintf(date_str,
					 sizeof(date_str),
					 "%04d-%02d-%02d %02d:%02d",
					 ((dirent.date >> 9) & 0x7F) + 1980,
					 (dirent.date >> 5) & 0x0F,
					 dirent.date & 0x1F,
					 (dirent.time >> 11) & 0x1F,
					 (dirent.time >> 5) & 0x3F);
		} else {
			strcpy(date_str, "----");
		}

		if (dirent.flags & FILE_DIRECTORY) {
			AddPendingFileEntry(&dirs, &dir_count, &dir_cap,
								dirent.filename, "<DIR>", "Folder",
								date_str, "rwxr-xr-x", true, dirico);
		} else if (dirent.flags & FILE_GENERAL) {
			char size_str[32];
			if (dirent.size < 1024)
				snprintf(size_str, sizeof(size_str), "%d B", dirent.size);
			else if (dirent.size < 1048576)
				snprintf(size_str, sizeof(size_str), "%d KB", dirent.size / 1024);
			else
				snprintf(size_str, sizeof(size_str), "%d MB", dirent.size / 1048576);

			const char* ext = get_extension(dirent.filename);
			const char* type_str = "File";
			ChIcon* ico = docico;
			if (stricmp_custom(ext, "exe")) {
				type_str = "Application";
				ico = exeico ? exeico : docico;
			} else if (stricmp_custom(ext, "dll")) {
				type_str = "Dynamic Lib";
				ico = dllico ? dllico : docico;
			} else if (stricmp_custom(ext, "bmp") || stricmp_custom(ext, "jpg") ||
					   stricmp_custom(ext, "png")) {
				type_str = "Image";
				ico = imgico ? imgico : docico;
			} else if (stricmp_custom(ext, "cnf") || stricmp_custom(ext, "conf") ||
					   stricmp_custom(ext, "txt")) {
				type_str = "Config / Text";
				ico = docico;
			} else if (stricmp_custom(ext, "wav") || stricmp_custom(ext, "mp3")) {
				type_str = "Audio";
				ico = docico;
			} else if (stricmp_custom(ext, "ttf")) {
				type_str = "Font";
				ico = docico;
			}

			AddPendingFileEntry(&files, &file_count, &file_cap,
								dirent.filename, size_str, type_str,
								date_str, "rw-r--r--", false, ico);
		}
	}

	_KeCloseFile(dirfd);

	/* Add all directories first */
	for (size_t i = 0; i < dir_count; i++) {
		const char* row_cells[] = {
			dirs[i].filename,
			dirs[i].size_str,
			dirs[i].type_str,
			dirs[i].date_str,
			dirs[i].access_str
		};
		ListViewAddItem(lview, row_cells, 5, (void*)1);
		if (dirs[i].icon) {
			ListViewSetItemIcon(lview,
								lview->itemCount - 1,
								dirs[i].icon,
								dirs[i].icon->image.width,
								dirs[i].icon->image.height);
		}
	}

	/* Add all regular files second */
	for (size_t i = 0; i < file_count; i++) {
		const char* row_cells[] = {
			files[i].filename,
			files[i].size_str,
			files[i].type_str,
			files[i].date_str,
			files[i].access_str
		};
		ListViewAddItem(lview, row_cells, 5, (void*)0);
		if (files[i].icon) {
			ListViewSetItemIcon(lview,
								lview->itemCount - 1,
								files[i].icon,
								files[i].icon->image.width,
								files[i].icon->image.height);
		}
	}

	if (dirs)
		free(dirs);
	if (files)
		free(files);

	if (mainWin && lview->base.ChPaintHandler) {
		lview->base.ChPaintHandler((ChWidget*)lview, mainWin);
		ChWindowUpdate(mainWin, lview->base.x, lview->base.y, lview->base.w, lview->base.h, 0, 1);
	}
}

static char* safe_strdup(const char* s) {
	if (!s)
		return NULL;
	char* copy = (char*)malloc(strlen(s) + 1);
	if (copy)
		strcpy(copy, s);
	return copy;
}

static void NormalizePath(const char* base, const char* sub, char* out, size_t out_max) {
	if (!out || out_max == 0)
		return;

	char temp[512];
	if (!base || base[0] == '\0')
		base = "/";

	if (!sub || sub[0] == '\0') {
		snprintf(temp, sizeof(temp), "%s", base);
	} else if (strcmp(base, "/") == 0) {
		snprintf(temp, sizeof(temp), "/%s", sub);
	} else {
		snprintf(temp, sizeof(temp), "%s/%s", base, sub);
	}

	size_t r = 0, w = 0;
	if (temp[0] != '/')
		out[w++] = '/';

	while (temp[r] != '\0' && w < out_max - 1) {
		if (temp[r] == '/') {
			if (w == 0 || out[w - 1] != '/')
				out[w++] = '/';
		} else {
			out[w++] = temp[r];
		}
		r++;
	}
	while (w > 1 && out[w - 1] == '/')
		w--;
	out[w] = '\0';
}

typedef int (*ext_handler)(const char* filename);

struct extension_list {
	char ext[8];
	ext_handler handler;
};

int FileOpenExe(const char* filename) {
	char fullpath[256];
	strncpy(fullpath, filename, sizeof(fullpath) - 1);
	fullpath[sizeof(fullpath) - 1] = '\0';
	char* dot = strrchr(fullpath, '.');
	if (dot) {
		for (char* p = dot; *p; p++)
			*p = tolower(*p);
	}
	const char* slash = strrchr(fullpath, '/');
	const char* proc_name = slash ? slash + 1 : fullpath;
	int pid = _KeCreateProcess(0, (char*)proc_name);
	if (pid > 0)
		_KeProcessLoadExec(pid, fullpath, 0, NULL);
	return 0;
}

int FileOpenAudio(const char* filename) {
	char* argv[1] = { (char*)filename };
	int pid = _KeCreateProcess(0, (char*)"accent");
	if (pid > 0)
		_KeProcessLoadExec(pid, (char*)"/audplr.exe", 1, argv);
	return 0;
}

int FileOpenImage(const char* filename) {
	char* argv[1] = { (char*)filename };
	int pid = _KeCreateProcess(0, (char*)"glimpse");
	if (pid > 0)
		_KeProcessLoadExec(pid, (char*)"/glimpse.exe", 1, argv);
	return 0;
}

static struct extension_list g_extension_table[] = {
	{ ".exe", FileOpenExe },
	{ ".wav", FileOpenAudio },
	{ ".mp3", FileOpenAudio },
	{ ".bmp", FileOpenImage },
	{ ".jpg", FileOpenImage },
	{ ".jpeg", FileOpenImage },
	{ ".png", FileOpenImage },
};

#define EXTENSION_TABLE_SIZE (sizeof(g_extension_table) / sizeof(g_extension_table[0]))

/*
 * HandleDirectoryNavigation -- core logic for navigating into subdirectories or executing files
 */
void HandleDirectoryNavigation(ListView* lv, ListItem* li) {
	if (!lv || !li)
		return;

	bool is_dir = (li->data == (void*)1);
	if (!is_dir) {
		char fullpath[256];
		NormalizePath(path, li->cells[0], fullpath, sizeof(fullpath));
		const char* dot = strrchr(li->cells[0], '.');
		if (dot) {
			for (size_t i = 0; i < EXTENSION_TABLE_SIZE; i++) {
				if (stricmp_custom(dot, g_extension_table[i].ext) && g_extension_table[i].handler) {
					g_extension_table[i].handler(fullpath);
					return;
				}
			}
		}
		return;
	}

	char target[256];
	NormalizePath(path, li->cells[0], target, sizeof(target));

	int test_fd = _KeOpenDir(target);
	if (test_fd < 0) {
		printf("HandleDirectoryNavigation: failed to open dir %s\n", target);
		return;
	}
	_KeCloseFile(test_fd);

	free(path);
	path = safe_strdup(target);

	RefreshFileView(path, lv);
	if (addressbar)
		FileAddressBarRepaint(addressbar);
}

/*
 * OnListViewItemAction -- callback when an item is double-clicked or activated
 */
void OnListViewItemAction(ListView* lv, int index, ListItem* item) {
	(void)index;
	if (item)
		HandleDirectoryNavigation(lv, item);
}

/*
 * PathNavigateBack -- navigate back to parent directory from current directory
 */
void PathNavigateBack() {
	if (!path || strcmp(path, "/") == 0)
		return;

	char temp[256];
	strncpy(temp, path, sizeof(temp) - 1);
	temp[sizeof(temp) - 1] = '\0';

	char* last_slash = strrchr(temp, '/');
	if (last_slash == temp) {
		temp[1] = '\0';
	} else if (last_slash) {
		*last_slash = '\0';
	} else {
		strcpy(temp, "/");
	}

	free(path);
	path = safe_strdup(temp);

	RefreshFileView(path, fileListView);
	if (addressbar)
		FileAddressBarRepaint(addressbar);
}

/*
 * PathEnterForward -- enter forward into selected item
 */
void PathEnterForward() {
	if (fileListView && fileListView->selectedIndex >= 0 &&
		fileListView->selectedIndex < fileListView->itemCount) {
		HandleDirectoryNavigation(fileListView, &fileListView->item[fileListView->selectedIndex]);
	}
}

/*
 * PartitionSelectedHandler -- action handler when a partition button is clicked
 */
void PartitionSelectedHandler(FileManagerPartitionButton* pbut) {
	if (!pbut)
		return;

	if (!pbut->mounted) {
		ChMessageBox* mb = ChCreateMessageBox(mainWin,
											  "Drive Not Mounted",
											  "This disk or partition is not mounted.",
											  MSGBOX_TYPE_ONLYCLOSE,
											  MSGBOX_ICON_INFORMATION);
		ChMessageBoxShow(mb);
		return;
	}

	char target[256];
	NormalizePath(pbut->guidString, NULL, target, sizeof(target));

	int test_fd = _KeOpenDir(target);
	if (test_fd < 0) {
		printf("PartitionSelectedHandler: failed to open %s\n", target);
		return;
	}
	_KeCloseFile(test_fd);

	free(path);
	path = safe_strdup(target);

	RefreshFileView(path, fileListView);
	if (addressbar)
		FileAddressBarRepaint(addressbar);
}

/*
 * BackButtonClicked -- back button click event
 * @param wid -- Pointer to Button widget (passed by system)
 * @param win -- Pointer to Main Window (passed by system)
 */
void BackButtonClicked(ChWidget* wid, ChWindow* win) {
	PathNavigateBack();
}

/*
 * DocumentItemActionHandler -- action handler for document
 * item
 */
void DocumentItemActionHandler(ChListView* lv, ChListItem* li) {
	ExtensionManagerSpawn(li);
}

void AboutClicked(ChWidget* wid, ChWindow* win) {
	ChMessageBox* mb = ChCreateMessageBox(mainWin,
										  "File Explorer v1.0",
										  "File Explorer v1.0 for XenevaOS !!",
										  MSGBOX_TYPE_ONLYCLOSE,
										  MSGBOX_ICON_SUCCESS);
	ChMessageBoxShow(mb);
}
/*
 * ExitItemClicked -- exit menu item action
 * handler
 * @param wid -- Pointer to widget
 * @param win -- Pointer to main window
 */
void ExitItemClicked(ChWidget* wid, ChWindow* win) {
	/* exit the application*/
	ChWindowCloseWindow(mainWin);
}

void M2_2Clicked(ChWidget* wid, ChWindow* win) {
	/* exit the application*/
	_KePrint("M2_2Clicked \r\n");
	int proc_id = _KeCreateProcess(0, "file");
	_KeProcessLoadExec(proc_id, "/file.exe", 0, 0);
	longjmp(mainWin->jump, 1);
}

/*
 * EnterClicked -- action handler for enter button
 */
void EnterClicked(ChWidget* wid, ChWindow* win) {
	PathEnterForward();
}

/*
 * FileManagerGetCurrentPath -- get the current path 
 */
char* FileManagerGetCurrentPath() {
	return path;
}

void _FileMngrThr() {
	_KePrint("File manager thread running \r\n");
	while (1) {
		_KeProcessSleep(1000);
	}
}
/*
* main -- main entry
*/
int main(int argc, char* argv[]) {
	app = ChitralekhaStartApp(argc, argv);
	mainWin = ChCreateWindow(app, WINDOW_FLAG_MOVABLE, "Files", 100, 100, 660, 500);

	win2 = NULL;

	mainWin->color = 0x99808080;

	pm = NULL;

	ChButton* backbut = ChCreateButton(
		10,
		34,
		50,
		35,
		"Back"); //mainWin->info->width / 2 - 100 / 2, mainWin->info->height / 2 - 75/2
	backbut->base.visible = true;
	ChWindowAddWidget(mainWin, (ChWidget*)backbut);
	backbut->base.ChActionHandler = BackButtonClicked;

	ChButton* Enterbut = ChCreateButton(60 + 10, 34, 50, 35, "Enter");
	Enterbut->base.visible = true;
	ChWindowAddWidget(mainWin, (ChWidget*)Enterbut);
	Enterbut->base.ChActionHandler = EnterClicked;

	addressbar = FileCreateAddressBar(130, 34, mainWin->info->width - 140, 35);
	addressbar->base.visible = true;
	ChWindowAddWidget(mainWin, (ChWidget*)addressbar);

	ChMenubar* mb = ChCreateMenubar(mainWin);

	ChMenuButton* file = ChCreateMenubutton(mb, "File");
	ChMenubarAddButton(mb, file);
	ChMenuButton* edit = ChCreateMenubutton(mb, "Help");
	ChMenubarAddButton(mb, edit);

	pm = ChCreatePopupMenu(mainWin, 0);
	ChMenuItem* item = ChCreateMenuItem("Exit", pm);
	item->wid.ChActionHandler = ExitItemClicked;
	ChMenuButtonAddMenu(file, pm);

	/* for testing purpose just create some menus */
	ChPopupMenu* help = ChCreatePopupMenu(mainWin, 0);
	ChMenuItem* about = ChCreateMenuItem("About", help);
	about->wid.ChActionHandler = AboutClicked;
	ChMenuButtonAddMenu(edit, help);

	ChScrollPane* sp = ChCreateScrollPane(
		mainWin, 170, 100, mainWin->info->width - (170 + 2), mainWin->info->height - 120);
	lv = ChCreateListView(170, 100, mainWin->info->width - (170 + 2), mainWin->info->height - 120);
	ChListViewSetScrollpane(lv, sp);

	//ChScrollBar* sb = ChCreateScrollBar(mainWin->info->width - 16, 26, 16, mainWin->info->height - 26, SCROLLBAR_ORIENTATION_VERTICAL);
	//ChScrollBarSetRange(sb, 0, 800, mainWin->info->height - 26);
	//ChScrollBar* sb1 = ChCreateScrollBar(0, mainWin->info->height - 16, mainWin->info->width, 16, SCROLLBAR_ORIENTATION_HORIZONTAL);
	//ChScrollBarSetRange(sb1, 0, 800, mainWin->info->width);
	fileListView = ChListViewCreate(
		170, 100, mainWin->info->width - (170 + 2), mainWin->info->height - 120, 24);
	ListViewAddColumn(fileListView, "Name", 200, 60);
	ListViewAddColumn(fileListView, "Size", 100, 50);
	ListViewAddColumn(fileListView, "Type", 100, 50);
	ListViewAddColumn(fileListView, "Modified", 220, 80);
	ListViewAddColumn(fileListView, "Access", 100, 50);
	ListViewSetActionHandler(fileListView, OnListViewItemAction);

	ChWindowAddWidget(mainWin, (ChWidget*)mb);
	//ChWindowAddWidget(mainWin, (ChWidget*)lv);
	//ChWindowAddWidget(mainWin, (ChWidget*)sp);
	fileListView->base.visible = true;
	ChWindowAddWidget(mainWin, (ChWidget*)fileListView);

	FileManagerPartitionList* partitionList =
		FileManagerCreatePartitionList(10,
									   100,
									   mainWin->info->width - (20 + (mainWin->info->width - 170)),
									   mainWin->info->height - 120);
	partitionList->wid.visible = true;
	partitionList->onPartitionSelected = PartitionSelectedHandler;
	ChWindowAddWidget(mainWin, (ChWidget*)partitionList);

	dirico = ChCreateIcon();
	ChIconOpen(dirico, "/icons/dir.bmp");
	ChIconRead(dirico);

	docico = ChCreateIcon();
	ChIconOpen(docico, "/icons/doc.bmp");
	ChIconRead(docico);

	exeico = ChCreateIcon();
	ChIconOpen(exeico, "/icons/exe.bmp");
	ChIconRead(exeico);

	imgico = ChCreateIcon();
	ChIconOpen(imgico, "/icons/img.bmp");
	ChIconRead(imgico);

	dllico = ChCreateIcon();
	ChIconOpen(dllico, "/icons/dll.bmp");
	ChIconRead(dllico);

	ChIcon* drive = ChCreateIcon();
	ChIconOpen(drive, "/icons/drive.bmp");
	ChIconRead(drive);

	/* Always add the primary System root partition first */
	FileManagerPartitionButton* rootBut = FileManageCreatePartitionButton(partitionList);
	rootBut->icon = drive;
	rootBut->mounted = true;
	rootBut->selected = true;
	partitionList->selectedButton = rootBut;
	strcpy(rootBut->partitionName, "System");
	strcpy(rootBut->guidString, "/");

	XEVDiskInfo diskInfo;
	memset(&diskInfo, 0, sizeof(XEVDiskInfo));
	XEVDiskPartitionInfo partitionInfo;
	memset(&partitionInfo, 0, sizeof(XEVDiskPartitionInfo));

	for (int i = 0; i < XE_MAX_STORAGE_DEVICE; i++) {
		int ret = _KeGetStorageDiskInfo(i, &diskInfo);
		if (ret == -1)
			break;
		printf("\nStorage Found %s \r\n", diskInfo.diskname);
		printf("Number of Partitions -> %d \r\n", diskInfo.num_partition);
		for (int j = 0; j < diskInfo.num_partition; j++) {
			int ret2 = _KeGetStoragePartitionInfo(i, j, &partitionInfo);
			if (ret2 == -1)
				break;
			printf("Partition Mounted to -> %s \r\n", partitionInfo.mountedName);
			printf("GUID -> %x-%x-%x\r\n",
				   partitionInfo.partitionGUID.Data1,
				   partitionInfo.partitionGUID.Data2,
				   partitionInfo.partitionGUID.Data3);
			for (int k = 0; k < 8; k++)
				printf("-%x", partitionInfo.partitionGUID.Data4[k]);

			if (strcmp(partitionInfo.mountedName, "/") == 0) {
				strcpy(rootBut->partitionName, diskInfo.diskname);
			} else {
				FileManagerPartitionButton* pbut = FileManageCreatePartitionButton(partitionList);
				pbut->icon = drive;
				if (partitionInfo.mountedName[0] != '\0') {
					pbut->mounted = true;
					strcpy(pbut->guidString, partitionInfo.mountedName);
				} else {
					pbut->mounted = false;
					strcpy(pbut->guidString, "Raw Drive");
				}
				strcpy(pbut->partitionName, diskInfo.diskname);
			}
			memset(&partitionInfo, 0, sizeof(XEVDiskPartitionInfo));
		}

		if (diskInfo.num_partition == 0) {
			int ret2 = _KeGetStoragePartitionInfo(i, 0, &partitionInfo);
			if (ret2 != -1 && partitionInfo.mountedName[0] != '\0') {
				if (strcmp(partitionInfo.mountedName, "/") == 0) {
					strcpy(rootBut->partitionName, diskInfo.diskname);
				} else {
					FileManagerPartitionButton* pbut = FileManageCreatePartitionButton(partitionList);
					pbut->icon = drive;
					pbut->mounted = true;
					strcpy(pbut->partitionName, diskInfo.diskname);
					strcpy(pbut->guidString, partitionInfo.mountedName);
				}
			} else {
				FileManagerPartitionButton* pbut = FileManageCreatePartitionButton(partitionList);
				pbut->icon = drive;
				pbut->mounted = false;
				strcpy(pbut->partitionName, diskInfo.diskname);
				strcpy(pbut->guidString, "Raw Drive");
			}
			memset(&partitionInfo, 0, sizeof(XEVDiskPartitionInfo));
		}
		memset(&diskInfo, 0, sizeof(XEVDiskInfo));
	}

	path = (char*)malloc(2);
	strcpy(path, "/");

	history = (char*)malloc(2);
	strcpy(history, "/");

	_KePrint("refreshing file view \r\n");
	RefreshFileView(path, fileListView);

	ChWindowPaint(mainWin);

	ChWindowBroadcastIcon(app, "/icons/file.bmp");

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));

	/* needs to set jmp environment, if a subwindow
	 * get closed it will jump here for continuing
	 * the application
	 */
	setjmp(mainWin->jump);
	double progress = 0;
	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		WindowHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
	}
}