/**
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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

#include "nmdapha.h"
#include <sys/_kefile.h>
#include <sys/mman.h>

ButtonInfo* gobtn;
ButtonInfo* gobtnHover;
ButtonInfo* gobtnClick;

static ButtonInfo* go_icon_or_fallback(ButtonInfo* preferred, ButtonInfo* fallback) {
	if (preferred && preferred->imageData)
		return preferred;
	if (fallback && fallback->imageData)
		return fallback;
	return NULL;
}

void NamdaphaGoButtonPaint(NamdaphaButton* button, ChWindow* win) {
	ButtonInfo* icon = go_icon_or_fallback(gobtn, NULL);
	if (button->hover)
		icon = go_icon_or_fallback(gobtnHover, gobtn);
	if (button->clicked)
		icon = go_icon_or_fallback(gobtnClick, gobtn);

	/* Click/hover used to fill with NAMDAPHA_COLOR (alpha 0) and then
	 * draw GoIconL/GoIconS, which are not shipped. That wiped the sprite
	 * from the staging buffer and the dirty copy published empty glass. --axiss */
	if (button->clicked)
		ChDrawRect(win->canv, button->x, button->y, button->w, button->h, GO_BUTTON_PRESSED);
	else if (button->hover)
		ChDrawRect(win->canv, button->x, button->y, button->w, button->h, GO_BUTTON_HOVER);
	else
		ChDrawRect(win->canv, button->x, button->y, button->w, button->h, NAMDAPHA_COLOR);

	if (icon && gobtn)
		NmButtonInfoDrawIcon(icon,
							 win->canv,
							 button->x + button->w / 2 - gobtn->iconWidth / 2,
							 button->y + button->h / 2 - gobtn->iconHeight / 2);
}

NamdaphaButton* NamdaphaInitialiseGoButton(ChWindow* win) {
	NamdaphaButton* gobutton =
		NmCreateButton(10, win->info->height - 60, NAMDAPHA_WIDTH - 20, 50, "Go");
	gobtn = NmCreateButtonInfo("/icons/GoIcon.bmp");
	gobtnHover = NmCreateButtonInfo("/icons/GoIconL.bmp");
	gobtnClick = NmCreateButtonInfo("/icons/GoIconS.bmp");
	NmButtonInfoRead(gobtn);
	NmButtonInfoRead(gobtnHover);
	NmButtonInfoRead(gobtnClick);
	gobutton->drawNamdaphaButton = NamdaphaGoButtonPaint;
	gobutton->nmbuttoninfo = gobtn;
	gobutton->actionHandler = 0;
	return gobutton;
}
