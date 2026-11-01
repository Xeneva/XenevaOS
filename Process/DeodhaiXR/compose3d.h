/**
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

#ifndef __COMPOSE3D_H__
#define __COMPOSE3D_H__

/**
 * DeodhaiXR 3D compositor (software).
 *
 * World model -- the camera sits at the origin and can only rotate:
 *  - yaw   : true 3D rotation about the vertical axis
 *  - pitch : y-shearing (the screen image slides vertically), which keeps
 *            every window a planar, perspective-correct quad and keeps the
 *            panorama lookup a pure table walk
 * World units are window pixels: a window at a depth equal to the focal
 * length (screen width / 2 at the 90 degree field of view) facing the
 * camera appears exactly 1:1, which is where every window is placed when
 * 3D mode is switched on (so nothing jumps).
 *
 * Layers, back to front:
 *  1. panorama  -- a cylindrical wallpaper rendered into the back surface
 *                  (DeoGetBackSurface), so every existing restore path
 *                  (DeodhaiBackSurfaceUpdate, alpha HUD blends, the glass
 *                  blur bake) keeps working unchanged
 *  2. normal windows -- textured quads, painter-sorted by depth; popups
 *                  ride on their parent's plane
 *  3. HUD       -- always-on-top windows (dock, launcher, systray) stay
 *                  screen-locked and go through the existing 2D composers
 *  4. cursor
 *
 * Zoomed windows are screen-locked by definition: while one is visible the
 * frame is delegated to XRComposeFrame unchanged.
 *
 * Cost per frame (N windows, R repaint regions):
 *  - bookkeeping          : O(N) -- hash lookup by Window*, insertion sort
 *                           on a nearly sorted list, one sync per frame
 *  - idle / client damage : O(damaged screen area x overlapping windows),
 *                           same shape as the 2D dirty path
 *  - window moved/eased   : O(old + new projected area)
 *  - camera moved         : O(screen pixels) for the panorama: per column
 *                           work hoisted to an O(W) table each frame, four
 *                           columns per NEON step, 2x2 blocks while dragging
 *  - window pixels        : O(projected area), exact per-scanline spans;
 *                           1:1 rows blend straight from the source with
 *                           NEON, perspective rows sample exactly every <=16
 *                           pixels (error < 1/4 texel) and step in fixed
 *                           point, then blend with NEON
 */

#include <stdint.h>
#include <chitralekha.h>
#include "window.h"

#define DEODHAI3D_MAX_WINDOWS 128
#define DEODHAI3D_MAX_REGIONS 64

/**
 * @brief Deodhai3DInitialise -- set up the 3D compositor. Call once after
 * the wallpaper is in the back surface and the screen blur is baked.
 * Snapshots the flat wallpaper (used as the fallback panorama and to
 * restore 2D mode). 3D mode starts disabled.
 * @param canvas -- Pointer to the system canvas
 */
extern void Deodhai3DInitialise(ChCanvas* canvas);

/**
 * @brief Deodhai3DLoadPanorama -- load a 360 degree cylindrical panorama
 * (JPEG). Without one, the flat wallpaper is wrapped with mirror repeat.
 * @param path -- path of the jpeg
 * @return true when the panorama replaced the current texture
 */
extern bool Deodhai3DLoadPanorama(const char* path);

/**
 * @brief Deodhai3DSetEnabled -- switch between 3D and the classic 2D
 * compositor. Enabling places every window where it is on screen now;
 * disabling restores the flat wallpaper and the 2D layout.
 */
extern void Deodhai3DSetEnabled(bool enable);

/**
 * @brief Deodhai3DToggle -- flips 3D mode
 */
extern void Deodhai3DToggle();

/**
 * @brief Deodhai3DEnabled -- returns true while 3D mode is active
 */
extern bool Deodhai3DEnabled();

/**
 * @brief XRComposeFrame3D -- the 3D compose pipeline; drop-in for
 * XRComposeFrame (falls back to it while 3D mode is off or a window is
 * zoomed).
 * @param canvas -- Pointer to the system canvas
 */
extern void XRComposeFrame3D(ChCanvas* canvas);

/**
 * @brief Deodhai3DHandleMouse -- 3D input: drag on empty space turns the
 * camera, drag a titlebar to move a window around you, Ctrl + titlebar drag
 * pushes it away / pulls it closer. Everything else is forwarded to the
 * window under the cursor in the coordinates the client expects.
 * @param canvas -- Pointer to the system canvas
 * @param x -- Mouse x location
 * @param y -- Mouse y location
 * @param button -- Mouse button state
 * @return true if the event was consumed (skip the 2D handlers)
 */
