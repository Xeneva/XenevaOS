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
void HandleDirectoryNavigation(ChListView* lv, ChListItem* li);
void PathNavigateBack();
void PathEnterForward();
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

/*
 * RefreshFileView -- reiterate and add file to the list view
 * @param dirfd -- directory file descriptor
 * @param lview -- Pointer to list view
 */
void RefreshFileView(int dirfd, ChListView* lview) {
	XEDirectoryEntry* dirent = (XEDirectoryEntry*)malloc(sizeof(XEDirectoryEntry));
	memset(dirent, 0, sizeof(XEDirectoryEntry));

	/* short directories first*/
	while (1) {
		if (dirent->index == -1)
			break;
		int code = _KeReadDir(dirfd, dirent);
		if (code != -1) {
			if (dirent->flags & FILE_DIRECTORY) {
				if ((strcmp(dirent->filename, ".") == 0) || (strcmp(dirent->filename, "..") == 0)) {
					memset(dirent->filename, 0, 32);
					continue;
				}
				ChListItem* li = ChListViewAddItem(mainWin, lview, dirent->filename);
				ChListViewSetListItemIcon(li, dirico);
				li->ChListItemAction = DirListItemAction;
			}
		}
		memset(dirent->filename, 0, 32);
	}

	dirent->index = 0;

	/* short the files next*/
	while (1) {
		if (dirent->index == -1)
			break;
		int code = _KeReadDir(dirfd, dirent);
		if (code != -1) {
			if (dirent->flags & FILE_GENERAL) {
				if ((strcmp(dirent->filename, ".") == 0) || (strcmp(dirent->filename, "..") == 0)) {
					memset(dirent->filename, 0, 32);
					continue;
				}
				ChListItem* fi = ChListViewAddItem(mainWin, lv, dirent->filename);
				ChIcon* ico = GetIconByName(dirent->filename);
				ChListViewSetListItemIcon(fi, ico);
				fi->ChListItemAction = DocumentItemActionHandler;
			}
		}
		memset(dirent->filename, 0, 32);
	}

	free(dirent);
}

/* DirListItemAction -- directory item action handler
 * this handler is called whenever directory is encountered
 * by double click or keyboard return key event
 */
void DirListItemAction(ChListView* lv, ChListItem* li) {
    HandleDirectoryNavigation(lv, li);
    /* just jump to window event handler or
    * else, the app will crash --axiss */
    longjmp(mainWin->jump, 1);
}

/*
 * HandleDirectoryNavigation -- core logic for navigation
 */
void HandleDirectoryNavigation(ChListView* lv, ChListItem* li) {
	if (li == NULL)
		return;

	if (li->icon != NULL && li->icon == docico) {
		/* handle it as document */
		ExtensionManagerSpawn(li);
		return;
	}

	int len = strlen(path);
	char* dirname = (char*)malloc(strlen(li->itemText) + len + 2);
	strcpy(dirname, path);
	strcpy(dirname + len, li->itemText);
	free(path);
	path = (char*)malloc(strlen(dirname) + 2);
	strcpy(path, dirname);

	strcat(path, "/");
	ChListViewClear(lv);

	PrintParentDir(path);
	/* bug : needs to sleep inorder to get
	* the file descriptor for the desired path */
	_KeProcessSleep(10);

	int dirfd = _KeOpenDir(dirname);
	RefreshFileView(dirfd, lv);
	_KeCloseFile(dirfd);
	free(dirname);
	ChListViewRepaint(mainWin, lv);
	FileAddressBarRepaint(addressbar);
}

/*
 * PathNavigateBack -- navigate back to parent directory from
 * current directory
 */
void PathNavigateBack() {
	int len = strlen(path);

	/* check if we are already in root folder */
	if (len == 1 && (strcmp(path, "/") == 0))
		return;

	char* subpath = (char*)malloc(strlen(path) + 1);
	strcpy(subpath, path);
	if (len > 1 && subpath[len - 1] == '/')
		subpath[len - 1] = '\0';
	char* last_slash = strrchr(subpath, '/');
	if (last_slash) {
		*last_slash = '\0';
		if (strlen(subpath) == 0) strcpy(subpath, "/");
	}

	ChListViewClear(lv);

	/* bug : needs to sleep inorder to get
	* the file descriptor for the desired path */
	_KeProcessSleep(10);

	int dirfd = _KeOpenDir(subpath);
	RefreshFileView(dirfd, lv);

	_KeCloseFile(dirfd);
	ChListViewRepaint(mainWin, lv);

	free(path);
	if (strcmp(subpath, "/") != 0) {
		path = (char*)malloc(strlen(subpath) + 2);
		strcpy(path, subpath);
		strcat(path, "/");
	} else {
		path = (char*)malloc(2);
		strcpy(path, "/");
	}
	FileAddressBarRepaint(addressbar);
	free(subpath);
}

/*
 * PathEnterForward -- enter forward from current directory
 * to the directory from selected item
 */
