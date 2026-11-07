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

#ifndef __PROGRESS_H__
#define __PROGRESS_H__

#include "../chitralekha.h"
#include "base.h"
#include "window.h"
#include "../widgets/list.h"

typedef struct _progressbar_ {
	ChWidget base;
	double maximumProgress;
	double minimumProgress;
	double currentProgress;
	double progressPercent;
	int progressPixelWidth;
} ChProgressBar;

/*
* ChCreateProgressBar -- creates a progress bar
* @param x -- x location
* @param y -- y location
* @param w -- width of the progress bar
* @param h -- height of the progress bar
*/
XE_EXTERN XE_LIB ChProgressBar* ChCreateProgressBar(int x, int y, int w, int h, double progress);

/*
* ChProgressBarSetMax -- set maximum limit of the progress
* @param pb -- Pointer to progress bar instance
* @param max -- Maximum limit of the progress in step
*/
XE_EXTERN XE_LIB void ChProgressBarSetMax(ChProgressBar* pb, int max);

/*
* ChProgressBarSetMin -- set minimum limit of the progress
* @param pb -- Pointer to progress bar instance
* @param min -- Minimum limit of the progress in step
*/
XE_EXTERN XE_LIB void ChProgressBarSetMin(ChProgressBar* pb, int min);

/*
* ChProgressBarGetMax -- get maximum limit of the progress bar
* @param pb -- Pointer to progress bar instance
*/
XE_EXTERN XE_LIB int ChProgressBarGetMax(ChProgressBar* pb);

/*
* ChProgressBarGetMin -- get minimum limit of the progress bar
* @param pb -- Pointer to progress bar
*/
XE_EXTERN XE_LIB int ChProgressBarGetMin(ChProgressBar* pb);

/*
* ChProgressBarSetValue -- set current progress value step
* @param pb -- Pointer to progress bar instance
* @param win -- Pointer to main window object
* @param value -- current step value
*/
XE_EXTERN XE_LIB void ChProgressBarSetValue(ChProgressBar* pb, ChWindow* win, double value);

#endif