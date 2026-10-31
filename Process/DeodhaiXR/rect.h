/**
* BSD 2-Clause License
* 
* @file rect.h
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

#ifndef __RECT_H__
#define __RECT_H__

#include "deodxr.h"

/**
* @brief RectSetLeft -- set a new left edge value to
* the given rect
* @param rect -- pointer to the rect structure
* @param new_left -- new left edge value
*/
extern int RectSetLeft(Rect* rect, int new_left);

/**
* @brief RectSetRight -- sets a new right edge value to
* the given rect
* @param rect -- pointer to the rect structure
* @param new_right -- new right value
*/
extern int RectSetRight(Rect* rect, int new_right);

/**
* @brief RectSetTop -- set a new top edge value to the given
* rectangle
* @param rect -- pointer to the rect structure
* @param new_top -- top edge value
*/
extern int RectSetTop(Rect* rect, int new_top);

/**
* @brief RectSetBottom -- set a new bottom edge value to the given
* rectangle
* @param rect -- pointer to the rect structure
* @param new_bottom -- new bottom edge value
*/
extern int RectSetBottom(Rect* rect, int new_bottom);

/**
* @brief RectGetLeft -- returns the left edge value
* @param rect -- pointer to the rect structure
*/
extern int RectGetLeft(Rect* rect);

/**
* @brief RectGetRight -- returns the right edge value
* @param rect -- pointer to the rect structure
*/
extern int RectGetRight(Rect* rect);

/**
* @brief RectGetTop -- returns the top edge value
* @param rect -- pointer to the rect structure
*/
extern int RectGetTop(Rect* rect);

/**
* @brief RectGetBottom -- returns the bottom edge value
* @param rect -- pointer to the rect structure
*/
extern int RectGetBottom(Rect* rect);

#endif