void PathEnterForward() {
	HandleDirectoryNavigation(lv, ChListViewGetSelectedItem(lv));
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
	ChWindowAddWidget(mainWin, (ChWidget*)backbut);
	backbut->base.ChActionHandler = BackButtonClicked;

	ChButton* Enterbut = ChCreateButton(60 + 10, 34, 50, 35, "Enter");
	ChWindowAddWidget(mainWin, (ChWidget*)Enterbut);
	Enterbut->base.ChActionHandler = EnterClicked;

	addressbar = FileCreateAddressBar(130, 34, mainWin->info->width - 140, 35);
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
	ListView* lv1 = ChListViewCreate(
		170, 100, mainWin->info->width - (170 + 2), mainWin->info->height - 120, 24);
	ListViewAddColumn(lv1, "Name", 200, 60);
	ListViewAddColumn(lv1, "Size", 100, 50);
	ListViewAddColumn(lv1, "Type", 100, 50);
	ListViewAddColumn(lv1, "Modified", 220, 80);
	ListViewAddColumn(lv1, "Access", 100, 50);

	const char* row1[] = {"kernel.bin", "2.1 MB", "Binary", "2026-06-18 14:02", "root"};
	ListItem* l1 = ListViewAddItem(lv1, row1, 5, NULL);

	const char* row2[] = {"doom.exe", "880 KB", "exec", "2026-06-17 09:41", "user"};
	ListItem* l2 = ListViewAddItem(lv1, row2, 5, NULL);

	const char* row3[] = {"xeldr.exe", "78 KB", "exec", "2026-06-17 09:41", "root"};
	ListItem* l3 = ListViewAddItem(lv1, row3, 5, NULL);

	const char* row4[] = {"audrv.cnf", "1 KB", "config", "2026-06-17 09:41", "user"};
	ListItem* l4 = ListViewAddItem(lv1, row4, 5, NULL);

	const char* row5[] = {"virtblk.dll", "55 KB", "system", "2026-06-17 09:41", "root"};
	ListItem* l5 = ListViewAddItem(lv1, row5, 5, NULL);

	const char* row6[] = {"virtnet.dll", "67 KB", "system", "2026-06-17 09:41", "root"};
	ListItem* l6 = ListViewAddItem(lv1, row6, 5, NULL);

	ChWindowAddWidget(mainWin, (ChWidget*)mb);
	//ChWindowAddWidget(mainWin, (ChWidget*)lv);
	//ChWindowAddWidget(mainWin, (ChWidget*)sp);
	ChWindowAddWidget(mainWin, (ChWidget*)lv1);

	FileManagerPartitionList* partitionList =
		FileManagerCreatePartitionList(10,
									   100,
									   mainWin->info->width - (20 + (mainWin->info->width - 170)),
									   mainWin->info->height - 120);
	ChWindowAddWidget(mainWin, (ChWidget*)partitionList);

	int dirfd = _KeOpenDir("/");

	path = (char*)malloc(strlen("/") + 1);
	strcpy(path, "/");

	dirico = ChCreateIcon();
	ChIconOpen(dirico, "/icons/dir.bmp");
	ChIconRead(dirico);

	docico = ChCreateIcon();
	ChIconOpen(docico, "/icons/doc.bmp");
	ChIconRead(docico);

	exeico = ChCreateIcon();
	ChIconOpen(exeico, "/icons/exe.bmp");
	ChIconRead(exeico);
	ListViewSetItemIcon(lv1, 1, exeico, exeico->image.width, exeico->image.height);

	imgico = ChCreateIcon();
	ChIconOpen(imgico, "/icons/img.bmp");
	ChIconRead(imgico);

	dllico = ChCreateIcon();
	ChIconOpen(dllico, "/icons/dll.bmp");
	ChIconRead(dllico);
	ListViewSetItemIcon(lv1, 4, dllico, dllico->image.width, dllico->image.height);
	ListViewSetItemIcon(lv1, 5, dllico, dllico->image.width, dllico->image.height);

	ChIcon* drive = ChCreateIcon();
	ChIconOpen(drive, "/icons/drive.bmp");
	ChIconRead(drive);

	for (int i = 0; i < 20; i++) {
		const char* row7[] = {"virus.dll", "67 KB", "system", "2026-06-17 09:41", "root"};
		ListItem* l7 = ListViewAddItem(lv1, row7, 5, NULL);
		int index = 6 + i;
		ListViewSetItemIcon(lv1, index, dllico, dllico->image.width, dllico->image.height);
	}

	_KePrint("refreshing file view \r\n");
	RefreshFileView(dirfd, lv);

	/* first store the root address */
	history = (char*)malloc(strlen("/") + 1);
	strcpy(history, "/");

	_KeCloseFile(dirfd);

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
			FileManagerPartitionButton* pbut = FileManageCreatePartitionButton(partitionList);
			pbut->icon = drive;
			pbut->mounted = true;
			strcpy(pbut->partitionName, diskInfo.diskname);
			strcpy(pbut->guidString, partitionInfo.mountedName);
			memset(&partitionInfo, 0, sizeof(XEVDiskPartitionInfo));
		}

		if (diskInfo.num_partition == 0) {
			FileManagerPartitionButton* pbut = FileManageCreatePartitionButton(partitionList);
			pbut->icon = drive;
			pbut->mounted = false;
			strcpy(pbut->partitionName, diskInfo.diskname);
			char buf[32];
			strcpy(buf, "Raw Drive");
			memcpy(pbut->guidString, buf, 32);
		}
		memset(&diskInfo, 0, sizeof(XEVDiskInfo));
	}

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