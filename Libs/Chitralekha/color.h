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

#ifndef __COLOR_H__
#define __COLOR_H__

#include <stdint.h>
#include <_xeneva.h>
#include "chitralekha.h"

#ifdef __cplusplus
XE_EXTERN {
#endif

	//! Color codes
#define WHITE	0xFFFFFFFF
#define SILVER	0xFFC0C0C0
#define GRAY	0xFF808080
#define BLACK	0xFF000000
#define RED		0xFFFF0000
#define MAROON	0xFF800000
#define YELLOW	0xFFFFFF00
#define OLIVE	0xFF808000
#define LIME	0xFF00FF00
#define GREEN	0xFF008000
#define AQUA	0xFF00FFFF
#define TEAL	0xFF008080
#define BLUE	0xFF0000FF
#define NAVY	0xFF000080
#define FUCHSIA 0xFFFF00FF
#define PURPLE	0xFF800080

	//! more colors are needed!!
#define DARKRED		  0xFF8B0000
#define BROWN		  0xFFA52A2A
#define FIREBRICK	  0xFFB22222
#define CRIMSON		  0xFFDC143C
#define TOMATO		  0xFFFF6347
#define CORAL		  0xFFFF7F50
#define INDIANRED	  0xFFCD5C5C
#define LIGHTCORAL	  0xFFF08080
#define DARKSALMON	  0xFFE9967A
#define SALMON		  0xFFFA8072
#define ORANGERED	  0xFFFF4500
#define DARKORANGE	  0xFFFF8C00
#define ORANGE		  0xFFFFA500
#define GOLD		  0xFFFFD700
#define DARKGOLDENROD 0xFFB8860B
#define GOLDENROD	  0xFFDAA520
#define PALEGOLDENROD 0xFFEEE8AA
#define DARKKHAKI	  0xFFBDB76B
#define KHAKI		  0xFFF0E68C
#define DARKGREEN	  0xFF006400
#define PALEGREEN	  0xFF98FB98
#define LIGHTSILVER	  0xFFD9D9D9
#define DESKBLUE	  0xFF5B7492
#define MENUCLICKBLUE 0xFF6A7298
#define TITLEBAR_DARK 0xFF2F2F2F
#define LIGHTBLACK	  0xFF3E3E3E
#define MAGENTA		  0xFFFF00FF
#define CYAN		  0xFF00FFFF

#define SET_ALPHA(color, alpha) (((color << 8) >> 8) | ((alpha << 24) & 0xff000000))

#define _RED(color) ((color & 0x00FF0000) / 0x10000)
#define _GRE(color) ((color & 0x0000FF00) / 0x100)
#define _BLU(color) ((color & 0x000000FF) / 0x1)
#define _ALP(color) ((color & 0xFF000000) / 0x1000000)

#define GET_ALPHA(color) ((color >> 24) & 0x000000FF)
#define GET_RED(color)	 ((color >> 16) & 0x000000FF)
#define GET_GREEN(color) ((color >> 8) & 0x000000FF)
#define GET_BLUE(color)	 ((color >> 0) & 0x000000FF)

	/*
	 * ChColorAlphaBlend -- blend two color
	 * @param color1 -- color 1
	 * @param color2 -- color 2
	 * @param opacity -- opacity of alpha
	 */
	XE_LIB uint32_t ChColorAlphaBlend(uint32_t oldColor, uint32_t color2, double opacity);

	/*
	 * ChColorAlphaBlend2 -- blend two color
	 * @param color1 -- self explanatory
	 * @param color2 -- self explanatory
	 */
	XE_LIB uint32_t ChColorAlphaBlend2(uint32_t color1, uint32_t color2);

	/*
	* ChColorDrawVerticalGradient -- draws vertical linear gradient
	* @param canv -- Pointer to canvas
	* @param x -- X coord of the rect
	* @param y -- Y coord of the rect
	* @param w -- Width of the rect
	* @param h -- Height of the rect
	* @param color1 -- starting color
	* @param color2 -- end color
	*/
	XE_LIB void ChColorDrawVerticalGradient(
		ChCanvas * canv, int x, int y, int w, int h, uint32_t color1, uint32_t color2);

	extern uint32_t make_col(uint8_t red, uint8_t green, uint8_t blue);
	extern uint32_t make_col_a(uint8_t red, uint8_t green, uint8_t blue, uint8_t a);

	/*
	* ChColorDrawHorizontalGradient -- draws horizontal linear gradient
	* @param canv -- Pointer to canvas
	* @param x -- X coord of the rect
	* @param y -- Y coord of the rect
	* @param w -- Width of the rect
	* @param h -- Height of the rect
	* @param color1 -- starting color
	* @param color2 -- end color
	*/
	XE_LIB void ChColorDrawHorizontalGradient(
		ChCanvas * canv, int x, int y, int w, int h, uint32_t color1, uint32_t color2);

	/*
	* ChColorDrawVerticalGradient -- draws vertical linear gradient
	* @param canv -- Pointer to canvas
	* @param x -- X coord of the rect
	* @param y -- Y coord of the rect
	* @param w -- Width of the rect
	* @param h -- Height of the rect
	* @param color1 -- starting color
	* @param color2 -- end color
	*/
	XE_LIB void ChColorDrawVerticalGradient(
		ChCanvas * canv, int x, int y, int w, int h, uint32_t color1, uint32_t color2);

	XE_LIB double clamp(double x, double upper, double lower);

	/*
     * ChColorRGBAtoBGRA -- convert RGBA pixels to BGRA888
     * @param col -- Color to convert
     */
	XE_LIB uint32_t ChColorRGBAtoBGRA(uint32_t col);

	/*
     * ChDrawPixelRAW -- draws a pixel to canvas buffer
     * @param canvas -- pointer to canvas
     * @param x -- x position
     * @param y -- y position
     * @param color -- color of the pixel
     */
	XE_LIB void ChDrawPixelRAW(ChCanvas * canvas, int x, int y, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif