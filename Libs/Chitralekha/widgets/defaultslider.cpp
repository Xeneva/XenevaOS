/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#include "slider.h"
#include "view.h"
#include <math.h>
#include "../draw.h"
#include "../color.h"
#include "icon.h"

/*
* ChDefaultSliderPainter -- default slider painter
*/
void ChDefaultSliderPainter(ChWidget* wid, ChWindow* win) {
	ChSlider* slider = (ChSlider*)wid;

	if (slider->type == CHITRALEKHA_SLIDER_VERTICAL) {
		ChDrawRect(win->canv,
				   slider->bound.x,
				   slider->bound.y,
				   slider->bound.w,
				   slider->bound.h,
				   ChWindowGetBackgroundColor(win));
		//ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, 0xFFDDDBDB);

		/* draw the progress */
		if (slider->currentVal != 0) {
			if (slider->progressPixel != 0) {
				uint32_t col1 = 0xFF84C8EC;
				uint32_t col2 = 0xFF3A96AC;
				if (slider->useCustomColor) {
					col1 = slider->customColor1;
					col2 = slider->customColor2;
				}
				ChColorDrawHorizontalGradient(win->canv,
											  wid->x,
											  wid->y + slider->progressPixel,
											  9,
											  (wid->y + wid->h) - (wid->y + slider->progressPixel),
											  col1,
											  col2);
			}
		}

		uint32_t outlineColor = 0xFF545454;
		if (slider->useCustomColor)
			outlineColor = slider->outlineColor;
		ChDrawRectUnfilled(win->canv, wid->x, wid->y, wid->w, wid->h, outlineColor);

		/* ! Slider thumb */
		if (slider->thumbVisible) {
			ChDrawRect(win->canv, wid->x - 5, wid->y + slider->thumbY, wid->w + 5 * 2, 15, SILVER);
			ChDrawRectUnfilled(
				win->canv, wid->x - 5, wid->y + slider->thumbY, wid->w + 5 * 2, 15, GRAY);
		}
	}

	if (slider->type == CHITRALEKHA_SLIDER_HORIZONTAL) {
		ChDrawRect(win->canv,
				   slider->bound.x,
				   slider->bound.y,
				   slider->bound.w,
				   slider->bound.h,
				   ChWindowGetBackgroundColor(win));
		ChDrawRect(win->canv, wid->x, wid->y, wid->w, wid->h, ChWindowGetBackgroundColor(win));

		/* draw the progress */
		if (slider->currentVal != 0) {
			uint32_t col1 = 0xFF84C8EC;
			uint32_t col2 = 0xFF3A96AC;
			if (slider->useCustomColor) {
				col1 = slider->customColor1;
				col2 = slider->customColor2;
			}
			ChColorDrawVerticalGradient(win->canv,
										wid->x,
										wid->y,
										((wid->x + slider->progressPixel) - wid->x),
										9,
										col1,
										col2);
		}

		uint32_t outlineColor = 0xFF545454;
		if (slider->useCustomColor)
			outlineColor = slider->outlineColor;
		ChDrawRectUnfilled(win->canv, wid->x, wid->y, wid->w, wid->h, outlineColor);

		/* ! Slider thumb */
		if (slider->thumbVisible) {
			ChDrawRect(win->canv, wid->x + slider->thumbX, wid->y - 5, 15, wid->h + 5 * 2, SILVER);
			ChDrawRectUnfilled(
				win->canv, wid->x + slider->thumbX, wid->y - 5, 15, wid->h + 5 * 2, GRAY);
		}
	}
}