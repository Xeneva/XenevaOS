#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/_ketimer.h>
#include <sys/_ketime.h>
#include <chitralekha.h>
#include <sys/iocodes.h>
#include <widgets/base.h>
#include <keycode.h>
#include <widgets/window.h>
#include "appgrid.h"
#include "launcher.h"
#include "button.h"
#include "searchbar.h"
#include <ctype.h>
#include "search.h"
#include "pagebutton.h"
#include "pwbutton.h"
#ifdef __XENEVA_UNIKERNEL__
int XELnchMain(int argc, char* arv[]);
extern "C" void XELnchThread() {
	XELnchMain(0, NULL);
}
#endif
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

ChitralekhaApp* app;
ChWindow* win;
AppGrid* mainGrid;
int screen_h;
int screen_w;
int bpp;
int graphicsFd;
int launcher_w, launcher_h;
static int _initpipe;
XESearchBar* searchBar;

/* the main painting code */
void XELauncherPaint(ChWindow* win) {
	_KePrint("Paining launcher \r\n");
	ChDrawRect(win->canv, 0, 0, win->info->width, win->info->height, LAUNCHER_BACKGROUND_COLOR);

	_KePrint("Painging all widgets \r\n");
	for (int i = 0; i < win->widgets->pointer; i++) {
		ChWidget* wid = (ChWidget*)list_get_at(win->widgets, i);
		if (wid)
			if (wid->ChPaintHandler)
				wid->ChPaintHandler(wid, win);
	}
	_KePrint("Widget painted \r\n");
	AppGridPaint(mainGrid, win);
	ChDrawRectUnfilled(win->canv, 0, 0, win->info->width, win->info->height, GRAY);
	ChWindowUpdate(win, 0, 0, win->info->width, win->info->height, 1, 0);
}

/*
* NamdaphaMouseHandler -- handles mouse events
* @param win -- Pointer to Chitralekha Window
* @param x -- Mouse x coord
* @param y -- Mouse y coord
* @param button -- mouse button state
* @param scroll -- scroll information
*/
void XELauncherMouseHandler(ChWindow* win, int x, int y, int button, int scroll) {
	list_t* buttonList = mainGrid->lbbuttonlist;
	if (mainGrid->show_search)
		buttonList = mainGrid->searchResultList;
	for (int i = 0; i < buttonList->pointer; i++) {
		LaunchButton* widget = (LaunchButton*)list_get_at(buttonList, i);
		if (widget->page_number != mainGrid->activePageNumber)
			continue;
		if (x > win->info->x + widget->x && x < (win->info->x + widget->x + widget->w) &&
			(y > win->info->y + widget->y && y < (win->info->y + widget->y + widget->h))) {
			widget->hover = true;
			widget->kill_focus = false;
			if (widget->mouseEvent)
				widget->mouseEvent(widget, win, x, y, button);
		} else {
			if (widget->hover) {
				widget->hover = false;
				widget->kill_focus = true;
				if (widget->mouseEvent)
					widget->mouseEvent(widget, win, x, y, button);
			}
		}
	}

	for (int i = 0; i < win->widgets->pointer; i++) {
		ChWidget* wid = (ChWidget*)list_get_at(win->widgets, i);
		if (x > win->info->x + wid->x && x < (win->info->x + wid->x + wid->w) &&
			(y > win->info->y + wid->y && y < (win->info->y + wid->y + wid->h))) {
			wid->hover = 1;
			wid->KillFocus = false;
			if (wid->ChMouseEvent)
				wid->ChMouseEvent(wid, win, x, y, button);
		} else {
			if (wid->hover) {
				wid->hover = false;
				wid->KillFocus = true;
				if (wid->ChMouseEvent)
					wid->ChMouseEvent(wid, win, x, y, button);
			}
		}
	}
}

