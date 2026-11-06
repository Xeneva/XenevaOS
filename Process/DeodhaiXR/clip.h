/**
* BSD 2-Clause License
* 
* @file clip.h
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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

#ifndef __CLIP_H__
#define __CLIP_H__

#include "deodxr.h"

/* Check if two rectangle intersects
* @param r1 -- rectangle one
* @param r2 -- rectangle two
*/
extern bool ClipCheckIntersect(Rect* r1, Rect* r2);

/*
* ClipCalculateRect -- calculate visible rectanlges of current window
* @param sub_rect -- subject rectangle
* @param cut_rect -- cutting rectangle
* @param list -- pointer to the list, where to store all
* visible rectangles
* @param count -- number of rectangles stored in the list
*/
extern void ClipCalculateRect(Rect* sub_rect, Rect* cut_rect, Rect* list, int* count);

/*
* ClipSubtractRect -- calculate visible rectanlges of current window
* @param sub_rect -- subject rectangle
* @param cut_rect -- cutting rectangle
* @param list -- pointer to the list, where to store all
* visible rectangles
* @param count -- number of rectangles stored in the list
*/
extern void ClipSubtractRect(Rect* sub_rect, Rect* cut_rect, Rect* list, int r_count);

extern void ClipGetBehindRect(Rect* sub_rect, Rect* cut_rect, Rect* list, int* r_count);

extern void ClipSubtractRectList(Rect* inputList, int* inputCount, Rect* cut_rect);

#endif