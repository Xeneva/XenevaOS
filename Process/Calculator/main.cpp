/**
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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
#include <widgets/window.h>
#include <widgets/menu.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <keycode.h>
#include <widgets/menubar.h>
#include <widgets/msgbox.h>
#include <string.h>
#include <stdlib.h>
#include "calculator.h"
#include <sys/mman.h>
#include <pthread.h>

ChitralekhaApp* app;
ChWindow* mainWin;
ChFont* dispFont;
CalculatorDisplay* mainDisp;

#define CALCULATOR_BACK_COLOR 0xFF353535
#define CALC_DISPLAY_DARK	  0xFF6598DE
#define CALC_DISPLAY_LIGHT	  0xFF8BADDC

#define MAX_DIGIT 1024

void CalcDisplayDraw(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* disp = (CalculatorDisplay*)wid;
	ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, WHITE);
	ChColorDrawHorizontalGradient(
		win->canv, wid->x, wid->y, wid->w, wid->h, CALC_DISPLAY_DARK, CALC_DISPLAY_LIGHT);
	ChDrawRectUnfilled(win->canv, wid->x, wid->y, wid->w, wid->h, BLACK);
	ChDrawRectUnfilled(win->canv, wid->x + 1, wid->y + 1, wid->w - 1, wid->h - 1, BLACK);
	ChFontSetSize(dispFont, 23);
	char* buff = disp->inputnum;
	bool _clear_output = false;
	if (disp->output) {
		buff = disp->outputnum;
		disp->output = false;
		_clear_output = true;
	}
	if (!buff[0])
		buff = "0";
	int font_w = ChFontGetWidth(dispFont, buff);
	ChFontDrawText(
		win->canv, dispFont, buff, wid->x + wid->w - font_w - 10, wid->y + wid->h / 2, 23, BLACK);

	if (disp->operator_ != 0) {
		ChFontSetSize(dispFont, 13);
		char* string = 0;
		switch (disp->operator_) {
		case CALC_OPERATOR_ADD:
			string = "(+)";
			break;
		case CALC_OPERATOR_MULT:
			string = "(*)";
			break;
		case CALC_OPERATOR_DIVIDE:
			string = "(/)";
			break;
		case CALC_OPERATOR_MOD:
			string = "(%)";
			break;
		case CALC_OPERATOR_SUB:
			string = "(-)";
			break;
		default:
			string = " ";
			break;
		}
		int op_w = ChFontGetWidth(dispFont, string);
		int op_h = ChFontGetHeight(dispFont, string);
		ChFontDrawText(win->canv,
					   dispFont,
					   string,
					   wid->x + wid->w - op_w - 10,
					   wid->y + wid->h - op_h,
					   13,
					   BLACK);
	}
	if (_clear_output)
		memset(disp->outputnum, 0, 1024);

	if (disp->historyIdx > 0) {
		ChFontSetSize(dispFont, 11);
		int hist_w = ChFontGetWidth(dispFont, disp->historyBuf);
		int hist_h = ChFontGetHeight(dispFont, disp->historyBuf);
		ChFontDrawText(win->canv,
					   dispFont,
					   disp->historyBuf,
					   wid->x + wid->w - hist_w - 10,
					   wid->y + 10,
					   13,
					   LIGHTBLACK);
	}
}

/*
 * CalcUpdateDisplay -- update the newly modified display
 * @param disp -- Pointer to calculator display
 */
void CalcUpdateDisplay(CalculatorDisplay* disp) {
	CalcDisplayDraw((ChWidget*)disp, mainWin);
	ChWindowUpdate(mainWin, disp->wid.x, disp->wid.y, disp->wid.w, disp->wid.h, 0, 1);
}

/*
 * CalcClearOutput -- clear all output
 * @param disp -- Pointer to calculator display
 */
void CalcClearOutput(CalculatorDisplay* disp) {
	memset(disp->outputnum, 0, 1024);
}

/*
 * CalculatorGetMainDisplay -- returns the main display
 */
CalculatorDisplay* CalculatorGetMainDisplay() {
	return mainDisp;
}
/*
 * CalcCreateDisplay -- creates a calculator display
 * @param x -- X position of the display
 * @param y -- Y position of the display
 * @param w -- Width of the display
 * @param h -- Height of the display
 */
