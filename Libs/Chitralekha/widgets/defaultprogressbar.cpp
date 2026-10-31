/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2024, Manas Kamal Choudhury
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

#include "progress.h"

/*
 * ChDefaultProgressBarPainter -- default xeneva progress bar painter
 * module
 * @param wid -- Pointer to this widget
 * @param win -- Pointer to main application window
 */
void ChDefaultProgressBarPainter(ChWidget* wid, ChWindow* win) {
	ChProgressBar* pb = (ChProgressBar*)wid;

	ChDrawRect(win->canv, pb->base.x, pb->base.y, pb->base.w, pb->base.h, 0xFFB9B9B9);

	//draw progress width
	ChColorDrawVerticalGradient(win->canv,
								pb->base.x,
								pb->base.y,
								pb->progressPixelWidth,
								pb->base.h,
								0xFF2DBD11,
								0xFF217A10);

	ChFontSetSize(win->app->baseFont, 13);
	char num[100];
	sprintf(num, "%f", pb->progressPercent);
	int symbolIndex = strlen(num);
	num[symbolIndex - 1] = '%';
	int text_len = ChFontGetWidth(win->app->baseFont, num);
	ChFontDrawText(win->canv,
				   win->app->baseFont,
				   num,
				   pb->base.x + pb->base.w / 2 - text_len / 2,
				   pb->base.y + pb->base.h / 2,
				   13,
				   BLACK);

	ChDrawRectUnfilled(win->canv, pb->base.x, pb->base.y, pb->base.w, pb->base.h, 0xFF827474);
}