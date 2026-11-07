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
#include <stdlib.h>
#include <string.h>

extern void ChDefaultProgressBarPainter(ChWidget* wid, ChWindow* win);

void ChProgressBarDestroy(ChWidget* widget, ChWindow* win) {
	ChProgressBar* pb = (ChProgressBar*)widget;
	free(pb);
	_KePrint("ChProgressBar destroy \r\n");
}
/*
 * ChCreateProgressBar -- creates a progress bar
 * @param x -- x location
 * @param y -- y location
 * @param w -- width of the progress bar
 * @param h -- height of the progress bar
 * @param progress -- current progress step
 */
ChProgressBar* ChCreateProgressBar(int x, int y, int w, int h, double progress) {
	ChProgressBar* pb = (ChProgressBar*)malloc(sizeof(ChProgressBar));
	memset(pb, 0, sizeof(ChProgressBar));
	pb->base.x = x;
	pb->base.y = 26 + y;
	pb->base.w = w;
	pb->base.h = h;
	pb->base.ChDestroy = ChProgressBarDestroy;
	pb->base.ChPaintHandler = ChDefaultProgressBarPainter;
	pb->minimumProgress = 0;
	pb->maximumProgress = 100;
	pb->currentProgress = progress;
	double progressValue = (pb->currentProgress / pb->maximumProgress) * pb->base.w;
	pb->progressPixelWidth = (int)progressValue;
	pb->progressPercent = (pb->currentProgress / pb->maximumProgress) * 100;
	return pb;
}

/*
 * ChProgressBarSetMax -- set maximum limit of the progress
 * @param pb -- Pointer to progress bar instance
 * @param max -- Maximum limit of the progress in step
 */
void ChProgressBarSetMax(ChProgressBar* pb, int max) {
	pb->maximumProgress = max;
}

/*
 * ChProgressBarSetMin -- set minimum limit of the progress
 * @param pb -- Pointer to progress bar instance
 * @param min -- Minimum limit of the progress in step
 */
void ChProgressBarSetMin(ChProgressBar* pb, int min) {
	pb->minimumProgress = min;
}

/*
 * ChProgressBarGetMax -- get maximum limit of the progress bar
 * @param pb -- Pointer to progress bar instance
 */
int ChProgressBarGetMax(ChProgressBar* pb) {
	return pb->maximumProgress;
}

/*
 * ChProgressBarGetMin -- get minimum limit of the progress bar
 * @param pb -- Pointer to progress bar
 */
int ChProgressBarGetMin(ChProgressBar* pb) {
	return pb->minimumProgress;
}

/*
 * ChProgressBarSetValue -- set current progress value step
 * @param pb -- Pointer to progress bar instance
 * @param win -- Pointer to main window object
 * @param value -- current step value
 */
void ChProgressBarSetValue(ChProgressBar* pb, ChWindow* win, double value) {
	pb->currentProgress = value;
	double progressValue = (pb->currentProgress / pb->maximumProgress) * pb->base.w;
	pb->progressPixelWidth = (int)progressValue;

	if (pb->currentProgress <= pb->minimumProgress)
		pb->currentProgress = pb->minimumProgress;

	if (pb->currentProgress >= pb->maximumProgress)
		pb->currentProgress = pb->maximumProgress;

	if (pb->currentProgress != pb->maximumProgress) {
		if (pb->base.ChPaintHandler)
			pb->base.ChPaintHandler((ChWidget*)pb, win);

		pb->progressPercent = (pb->currentProgress / pb->maximumProgress) * 100;
		ChWindowUpdate(win, pb->base.x, pb->base.y, pb->base.w, pb->base.h, 0, 1);
	}
}