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

#ifndef __AU_CONSOLE_H__
#define __AU_CONSOLE_H__

#include <aurora.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif

#define SCREEN_SETMODE    200
#define SCREEN_GETWIDTH   201
#define SCREEN_GETHEIGHT  202
#define SCREEN_GETBPP     203
#define SCREEN_SETBPP     204
#define SCREEN_UPDATE     205
#define SCREEN_GET_SCANLINE  206
#define SCREEN_GET_PITCH     207
#define SCREEN_REG_MNGR      208
#define SCREEN_GET_FB     209
#define SCREEN_RESTORE_BOOT_FB 210

typedef struct _aucon_ {
	uint32_t width;
	uint32_t height;
	uint32_t *buffer;
	uint32_t bpp;
	uint16_t scanline;
	uint32_t size;
	uint32_t pitch;
	bool early_mode;
	/* desktop_width/height -- size reported to the compositor for the
	 * desktop scanout. Equals width/height unless the loader passed a
	 * manual desktop override (resolutions the firmware GOP never
	 * offers) and the console framebuffer is at least that big; a
	 * bigger override needs a GPU driver to repoint the console to it
	 * first. Text drawing always uses width/height (real GOP). --axiss */
	uint32_t desktop_width;
	uint32_t desktop_height;
}AuConsole;

/*
* AuConsoleInitialize -- initialize kernel direct screen
* console
* @param info -- Pointer to kernel boot info structure
*/
extern void AuConsoleInitialize(PKERNEL_BOOT_INFO info, bool early);

/*
* AuConsolePostInitialise -- initialise the post console process
* @param info -- pointer to kernel boot info structure
*/
extern void AuConsolePostInitialise(PKERNEL_BOOT_INFO info);


/**
 * @brief Prints string to console output
 * @param str -- string to print
 */
extern void AuPutS(char* str);

/**
 * @brief Prints string to console output
 * @param str -- string to print
 * @param col -- foreground color
 */
extern void AuPutS_Color(char* str, uint32_t color);
/*
* AuTextOut -- standard text printing function
* for entire kernel
* @param text -- text to output
*/
AU_EXTERN AU_EXPORT void AuTextOut(const char* text, ...);

AU_EXTERN AU_EXPORT void AuTextOut_profiler(const char* text, ...);

/*
* AuConsoleEarlyEnable -- enables or disable early
* mode text output
* @param value -- boolean value
*/
void AuConsoleEarlyEnable(bool value);

/*
* AuConsoleSetDisplayOwned -- hand the scanout to the compositor.
* Once the display owner claims PROCESS_TOKEN_DISPLAY, framebuffer text
* output is muted (UART logging is unaffected). The compositor draws into
* the same physical framebuffer, so any further AuTextOut would scribble
* over the live desktop. --term never claims the token, so its framebuffer
* console keeps working.
*/
void AuConsoleSetDisplayOwned(void);

/*
* AuConsoleRestoreBootFb -- move the kernel console back onto the boot
* framebuffer (see aucon.c). Used by TERM mode, which has no compositor
* to present the GPU backing the console was repointed at.
* Returns 1 when restored, 0 when there is nothing to restore.
*/
int AuConsoleRestoreBootFb(void);

/*
* AuConsolePresentFn -- mirrors console damage onto another scanout.
* src is the console framebuffer, src_pitch its byte stride; (x, y, w, h)
* is the damaged rectangle in pixels. The virtio-gpu driver installs this
* so the text console stays visible on its display while no compositor
* owns the screen (TERM mode). --axiss
*/
typedef void (*AuConsolePresentFn)(uint32_t* src, uint32_t src_pitch,
	int x, int y, int w, int h);
AU_EXTERN AU_EXPORT void AuConsoleSetPresentHook(AuConsolePresentFn fn);

/*
 * AuConsolePresentRect -- hand an explicit damage rectangle to the scanout
 * mirror (see AuConsoleSetPresentHook). This is the path SCREEN_UPDATE takes
 * for userspace, which paints into the mapped framebuffer directly.
 */
void AuConsolePresentRect(int x, int y, int w, int h);

/*
 * AuConsoleGetScreenWidth -- return the screen
 * width
 */
AU_EXTERN AU_EXPORT uint32_t AuConsoleGetScreenWidth();

/*
 * AuConsoleGetScreenHeight -- return the screen
 * height
 */
AU_EXTERN AU_EXPORT uint32_t AuConsoleGetScreenHeight();

AU_EXTERN AU_EXPORT void AuConsoleSetConInfo(uint64_t phys, uint64_t virt, size_t xres, size_t yres);

#endif
