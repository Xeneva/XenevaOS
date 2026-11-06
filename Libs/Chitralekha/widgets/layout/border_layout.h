/**
* @file border_layout.h
* 
* BSD 2-Clause License
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

#ifndef __BORDER_LAYOUT_H__
#define __BORDER_LAYOUT_H__

#include "..\window.h"
#include "..\list.h"
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	typedef enum {
		CH_BORDER_ALIGN_CENTER = 0,
		CH_BORDER_ALIGN_TOP,
		CH_BORDER_ALIGN_BOTTOM,
		CH_BORDER_ALIGN_LEFT,
		CH_BORDER_ALIGN_RIGHT
	} ChBorderAlignment;

#define CH_WIDEGT_BORDERLAYOUT 0x20

	typedef struct {
		ChWidget* widget;
		ChBorderAlignment align;
		int prefW;
		int prefH;
	} ChBorderItem;

	typedef struct _ChBorderLayout_ {
		ChWidget base;
		list_t* items;
		int hgap, vgap;
		int defSideW;
		int defBarH;
	} ChBorderLayout;

	/** 
     * @brief ChCreateBorderLayout -- create a new border layout with given parameters
     * @param x -- Border Layout X position
     * @param y -- Border Layout Y position
     * @param w -- Width of the Border Layout
     * @param h -- height of the Border Layout
     * @param hgap -- Horizontal padding between each alignment
     * @param vgap -- Verical padding between each alignment
     */
	XE_EXPORT ChBorderLayout* ChCreateBorderLayout(int x, int y, int w, int h, int hgap, int vgap);

	/**
     * @brief ChBorderLayoutResize -- resize the border layout with given 
     * parameter 
     * @param l -- Pointer to border layout
     * @param x -- x location of the border layout
     * @param y -- y location of the border layout
     * @param w -- new width of the border layout
     * @param h -- new height of the border layout
     */
	XE_EXPORT void ChBorderLayoutResize(ChBorderLayout * l, int x, int y, int w, int h);

	/**
     * @brief ChBorderLayoutAdd -- add a widget to the border layout
     * by its alignment
     * @param l -- Pointer to border layout
     * @param w -- Pointer to the widget to add
     * @param align -- alignment number
     */
	XE_EXPORT void ChBorderLayoutAdd(ChBorderLayout * l, ChWidget * w, ChBorderAlignment align);

	/**
     * @brief ChBorderLayoutDoLayout -- reorganise/refresh sel
     * @param l -- Pointer to border layout
     */
	XE_EXPORT void ChBorderLayoutDoLayout(ChBorderLayout * l);

	/**
     * @brief ChBorderLayoutSetPreferred -- set preferred border 
     * width and height for widget geometry
     * @param l -- Pointer to Border Layout
     * @param w -- Pointer to widget
     * @param pw -- Preferred width
     * @param ph -- preferred height
     */
	XE_EXPORT void ChBorderLayoutSetPreferred(ChBorderLayout * l, ChWidget * w, int pw, int ph);

	/**
     * @brief ChBorderLayoutSetDefaults -- set default border geometry
     * @param l -- Pointer to Border Layout
     * @param sideW -- side width
     * @param barH -- bar height
     */
	XE_EXPORT void ChBorderLayoutSetDefaults(ChBorderLayout * l, int sideW, int barH);

#ifdef __cplusplus
}
#endif

#endif