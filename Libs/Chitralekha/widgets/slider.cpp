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

extern void ChDefaultSliderPainter(ChWidget* wid, ChWindow* win);

void ChSliderDestroy(ChWidget* widget, ChWindow* win) {
	ChSlider* slider = (ChSlider*)widget;
	if (win->focusedWidget == widget)
		win->focusedWidget = NULL;
	free(slider);
	_KePrint("Slider destroyed \r\n");
}

/*
 * ChSliderMouseEvent -- Mouse event handler for slider
 */
void ChSliderMouseEvent(ChWidget* widget, ChWindow* win, int x, int y, int button) {
	ChSlider* slider = (ChSlider*)widget;
	bool actionRequired = false;
	if (button) {
		if (slider->type == CHITRALEKHA_SLIDER_VERTICAL) {
			slider->thumbY = y - (win->info->y + widget->y + 15 / 2);
			if (slider->thumbY <= 0)
				slider->thumbY = 0;
			slider->progressPixel = slider->thumbY;
			if (slider->progressPixel == 0)
				slider->progressPixel = 1;

			//! update the current values
			//if (slider->thumbY < slider->lastThumbY) {
			//	slider->currentVal = ((float)slider->thumbY / (float)slider->base.h); //+= slider->stepSize;
			//	/*if (slider->currentVal >= slider->max)
			//		slider->currentVal = slider->max;*/
			//}else if (slider->lastThumbY < slider->thumbY) {
			//	slider->currentVal -= slider->stepSize;
			//	if (slider->currentVal <= slider->min)
			//		slider->currentVal = slider->min;
			//}
			float ratio = 1.0f - ((float)slider->thumbY / (float)slider->base.h);
			slider->currentVal = slider->min + ratio * (slider->max - slider->min);

			if ((win->info->y + widget->y + slider->thumbY) <= (win->info->y + widget->y))
				slider->thumbY = 0;

			if ((win->info->y + widget->y + slider->thumbY + 15) >=
				(win->info->y + widget->y + widget->h)) {
				slider->thumbY =
					(win->info->y + widget->y + widget->h) - (win->info->y + widget->y + 15);
				slider->progressPixel = 0;
			}

			slider->lastThumbY = slider->thumbY;
			if (slider->base.ChPaintHandler)
				slider->base.ChPaintHandler((ChWidget*)slider, win);
			ChWindowUpdate(
				win, slider->bound.x, slider->bound.y, slider->bound.w, slider->bound.h, 0, 1);

			win->focusedWidget = widget;
			actionRequired = true;
		}

		if (slider->type == CHITRALEKHA_SLIDER_HORIZONTAL) {
			slider->thumbX = x - (win->info->x + widget->x + 15 / 2);
			slider->progressPixel = slider->thumbX;

			if (slider->thumbX > slider->lastThumbX) {
				slider->currentVal += slider->stepSize;
				if (slider->currentVal >= slider->max)
					slider->currentVal = slider->max;
			} else if (slider->lastThumbX > slider->thumbX) {
				slider->currentVal -= slider->stepSize;
				if (slider->currentVal <= slider->min)
					slider->currentVal = slider->min;
			}

			if ((win->info->x + widget->x + slider->thumbX) <= (win->info->x + widget->x)) {
				slider->thumbX = 0;
				slider->progressPixel = 0;
			}

			if ((win->info->x + widget->x + slider->thumbX + 15) >=
				(win->info->x + widget->x + widget->w)) {
				slider->thumbX =
					(win->info->x + widget->x + widget->w) - (win->info->x + widget->x + 15);
			}

			slider->lastThumbX = slider->thumbX;
			if (slider->base.ChPaintHandler)
				slider->base.ChPaintHandler((ChWidget*)slider, win);
			ChWindowUpdate(
				win, slider->bound.x, slider->bound.y, slider->bound.w, slider->bound.h, 0, 1);
			win->focusedWidget = widget;
			actionRequired = true;
		}
	}

	/* call the action handler for required action */
	if (actionRequired) {
		if (widget->ChActionHandler)
			widget->ChActionHandler(widget, win);
	}
}

/*
 * ChCreateSlider -- create a slider widget
 * @param sliderType -- type of the slider
 */
