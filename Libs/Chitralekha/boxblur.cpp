/**
* @file boxblur.cpp
* 
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

#include "boxblur.h"

/**
 * @brief ChBoxBlur -- make an image blurred
 * @param canv -- Pointer to canvas
 * @param inputpix -- Input image buffer
 * @param outpix -- Output image buffer
 * @param cx -- x start
 * @param cy -- y start
 * @param w -- width of the image
 * @param h -- height of the image
 */
void ChBoxBlur(ChCanvas* canv, uint32_t* inputpix, uint32_t* outpix, int cx, int cy, int w, int h) {
	for (int j = 0; j < h; j++) {
		for (int i = 0; i < w; i++) {
			int redTotal = 0;
			int greenTotal = 0;
			int blueTotal = 0;
			int alphaTotal = 0;

			for (int row = -1; row <= 1; row++) {
				for (int col = -1; col <= 1; col++) {
					int currentX = cx + i + col;
					int currentY = cy + j + row;

					if (currentX >= 0 && currentX < canv->canvasWidth && currentY >= 0 &&
						currentY < canv->canvasHeight) {
						uint32_t color = inputpix[(currentY * w + currentX)];

						uint8_t red = GET_RED(color);
						uint8_t green = GET_GREEN(color);
						uint8_t blue = GET_BLUE(color);
						uint8_t alpha = GET_ALPHA(color);

						redTotal += red;
						greenTotal += green;
						blueTotal += blue;
						alphaTotal += alpha;
					}
				}
			}

			uint8_t red = redTotal / 9;
			uint8_t green = greenTotal / 9;
			uint8_t blue = blueTotal / 9;
			uint8_t alpha = alphaTotal / 9;

			outpix[(cy + j) * w + (cx + i)] = make_col_a(red, green, blue, alpha);
		}
	}
}