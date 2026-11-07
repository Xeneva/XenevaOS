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

#ifndef __SLIDER_H__
#define __SLIDER_H__

#include "../chitralekha.h"
#include "base.h"
#include "window.h"

#define CHITRALEKHA_SLIDER_HORIZONTAL 1
#define CHITRALEKHA_SLIDER_VERTICAL	  2

typedef struct _slider_ {
	ChWidget base;
	ChRect bound;
	int thumbX;
	int thumbY;
	int lastThumbX;
	int lastThumbY;
	float max;
	float min;
	float currentVal;
	int progressPixel;
	float stepSize;
	bool thumbVisible;
	bool useCustomColor;
	/* Custom Color gradient */
	uint32_t customColor1;
	uint32_t customColor2;
	uint32_t outlineColor;
	uint8_t type;
} ChSlider;

/*
* ChCreateSlider -- create a slider widget
* @param sliderType -- type of the slider
*/
XE_EXTERN XE_LIB ChSlider* ChCreateSlider(uint8_t sliderType, int x, int y, int length);

/*
* ChSliderSetStepSize -- set the step size for given slider
* @param slider -- Pointer to slider
* @param stepSz -- step size in float
*/
XE_EXTERN XE_LIB void ChSliderSetStepSize(ChSlider* slider, float stepSz);

/*
* ChSliderGetStepSize -- get the current step size for
* given slider
* @param slider -- Pointer to slider
*/
XE_EXTERN XE_LIB float ChSliderGetStepSize(ChSlider* slider);

/*
* ChSliderGetCurrentValue -- returns the current value
* from given slider
* @param slider -- Pointer to slider
*/
XE_EXTERN XE_LIB float ChSliderGetCurrentValue(ChSlider* slider);

/*
* ChSliderSetValue -- set a desired value to
* the slider
* @param slider -- Pointer to slider
* @param value -- progress value
*/
XE_EXTERN XE_LIB void ChSliderSetValue(ChSlider* slider, float value);

/*
* ChSliderSetMax -- set maximum value limit
* @param slider -- Pointer to slider
* @param max -- Maximum value
*/
XE_EXTERN XE_LIB void ChSliderSetMax(ChSlider* slider, float max);

/*
* ChSliderGetMax -- return the maximum value limit
* @param slider -- Pointer to slider
*/
XE_EXTERN XE_LIB float ChSliderGetMax(ChSlider* slider);

/*
* ChSliderSetMin -- set the minimum value limit for
* the given slider
* @param slider -- Pointer to slider
* @param min -- Minimum value
*/
XE_EXTERN XE_LIB void ChSliderSetMin(ChSlider* slider, float min);

/*
* ChSliderGetMin -- returns the minimum value
* limit for the given slider
* @param slider -- Pointer to slider
*/
XE_EXTERN XE_LIB float ChSliderGetMin(ChSlider* slider);

#endif