CalculatorDisplay* CalcCreateDisplay(int x, int y, int w, int h) {
	CalculatorDisplay* disp = (CalculatorDisplay*)malloc(sizeof(CalculatorDisplay));
	memset(disp, 0, sizeof(CalculatorDisplay));
	disp->wid.x = CHITRALEKHA_WINDOW_DEFAULT_PAD_X + x;
	disp->wid.y = CHITRALEKHA_WINDOW_DEFAULT_PAD_Y + y;
	disp->wid.w = w;
	disp->wid.h = h;
	disp->wid.ChMouseEvent = 0;
	disp->wid.ChPaintHandler = CalcDisplayDraw;
	disp->inputnum = (char*)malloc(MAX_DIGIT);
	memset(disp->inputnum, 0, 1024);
	disp->outputnum = (char*)malloc(MAX_DIGIT);
	disp->operator_ = 0;
	disp->inputidx = 0;
	disp->historyBuf = (char*)malloc(MAX_DIGIT);
	disp->historyIdx = 0;
	return disp;
}

/*
 * CalculatorProcess -- process calculator calculations
 * @param calc -- Pointer to calculator display
 */
void CalculatorProcess(CalculatorDisplay* calc) {
	if (calc->operator_ == 0) {
		return;
	}
	int num2 = atoi(calc->inputnum);
	calc->num2 = num2;
	int result = 0;
	if (calc->operator_ != 0) {
		int num1 = calc->num1;
		switch (calc->operator_) {
		case CALC_OPERATOR_ADD:
			result = num1 + num2;
			break;
		case CALC_OPERATOR_DIVIDE: {
			if (num2 == 0) {
				CalcAllClear(calc);
				CalcClearHistory(calc);
				CalcClearOutput(calc);
				calc->output = 0;
				strcpy(calc->inputnum, "Can't divide by 0");
				calc->inputidx += strlen(calc->inputnum) - 1;
				CalcUpdateDisplay(calc);
				/* CalcAllClear(calc);
										   CalcClearHistory(calc);
										   CalcClearOutput(calc);*/
				calc->output = 0;
				return;
			}
			result = num1 / num2;
			break;
		}
		case CALC_OPERATOR_MOD:
			result = num1 % num2;
			break;
		case CALC_OPERATOR_MULT:
			result = num1 * num2;
			break;
		case CALC_OPERATOR_SUB:
			result = num1 - num2;
			break;
		}
	}
	memset(calc->outputnum, 0, 1024);
	itoa_s(result, 10, calc->outputnum);
	CalcAddToHistory(calc, calc->inputnum, 0);
	CalcAddToHistory(calc, NULL, calc->operator_);
	calc->output = true;
	calc->num1 = result;
	CalcClearHistory(calc);
	CalcAddToHistory(calc, calc->inputnum, 0);
	/*calc->operator_ = 0;*/

	/* keep the result in inputnum too, not just outputnum -- the display
	 * only shows outputnum for a single paint before falling back to
	 * inputnum, so wiping inputnum here (the old CalcAllClear) left the
	 * value in no buffer at all: Back/AC had nothing left to clear --axiss */
	memset(calc->inputnum, 0, 1024);
	strcpy(calc->inputnum, calc->outputnum);
	calc->inputidx = strlen(calc->inputnum);

	/* mark this as a just-computed result: the next digit typed should
	 * replace it (a new number), not append to it, and num1 must not be
	 * left stale for the operator handlers' "num1 == 0 means unset"
	 * check -- both handled in CalcAddDigit --axiss */
	calc->freshResult = true;
}

/* CalcAddDigit -- adds a digit to the calculator
 * processor, only one digit 
 * @param disp -- Calculator display processor
 * @param number -- number with one digit
 */
void CalcAddDigit(CalculatorDisplay* disp, int number) {
	if (number > 9)
		return;
	if (disp->freshResult) {
		/* starting fresh entry after a shown result: always replace the
		 * displayed value instead of appending. Only drop num1 too if no
		 * operator has been chosen yet -- if the user pressed an operator
		 * first (chaining off the previous result, e.g. "5+3=" then "+2="),
		 * num1 must survive so the upcoming '=' still computes from it --axiss */
		memset(disp->inputnum, 0, 1024);
		disp->inputidx = 0;
		if (disp->operator_ == 0)
			disp->num1 = 0;
		disp->freshResult = false;
	}
	char num[16];
	itoa_s(number, 10, num);
	if (disp->inputidx >= 1023)
		return;
	disp->inputnum[disp->inputidx] = num[0];
	disp->inputidx++;
}