ChSlider* ChCreateSlider(uint8_t sliderType, int x, int y, int length) {
	ChSlider* slider = (ChSlider*)malloc(sizeof(ChSlider));
	memset(slider, 0, sizeof(ChSlider));
	slider->base.x = x;
	slider->base.y = 26 + y;
	slider->thumbVisible = true;
	slider->useCustomColor = 0;
	switch (sliderType) {
	case CHITRALEKHA_SLIDER_VERTICAL:
		slider->base.w = 10;
		slider->base.h = length;
		slider->thumbY = slider->base.h - 15;
		if (slider->thumbVisible) {
			slider->bound.x = slider->base.x - 5;
			slider->bound.w = slider->base.w + 5 * 2;
			slider->bound.y = slider->base.y;
			slider->bound.h = slider->base.h;
		} else {
			slider->bound.x = slider->base.x;
			slider->bound.w = slider->base.w; // +5 * 2;
			slider->bound.y = slider->base.y;
			slider->bound.h = slider->base.h;
		}
		break;
	case CHITRALEKHA_SLIDER_HORIZONTAL:
		slider->base.w = length;
		slider->base.h = 10;
		slider->thumbX = 0;
		if (slider->thumbVisible) {
			slider->bound.x = slider->base.x;
			slider->bound.w = slider->base.w;
			slider->bound.y = slider->base.y - 5;
			slider->bound.h = slider->base.h + 5 * 2;
		} else {
			slider->bound.x = slider->base.x;
			slider->bound.w = slider->base.w; // +5 * 2;
			slider->bound.y = slider->base.y;
			slider->bound.h = slider->base.h;
		}
		break;
	}

	slider->type = sliderType;
	slider->base.ChDestroy = ChSliderDestroy;
	slider->base.ChMouseEvent = ChSliderMouseEvent;
	slider->base.ChPaintHandler = ChDefaultSliderPainter;
	slider->stepSize = 1.0;
	slider->min = 0.0;
	slider->currentVal = slider->min;
	slider->max = 100.0;
	return slider;
}

/*
 * ChSliderSetStepSize -- set the step size for given slider
 * @param slider -- Pointer to slider
 * @param stepSz -- step size in float
 */
void ChSliderSetStepSize(ChSlider* slider, float stepSz) {
	slider->stepSize = stepSz;
}

/*
 * ChSliderGetStepSize -- get the current step size for
 * given slider
 * @param slider -- Pointer to slider
 */
float ChSliderGetStepSize(ChSlider* slider) {
	return slider->stepSize;
}

/*
 * ChSliderGetCurrentValue -- returns the current value
 * from given slider
 * @param slider -- Pointer to slider
 */
float ChSliderGetCurrentValue(ChSlider* slider) {
	return slider->currentVal;
}

/*
 * ChSliderSetValue -- set a desired value to 
 * the slider
 * @param slider -- Pointer to slider
 * @param value -- progress value 
 */
void ChSliderSetValue(ChSlider* slider, float value) {
	slider->currentVal = value * slider->stepSize;
	if (slider->currentVal > slider->max)
		slider->currentVal = slider->max;
	if (slider->currentVal < slider->min)
		slider->currentVal = slider->min;
	switch (slider->type) {
	case CHITRALEKHA_SLIDER_VERTICAL:
		slider->progressPixel = (int)slider->currentVal;
		slider->thumbY = slider->progressPixel - 15;
		break;
	case CHITRALEKHA_SLIDER_HORIZONTAL:
		slider->progressPixel = (int)slider->currentVal;
		slider->thumbX = slider->progressPixel - 15;
		break;
	}
}

/*
 * ChSliderSetMax -- set maximum value limit
 * @param slider -- Pointer to slider
 * @param max -- Maximum value
 */
void ChSliderSetMax(ChSlider* slider, float max) {
	slider->max = max;
}

/*
 * ChSliderGetMax -- return the maximum value limit
 * @param slider -- Pointer to slider
 */
float ChSliderGetMax(ChSlider* slider) {
	return slider->max;
}

/*
 * ChSliderSetMin -- set the minimum value limit for
 * the given slider
 * @param slider -- Pointer to slider
 * @param min -- Minimum value
 */
void ChSliderSetMin(ChSlider* slider, float min) {
	slider->min = min;
	slider->currentVal = slider->min;
}

/*
 * ChSliderGetMin -- returns the minimum value
 * limit for the given slider
 * @param slider -- Pointer to slider
 */
float ChSliderGetMin(ChSlider* slider) {
	return slider->min;
}