extern bool Deodhai3DHandleMouse(ChCanvas* canvas, int x, int y, int button);

/**
 * @brief Deodhai3DSetCamera -- set camera orientation (radians)
 * @param yaw -- rotation about the vertical axis, + turns right
 * @param pitch -- + looks up; clamped to +-DEODHAI3D_PITCH_LIMIT
 */
extern void Deodhai3DSetCamera(float yaw, float pitch);

/**
 * @brief Deodhai3DGetCamera -- get camera orientation (radians)
 */
extern void Deodhai3DGetCamera(float* yaw, float* pitch);

/**
 * @brief Deodhai3DWindowSetPosition -- translate a window in 3D space. The
 * window turns to face the camera.
 * @param win -- Pointer to window
 * @param x, y, z -- world position of the window centre (pixel units,
 * +x right, +y down, +z forward)
 * @return false if the window is unknown or the slot table is full
 */
extern bool Deodhai3DWindowSetPosition(Window* win, float x, float y, float z);

/**
 * @brief Deodhai3DWindowGetPosition -- get the world position of a window
 */
extern bool Deodhai3DWindowGetPosition(Window* win, float* x, float* y, float* z);

/**
 * @brief Deodhai3DWindowPushDepth -- move a window along its view ray
 * (it stays under the same screen spot, only grows / shrinks). Eased over
 * a few frames.
 * @param win -- Pointer to window (a popup moves its parent)
 * @param factor -- distance multiplier: > 1 pushes back, < 1 pulls closer
 */
extern bool Deodhai3DWindowPushDepth(Window* win, float factor);

/**
 * @brief Deodhai3DWindowSendToBack -- eases a window behind every other
 * window (painter order is by distance, so it draws first)
 */
extern bool Deodhai3DWindowSendToBack(Window* win);

/**
 * @brief Deodhai3DWindowBringToFront -- eases a window in front of every
 * other window
 */
extern bool Deodhai3DWindowBringToFront(Window* win);

/**
 * Tearing. Shake the cursor over a window (4 quick back-and-forth moves,
 * button up): a small slit appears there. Press near it within 1.5 s and
 * pull: the window tears open from that point to the cursor, following
 * it in 3D on the window's plane. The opening shows whatever is behind
 * (wallpaper or other windows), edges are ragged with a paper-fibre rim
 * and a soft shadow, and the window's own pixels beside the crack are
 * pushed apart. While torn the application receives no mouse or keyboard
 * input (the page can still be moved by its titlebar). Shake over it again
 * to mend it. Purely compositor side: the client buffer is never touched.
 */

/**
 * @brief Deodhai3DWindowTear -- tear a window open programmatically from
 * (ax, ay) to (tx, ty), window texels
 */
extern bool Deodhai3DWindowTear(Window* win, float ax, float ay, float tx, float ty);

/**
 * @brief Deodhai3DWindowMend -- heal a window's tear (animated); input
 * returns once it has closed
 */
extern bool Deodhai3DWindowMend(Window* win);

/**
 * @brief Deodhai3DWindowInputBlocked -- true while a window (or the window
 * a popup rides on) is torn. DeodhaiBroadcastKey consults this.
 */
extern bool Deodhai3DWindowInputBlocked(Window* win);

/**
 * @brief Deodhai3DWindowTearBounds -- screen bounds of a window's tear as
 * of the last composed frame
 */
extern bool Deodhai3DWindowTearBounds(Window* win, Rect* bounds);

/**
 * @brief Deodhai3DHandleKey -- 3D keyboard shortcuts, all with Ctrl held:
 *   Ctrl+3  toggle 3D compositing
 *   Ctrl+-  push the focused window back      Ctrl+=  pull it closer
 *   Ctrl+[  send it behind every window       Ctrl+]  bring it to the front
 * @param key -- character from ChitralekhaGetKeyPress
 * @param ctrl -- Ctrl state
 * @return true if the key was consumed (do not forward to the window)
 */
extern bool Deodhai3DHandleKey(int key, bool ctrl);

/**
 * @brief Deodhai3DScreenToWindow -- maps a screen pixel onto a window's own
 * pixel grid using the last composed frame
 * @return true when the pixel lands on the window
 */
extern bool Deodhai3DScreenToWindow(Window* win, int sx, int sy, int* lx, int* ly);

#endif