/*
 * CalcRemoveDigit -- removes a digit from input buffer
 * @param disp -- Calculator display
 */
void CalcRemoveDigit(CalculatorDisplay* disp) {
	/* editing the shown value directly -- a digit typed after this should
	 * append, not replace it --axiss */
	disp->freshResult = false;
	if (disp->inputidx == 0)
		return;

	int idx = disp->inputidx - 1;
	if (idx <= 0)
		idx = 0;
	disp->inputnum[idx] = '\0';
	disp->inputidx--;
	if (disp->inputidx <= 0)
		disp->inputidx = 0;
}

/*
 * CalcAddToHistory -- add recent calculations to history
 * @param disp -- Pointer to calculator display
 * @param num -- numbers to add
 * @param operator_ -- operator to add
 */
void CalcAddToHistory(CalculatorDisplay* disp, char* num, uint8_t operator_) {
	if (disp->historyIdx == 1024)
		return;
	if (operator_ != 0) {
		char opstr[1];
		switch (operator_) {
		case CALC_OPERATOR_ADD:
			opstr[0] = '+';
			break;
		case CALC_OPERATOR_DIVIDE:
			opstr[0] = '/';
			break;
		case CALC_OPERATOR_MOD:
			opstr[0] = '%';
			break;
		case CALC_OPERATOR_MULT:
			opstr[0] = '*';
			break;
		case CALC_OPERATOR_SUB:
			opstr[0] = '-';
			break;
		default:
			opstr[0] = '\0';
			break;
		}
		disp->historyBuf[disp->historyIdx] = opstr[0];
		disp->historyIdx++;
	} else {
		/* was "strlen(num) - 1": a 1-digit num left historyIdx
		 * unchanged, and an empty num underflowed this uint16_t to
		 * 65535 --axiss */
		int count = strlen(num);
		strcpy(disp->historyBuf + disp->historyIdx, num);
		disp->historyIdx += count;
	}
}

/*
 * CalcClearHistory -- clears recent digits history
 * @param disp -- Pointer to calculator display
 */
void CalcClearHistory(CalculatorDisplay* disp) {
	memset(disp->historyBuf, 0, 1024);
	disp->historyIdx = 0;
}
/*
 * CalcAllClear -- clear all digits
 * @param disp -- Pointer to calculator display
 * processor
 */
