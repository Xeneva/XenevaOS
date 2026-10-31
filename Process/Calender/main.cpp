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
#include <sys/_ketime.h>
#include <time.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/button.h>
#include <keycode.h>
#include <widgets/window.h>
#include <string.h>
#include <stdlib.h>

ChitralekhaApp* app;
ChWindow* mainWin;
uint8_t* calenderBuffer;
int box_width;
int box_height;
int daycode;
ChFont* monthText;
XETime t;

int current_year;
int current_month;
int current_day;
int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

typedef struct _datebox_ {
	int x;
	int y;
	int w;
	int h;
	uint16_t dateValue;
	bool today;
} DateBox;

void CalenderRepaint(int year, int month, int num_days);

void CalenderHandleKeys(int c) {
	switch (c) {
	case KEY_W:
		current_month--;
		current_day = 1;

		if (current_month < 0) {
			current_month = 12;
			current_year--;
		}
		if (current_month == t.month && current_year == t.year)
			current_day = t.day;

		CalenderRepaint(current_year, current_month, days_in_month[current_month]);
		break;
	case KEY_S:
		current_month++;
		current_day = 1;
		if (current_month > 12) {
			current_month = 1;
			current_year++;
		}
		if (current_month == t.month && current_year == t.year)
			current_day = t.day;
		CalenderRepaint(current_year, current_month, days_in_month[current_month]);
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
		ChWindowHandleMouse(mainWin, e->dword, e->dword2, e->dword3);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
		/* handle key events from deodhai */
	case DEODHAI_REPLY_KEY_EVENT: {
		int code = e->dword;
		ChitralekhaProcessKey(code);
		char c = ChitralekhaKeyToASCII(code);
		CalenderHandleKeys(c);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	}
}

/*
 * GetDayText -- converts a day number to
 * string
 * @param day -- day number
 */
char* GetDayText(int day) {
	switch (day) {
	case 0:
		return "Sun";
	case 1:
		return "Mon";
	case 2:
		return "Tue";
	case 3:
		return "Wed";
	case 4:
		return "Thu";
	case 5:
		return "Fri";
	case 6:
		return "Sat";
	default:
		return 0;
	}
}

/*
 * GetMonthText -- converts the given month
 * number to string
 * @param month -- month number
 */
char* GetMonthText(int month) {
	switch (month) {
	case 0:
		return 0;
	case 1:
		return "January";
	case 2:
		return "February";
	case 3:
		return "March";
	case 4:
		return "April";
	case 5:
		return "May";
	case 6:
		return "June";
	case 7:
		return "July";
	case 8:
		return "August";
	case 9:
		return "September";
	case 10:
		return "October";
	case 11:
		return "November";
	case 12:
		return "December";
	}
}

/*
 * isLeapYear -- checks if the given year is
 * leap year
 * @param year -- year to consider
 */
bool isLeapYear(int year) {
	return (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
}

/*
 * GetDayOfWeek -- get the day of the week in (0-6) form
 * @param day -- current day to take i.e 1
 * @param month -- month to consider
 * @param year -- year to consider
 */
uint8_t GetDayOfWeek(uint8_t day, uint8_t month, uint16_t year) {
	//!Zeller's Congruence algorithm
	uint16_t t = (14 - month) / 12;
	uint16_t y = year - t;
	uint16_t m = month + 12 * t - 2;
	uint8_t d = (day + y + y / 4 - y / 100 + y / 400 + (31 * m) / 12) % 7;
	return d;
}

/*
 * CalenderRepaint -- repaints the calender with given
 * parameters
 * @param year -- year to consider
 * @param month -- month to consider
 * @param num_days -- number of days of given month
 */
void CalenderRepaint(int year, int month, int num_days) {
	ChFontSetSize(app->baseFont, 13);
	if (isLeapYear(year))
		days_in_month[2] = 29;
	else
		days_in_month[2] = 28;

	for (int j = 0; j < 42; j++) {
		DateBox* db = (DateBox*)&calenderBuffer[j * sizeof(DateBox)];
		db->dateValue = 0;
		db->today = 0;
	}

	int offset = GetDayOfWeek(1, month, year);

	for (int j = 0; j < num_days; j++) {
		DateBox* db = (DateBox*)&calenderBuffer[offset * sizeof(DateBox)];
		db->dateValue = j + 1;
		if (t.year == year && t.month == month) {
			if (t.day == db->dateValue)
				db->today = 1;
		}
		offset++;
	}
	int start_y = 0;
	DateBox* db = (DateBox*)&calenderBuffer[0 * sizeof(DateBox)];
	start_y = db->y;
	int max_h = mainWin->info->height - start_y;
	ChDrawRect(mainWin->canv, 5, start_y, mainWin->info->width - 10, max_h - 50, mainWin->color);
	for (int i = 0; i < 42; i++) {
		DateBox* db = (DateBox*)&calenderBuffer[i * sizeof(DateBox)];
		if (db->dateValue == 0)
			continue;
		char str[2];
		itoa(db->dateValue, str);
		int text_len = ChFontGetWidth(app->baseFont, str);
		uint32_t textcol = BLACK;
		/* selection*/
		if (db->today)
			ChDrawRect(mainWin->canv, db->x, db->y, box_width, box_height, 0xFFAE5C22);
		if (db->today)
			textcol = WHITE;
		ChFontDrawText(mainWin->canv,
					   app->baseFont,
					   str,
					   db->x + (db->w / 2) - text_len / 2,
					   db->y + db->h / 2,
					   10,
					   textcol);
	}

	char monthName[20];
	sprintf(monthName, "%s %d", GetMonthText(month), year);

	ChFontSetSize(monthText, 18);
	int monthLen = ChFontGetWidth(monthText, monthName);
	ChDrawRect(mainWin->canv,
			   ((mainWin->info->width / 2) - monthLen / 2) - 20,
			   26,
			   monthLen + 40,
			   box_height,
			   mainWin->color);
	ChFontDrawText(mainWin->canv,
				   monthText,
				   monthName,
				   (mainWin->info->width / 2) - monthLen / 2,
				   26 + 30,
				   18,
				   BLACK);

	ChWindowUpdate(mainWin, 0, 0, mainWin->info->width, mainWin->info->height, 1, 0);
}

/**
 * AI Generated code
 */
static void civil_from_days(int64_t z, int* y, unsigned* m, unsigned* d) {
	z += 719468;
	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t y0 = (int64_t)yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp + (mp < 10 ? 3 : -9);
	*y = (int)(y0 + (*m <= 2));
}

void CalenderGetYMD(uint32_t* year, uint32_t* month, uint32_t* day, int32_t tz_offset) {
	int64_t sec_, nsec_;
	_KeGetWalltime(&sec_, &nsec_);
	sec_ += tz_offset;

	int64_t days = (int64_t)sec_ / 86400;
	uint32_t secs_of_day = sec_ % 86400;

	int y;
	unsigned mo, d;
	civil_from_days(days, &y, &mo, &d);
	*year = y;
	*month = mo;
	*day = d;
}

/*
* main -- main entry
*/
int main(int argc, char* argv[]) {
	_KePrint("Cal \r\n");
	app = ChitralekhaStartApp(argc, argv);
	mainWin = ChCreateWindow(
		app, WINDOW_FLAG_MOVABLE, "Calender", 100, 50, CHITRALEKHA_DEFAULT_WIN_WIDTH, 400);

	_KePrint("Calender \r\n");
	/* Create the main calender buffer */
	calenderBuffer = (uint8_t*)malloc(sizeof(DateBox) * 42);
	memset(calenderBuffer, 0, sizeof(DateBox) * 42);

	//_KeGetCurrentTime(&t);

	current_month = t.month;
	current_year = t.year;
	current_day = t.day;
	CalenderGetYMD((uint32_t*)&current_year,
				   (uint32_t*)&current_month,
				   (uint32_t*)&current_day,
				   TZ_SEC_IST_INDIA);

	monthText = ChInitialiseFont(FORTE);

	box_width = (mainWin->info->width - 2 * 5) / 7;
	box_height = (mainWin->info->height - (26 + 30)) / 8;
	box_height -= 5;

	ChWindowPaint(mainWin);
	ChFontSetSize(app->baseFont, 14);

	/*
	 * Prepare the calender grid for dates to be shown
	 */
	int x = 5;
	int y = 26 + 40;

	for (int i = 0; i < 7; i++) {
		char* day = GetDayText(i);
		int text_len = ChFontGetWidth(app->baseFont, day);
		ChFontDrawText(mainWin->canv,
					   app->baseFont,
					   day,
					   x + (box_width / 2) - text_len / 2,
					   y + box_height / 2,
					   10,
					   LIGHTBLACK);
		x += box_width;
	}
	y += box_height;
	x = 5;
	int cal_off = 0;
	for (int j = 0; j < 42; j++) {
		DateBox* db = (DateBox*)&calenderBuffer[j * sizeof(DateBox)];
		db->x = x;
		db->y = y;
		db->w = box_width;
		db->h = box_height;
		x += box_width;
		cal_off++;
		if (cal_off == 7) {
			x = 5;
			y += box_height;
			cal_off = 0;
		}
	}

	/*
	 * Print the help message in bottom of the window
	 */
	ChFontSetSize(app->baseFont, 12);
	int msglen = ChFontGetWidth(app->baseFont, "Press 'W' for Up and 'S' for Down");
	ChFontDrawText(mainWin->canv,
				   app->baseFont,
				   "Press 'W' for Up and 'S' for Down",
				   (mainWin->info->width / 2) - msglen / 2,
				   mainWin->info->height - 10,
				   10,
				   LIGHTBLACK);

	/*
	 * Repaint the calender
	 */
	CalenderRepaint(t.year, t.month, days_in_month[t.month]);

	ChWindowBroadcastIcon(app, "/icons/calndr.bmp");

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));

	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		WindowHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
	}
}