bool isControlKeyPressed(int key) {
	bool value = 0;
	switch (key) {
	case 0x3A: {
		//capslock on
		value = 1;
		bool bit = ChitralekhaKeyGetCapslock();
		bit = !bit;
		ChitralekhaKeySetCapslock(bit);
		break;
	}
	case 0xBA: {
		//capslock off
		value = 1;
		break;
	}
	}
	return value;
}
/*
* XenevaLauncherHandleMessage -- handle incoming messages
*
*/
void XenevaLauncherHandleMessage(PostEvent* e) {
	switch (e->type) {
		/*handle timer message code */
	case TIMER_MESSAGE_CODE: {
		memset(e, 0, sizeof(PostEvent));
		break;
	}
		/* handle mouse event from deodhai */
	case DEODHAI_REPLY_MOUSE_EVENT: {
		int handle = e->dword4;
		ChWindow* mouseWin = ChGetWindowByHandle(win, handle);
		if (mouseWin == win)
			XELauncherMouseHandler(mouseWin, e->dword, e->dword2, e->dword3, 0);
		else
			ChWindowHandleMouse(mouseWin, e->dword, e->dword2, e->dword3);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	case DEODHAI_REPLY_FOCUS_CHANGED: {
		/* Shown again: drop any leftover search filter so the app grid
		 * is not still hiding every icon from a previous unmapped key. --axiss */
		searchBar->textPos = 0;
		searchBar->prevTextPos = 0;
		searchBar->scrollOffset = 0;
		searchBar->text[0] = '\0';
		mainGrid->show_search = false;
		AppGridSearchReset(mainGrid);
		XELauncherPaint(win);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	case DEODHAI_REPLY_KEY_EVENT: {
		if (win->info->hide) {
			memset(e, 0, sizeof(PostEvent));
			break;
		}
		if (!isControlKeyPressed(e->dword)) {
			char c = ChitralekhaGetKeyPress(e->dword);
			/** dont accept release keys **/
			if (!(e->dword & 0x80)) {
				searchBar->prevTextPos = searchBar->textPos;
				if (c == 0x08) { /* Backspace */
					if (searchBar->textPos == 0) {
						mainGrid->show_search = false;
						AppGridSearchReset(mainGrid);
						if (mainGrid->PaintAppGrid) {
							mainGrid->PaintAppGrid(mainGrid, win);
							ChWindowUpdate(
								win, mainGrid->x, mainGrid->y, mainGrid->w, mainGrid->h, 0, 1);
						}
						memset(e, 0, sizeof(PostEvent));
						break;
					}
					searchBar->textPos--;
					searchBar->text[searchBar->textPos] = '\0';
				} else if (c >= 32 && c < 127) {
					if (ChitralekhaKeyGetCapslock())
						c = toupper(c);
					if (searchBar->textPos == 1024) {
						memset(e, 0, sizeof(PostEvent));
						break;
					}
					mainGrid->show_search = true;
					searchBar->text[searchBar->textPos++] = c;
					searchBar->text[searchBar->textPos] = '\0';
				} else {
					memset(e, 0, sizeof(PostEvent));
					break;
				}
				if (searchBar->wid.ChPaintHandler) {
					searchBar->wid.ChPaintHandler((ChWidget*)searchBar, win);
					ChWindowUpdate(win,
								   searchBar->wid.x,
								   searchBar->wid.y,
								   searchBar->wid.w,
								   searchBar->wid.h,
								   0,
								   1);
				}
				AppGridSearchReset(mainGrid);
				if (mainGrid->show_search)
					_match_string(searchBar->text, mainGrid->lbbuttonlist);
				if (mainGrid->PaintAppGrid) {
					mainGrid->PaintAppGrid(mainGrid, win);
					ChWindowUpdate(win, mainGrid->x, mainGrid->y, mainGrid->w, mainGrid->h, 0, 1);
				}
			}
		}
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	case DEODHAI_REPLY_MOUSE_LEAVE: {
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	}
}

static int _total_page_count;
/*
 * XELauncherGetAppGrid -- returns the main
 * application grid
 */
AppGrid* XELauncherGetAppGrid() {
	return mainGrid;
}

ChWindow* XELauncherGetMainWin() {
	return win;
}

void XEUpPageButtonAction(ChWidget* wid, ChWindow* win) {
	mainGrid->activePageNumber -= 1;
	if (mainGrid->activePageNumber < 1)
		mainGrid->activePageNumber = 1;

	AppGridPaint(mainGrid, win);
	ChWindowUpdate(win, mainGrid->x, mainGrid->y, mainGrid->w, mainGrid->h, 0, 1);
}

void XEDownPageButtonAction(ChWidget* wid, ChWindow* win) {
	if (mainGrid->activePageNumber >= 8)
		mainGrid->activePageNumber = 8;
	mainGrid->activePageNumber += 1;

	if (mainGrid->activePageNumber > _total_page_count)
		mainGrid->activePageNumber = _total_page_count;

	AppGridPaint(mainGrid, win);
	ChWindowUpdate(win, mainGrid->x, mainGrid->y, mainGrid->w, mainGrid->h, 0, 1);
}

/**
 *@brief xe_shutdown_action -- send shutdown request to init
 */
void xe_shutdown_action(ChWidget* wid, ChWindow* win) {
	if (xelaunch_get_pipe() != -1) {
		InitRequestMsg* msg = (InitRequestMsg*)malloc(sizeof(InitRequestMsg));
		strcpy(msg->message, INIT_REQUEST_PW_DOWN);
		_KeWriteFile(xelaunch_get_pipe(), msg, sizeof(InitRequestMsg));
	} else {
		_KePrint("****xelauncg piipe : %d \r\n", xelaunch_get_pipe());
	}
}

/**
 * @brief xe_restart_action -- send restart request to init
 */
void xe_restart_action(ChWidget* wid, ChWindow* win) {
	if (xelaunch_get_pipe() != -1) {
		InitRequestMsg* msg = (InitRequestMsg*)malloc(sizeof(InitRequestMsg));
		strcpy(msg->message, INIT_REQUEST_PW_REBOOT);
		_KeWriteFile(xelaunch_get_pipe(), msg, sizeof(InitRequestMsg));
	} else {
		_KePrint("****xelauncg piipe : %d \r\n", xelaunch_get_pipe());
	}
}

/*
* main -- xeneva launcher entry point
*/
int main(int argc, char* arv[]) {
	app = ChitralekhaStartApp(argc, arv);
	ChFontSetSize(app->baseFont, 12);

	/** open init pipe, for admin requests */
	_initpipe = _KeOpenFile("/pipe/init", FILE_OPEN_READ_ONLY | FILE_OPEN_WRITE);
	if (_initpipe == -1)
		_KePrint("[XELaunch]: failed to initialize the pipe \r\n");

	/* create a demo canvas just for getting the graphics
	* file descriptor
	*/
	XEFileIOControl graphctl;
	memset(&graphctl, 0, sizeof(XEFileIOControl));
	graphctl.syscall_magic = AURORA_SYSCALL_MAGIC;

	/* create a temporary canvas just to get
	* graphics file descriptor and screen resolution
	*/
	ChCanvas* canv = ChCreateCanvas(100, 100);
	screen_w = canv->screenWidth;
	screen_h = canv->screenHeight;
	bpp = canv->bpp;
	graphicsFd = canv->graphics_fd;
	_total_page_count = 1;
	free(canv);

	win = ChCreateWindow(app,
						 WINDOW_FLAG_STATIC | WINDOW_FLAG_ALWAYS_ON_TOP | WINDOW_FLAG_GLASS,
						 "Xeneva Launcher",
						 10,
						 10,
						 screen_w - 10 * 2,
						 screen_h - 85);
	launcher_w = screen_w - (10 * 2); //90;
	launcher_h = screen_h - 40;
	int grid_w = launcher_w / 2;
	int grid_h = launcher_h / 2;

	mainGrid = LauncherCreateAppGrid(
		launcher_w / 2 - (grid_w / 2), launcher_h / 2 - (grid_h / 2), grid_w + 50, grid_h + 50);

	mainGrid->activePageNumber = 1;
	/* now read the launcher config file
	 * for application entries 
	 */
	LauncherConfigInitialise();
	LauncherSetupByConfigFile();

	win->color = LAUNCHER_BACKGROUND_COLOR; //0xCCBBBBBB;
	win->ChWinPaint = XELauncherPaint;
	win->info->hide = true;
	win->info->alpha = false;

	searchBar = XECreateSearchBar(launcher_w / 2 - 280 / 2, 40, 280, 35);
	XEPageButton* up =
		CreatePageButton(launcher_w - 70, launcher_h / 2 - (55 / 2) - 60, 40, 55, PAGE_BUTTON_UP);
	up->wid.ChActionHandler = XEUpPageButtonAction;
	XEPageButton* down =
		CreatePageButton(launcher_w - 70, launcher_h / 2 - 55 / 2, 40, 55, PAGE_BUTTON_DOWN);
	ChWindowAddWidget(win, (ChWidget*)up);
	down->wid.ChActionHandler = XEDownPageButtonAction;
	ChWindowAddWidget(win, (ChWidget*)down);

	ChWindowAddWidget(win, (ChWidget*)searchBar);

	_total_page_count = AppGridGetTotalNumberOfPage(mainGrid);
	if (_total_page_count == 1) {
		up->disabled = true;
		down->disabled = true;
	}

	power_button* shutdown =
		create_power_button(POWER_BUTTON_TYPE_SHUTDOWN, launcher_w - 70 * 2 + 20, 40);
	shutdown->base.ChActionHandler = xe_shutdown_action;
	ChWindowAddWidget(win, (ChWidget*)shutdown);

	power_button* restart =
		create_power_button(POWER_BUTTON_TYPE_RESTART, launcher_w - 70 + 20, 40);
	restart->base.ChActionHandler = xe_restart_action;
	ChWindowAddWidget(win, (ChWidget*)restart);

	ChWindowPaint(win);

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));

	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		XenevaLauncherHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
	}
}

AppGrid* xelaunch_get_app_grid() {
	return mainGrid;
}

int xelaunch_get_pipe() {
	return _initpipe;
}