void CalcAllClear(CalculatorDisplay* disp) {
	memset(disp->inputnum, 0, 1024);
	disp->inputidx = 0;
	disp->freshResult = false;
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
		int c = ChitralekhaKeyToASCII(code);
		HandleKeyEvents(c);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	case DEODHAI_REPLY_TOUCH_EVENT: {
		int handle = e->dword4;
		if (e->dword5 == WINDOW_HANDLE_TYPE_NORMAL) {
			ChWindow* mouseWin = ChGetWindowByHandle(mainWin, handle);
			ChWindowHandleTouch(mouseWin, e->dword, e->dword2, e->dword3);
		} else if (e->dword5 == WINDOW_HANDLE_TYPE_POPUP) {
			ChWindow* pw = ChGetPopupWindowByHandle(mainWin, handle);
			//ChPopupWindowHandleTouch(pw, e->dword, e->dword2, e->dword3);
		}
		memset(e, 0, sizeof(PostEvent));
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

	case DEODHAI_REPLY_DESTROY_BUFFER: {
		ChWindowHandleDestroyBuffer(mainWin);
		memset(e, 0, sizeof(PostEvent));
		break;
	}

	case DEODHAI_REPLY_REINIT_BUFFER: {
		int backbufkey = e->dword;
		_KePrint("Calculator backbufkey : %x \r\n", backbufkey);
		int id = _KeCreateSharedMem(backbufkey, 0, 0);

		void* buffer = _KeObtainSharedMem(id, 0, 0);
		_KePrint("calculator reinit shm : %x \r\n", buffer);
		mainWin->app->backbufkey = backbufkey;
		mainWin->app->fb = buffer;
		mainWin->buffer = (uint32_t*)buffer;
		mainWin->info->windowReady = 1;

		_KeProcessSleep(100);
		ChDeAllocateBuffer(mainWin->canv);
		mainWin->canv->canvasWidth = mainWin->info->width;
		mainWin->canv->canvasHeight = mainWin->info->height;
		_KePrint("calc w : %d, h: %d \r\n", mainWin->info->width, mainWin->info->height);
		ChAllocateBuffer(mainWin->canv);
		ChWindowPaint(mainWin);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	default:
		memset(e, 0, sizeof(PostEvent));
		break;
	}
}

void CalculatorCreateButtonGird(ChWindow* win) {
	int y_start = mainDisp->wid.y + mainDisp->wid.h + 10;
	int x_start = 10;
	int grid_width = win->info->width - 10 * 2 - CHITRALEKHA_WINDOW_DEFAULT_PAD_X;
	int pad_x = 10;
	int pad_y = 10;
	int num_buttons_per_row = grid_width / (70 + pad_x);
	int butt_w = 80;

	ChFontSetSize(app->baseFont, 24);
	/* first row*/
	ChButton* ac = ChCreateButton(x_start, y_start, butt_w, 35, "AC"); //All Clear
	ac->base.ChActionHandler = AllClearAction;
	ChWindowAddWidget(win, (ChWidget*)ac);
	ChButton* back = ChCreateButton(ac->base.x + ac->base.w + pad_x, y_start, butt_w, 35, "Back");
	back->base.ChActionHandler = BackAction;
	ChWindowAddWidget(win, (ChWidget*)back);

	ChButton* mod = ChCreateButton(back->base.x + back->base.w + pad_x, y_start, butt_w, 35, "%");
	mod->base.ChActionHandler = ModAction;
	ChWindowAddWidget(win, (ChWidget*)mod);
	ChButton* div = ChCreateButton(mod->base.x + mod->base.w + pad_x, y_start, butt_w, 35, "/");
	div->base.ChActionHandler = DivideAction;
	ChWindowAddWidget(win, (ChWidget*)div);

	/*second row */
	y_start += mod->base.h + pad_y;
	ChButton* seven = ChCreateButton(x_start, y_start, butt_w, 35, "7");
	seven->base.ChActionHandler = SevenAction;
	ChWindowAddWidget(win, (ChWidget*)seven);

	ChButton* eight =
		ChCreateButton(seven->base.x + seven->base.w + pad_x, y_start, butt_w, 35, "8");
	eight->base.ChActionHandler = EightAction;
	ChWindowAddWidget(win, (ChWidget*)eight);

	ChButton* nine =
		ChCreateButton(eight->base.x + eight->base.w + pad_x, y_start, butt_w, 35, "9");
	nine->base.ChActionHandler = NineAction;
	ChWindowAddWidget(win, (ChWidget*)nine);

	ChButton* mult = ChCreateButton(nine->base.x + nine->base.w + pad_x, y_start, butt_w, 35, "*");
	mult->base.ChActionHandler = MultAction;
	ChWindowAddWidget(win, (ChWidget*)mult);

	/* third row */
	y_start += mult->base.h + pad_y;
	ChButton* four = ChCreateButton(x_start, y_start, butt_w, 35, "4");
	four->base.ChActionHandler = FourAction;
	ChWindowAddWidget(win, (ChWidget*)four);

	ChButton* five = ChCreateButton(four->base.x + four->base.w + pad_x, y_start, butt_w, 35, "5");
	five->base.ChActionHandler = FiveAction;
	ChWindowAddWidget(win, (ChWidget*)five);

	ChButton* six = ChCreateButton(five->base.x + five->base.w + pad_x, y_start, butt_w, 35, "6");
	six->base.ChActionHandler = SixAction;
	ChWindowAddWidget(win, (ChWidget*)six);

	ChButton* minus = ChCreateButton(six->base.x + six->base.w + pad_x, y_start, butt_w, 35, "-");
	minus->base.ChActionHandler = SubAction;
	ChWindowAddWidget(win, (ChWidget*)minus);

	/* fourth row */
	y_start += minus->base.h + pad_y;
	ChButton* one = ChCreateButton(x_start, y_start, butt_w, 35, "1");
	one->base.ChActionHandler = OneAction;
	ChWindowAddWidget(win, (ChWidget*)one);

	ChButton* two = ChCreateButton(one->base.x + one->base.w + pad_x, y_start, butt_w, 35, "2");
	two->base.ChActionHandler = TwoAction;
	ChWindowAddWidget(win, (ChWidget*)two);

	ChButton* three = ChCreateButton(two->base.x + two->base.w + pad_x, y_start, butt_w, 35, "3");
	three->base.ChActionHandler = ThreeAction;
	ChWindowAddWidget(win, (ChWidget*)three);

	ChButton* add = ChCreateButton(three->base.x + three->base.w + pad_x, y_start, butt_w, 35, "+");
	add->base.ChActionHandler = AddAction;
	ChWindowAddWidget(win, (ChWidget*)add);

	/* fifth row */
	y_start += add->base.h + pad_y;
	ChButton* plusminus = ChCreateButton(x_start, y_start, butt_w, 35, "+/-");
	ChWindowAddWidget(win, (ChWidget*)plusminus);
	ChButton* zero =
		ChCreateButton(plusminus->base.x + plusminus->base.w + pad_x, y_start, butt_w, 35, "0");
	zero->base.ChActionHandler = ZeroAction;
	ChWindowAddWidget(win, (ChWidget*)zero);
	ChButton* dot = ChCreateButton(zero->base.x + zero->base.w + pad_x, y_start, butt_w, 35, ".");
	ChWindowAddWidget(win, (ChWidget*)dot);
	ChButton* equal = ChCreateButton(dot->base.x + dot->base.w + pad_x, y_start, butt_w, 35, "=");
	equal->base.ChActionHandler = EqualAction;
	ChWindowAddWidget(win, (ChWidget*)equal);
}

void CalculatorClose(ChWindow* win, ChWinGlobalControl* ctl) {
	/*ChFontClose(win->app->baseFont);
	ChFontClose(dispFont);*/
	ChWindowCloseWindow(win);
}

/*
 * CalculatorAboutBox -- show the about box
 * @param widget -- System parameter
 * @param win -- System parameter
 */
void CalculatorAboutBox(ChWidget* widget, ChWindow* win) {
	ChMessageBox* mbox = ChCreateMessageBox(mainWin,
											"About Calculator v1.0",
											"This program is part of Xeneva OS",
											MSGBOX_TYPE_ONLYCLOSE,
											MSGBOX_ICON_WARNING);
	ChMessageBoxShow(mbox);
}

void PopupWindowMouseEventTest(ChWidget* widget, ChWindow* win, int x, int y, int button) {
	ChPopupWindow* pw = (ChPopupWindow*)widget;
	_KePrint("Popup window mouse event \r\n");
	pw->shwin->dirty = true;
}

/*
* main -- main entry
*/
int main(int argc, char* argv[]) {
	/*if (strcmp(argv[0], "-about") == 0)
		printf("Calculator v1.0 for Xeneva OS \n");*/

	app = ChitralekhaStartApp(argc, argv);
	mainWin = ChCreateWindow(app,
							 WINDOW_FLAG_MOVABLE | WINDOW_FLAG_STYLE_KNOT,
							 "Calculator",
							 400,
							 480 / 2 - 400 / 2,
							 380,
							 400);
	mainWin->color = CALCULATOR_BACK_COLOR;
	for (int i = 0; i < mainWin->GlobalControls->pointer; i++) {
		ChWinGlobalControl* ctl = (ChWinGlobalControl*)list_get_at(mainWin->GlobalControls, i);
		if (ctl->type == WINDOW_GLOBAL_CONTROL_CLOSE) {
			/* change the close action event */
			ctl->ChGlobalActionEvent = CalculatorClose;
			break;
		}
	}

	ChWindowBroadcastIcon(app, "/icons/calc.bmp");

	dispFont = ChInitialiseFont(FORTE);

	mainDisp = CalcCreateDisplay(
		10, 40, mainWin->info->width - 10 * 2 - CHITRALEKHA_WINDOW_DEFAULT_PAD_X, 75);

	CalculatorCreateButtonGird(mainWin);
	ChWindowAddWidget(mainWin, (ChWidget*)mainDisp);
	/* button grid */
	ChWindowPaint(mainWin);

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));

	/* setup the jump buffer */
	setjmp(mainWin->jump);

	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		WindowHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
	}
}
