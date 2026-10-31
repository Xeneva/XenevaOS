/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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


#ifndef __SVGA_H__
#define __SVGA_H__

/* SVGA_REG_ENABLE bit definitions */
#define SVGA_REG_ENABLE_DISABLE 0
#define SVGA_REG_ENABLE_ENABLE 1
#define SVGA_REG_ENABLE_HIDE   2
#define SVGA_REG_ENABLE_ENABLE_HIDE (SVGA_REG_ENABLE_ENABLE | \
 SVGA_REG_ENABLE_HIDE)

/* CURSOR bits */
#define SVGA_CURSOR_ON_HIDE  0x0
#define SVGA_CURSOR_ON_SHOW  0X1
#define SVGA_CURSOR_ON_REMOVE_FROM_FB  0x2  // remove the cursor from framebuffer to see the behind objects
#define SVGA_CURSOR_ON_RESTORE_TO_FB   0x3  // put the cursor to framebuffer to hide the behind objects

#define SVGA_FB_MAX_TRACEABLE_SIZE 0x1000000

#define SVGA_MAX_PSEUDOCOLOR_DEPTH 8
#define SVGA_MAX_PSEUDOCOLORS  (1 << SVGA_MAX_PSEUDOCOLOR_DEPTH)
#define SVGA_NUM_PALETTE_REGS (3 * SVGA_MAX_PSEUDOCOLORS)

#define SVGA_MAGIC  0x900000UL
#define SVGA_MAKE_ID(ver) (SVGA_MAGIC << 8 | (ver))

#define SVGA_VERSION_2  2
#define SVGA_ID_2    SVGA_MAKE_ID(SVGA_VERSION_2)


#endif