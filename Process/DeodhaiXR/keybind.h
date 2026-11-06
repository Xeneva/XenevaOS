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

#ifndef __DEODHAI_XR_KEYBIND_H__
#define __DEODHAI_XR_KEYBIND_H__

/**
 * @brief _DeodhaiKeyBindInitialize -- initialize
 * key binding structure to zero
 */
extern void _DeodhaiKeyBindInitialize();

/**
 *  @brief _DeodhaiRegisterSpecialKey -- register
 * special key with special code
 * @param key -- Keyboard KEY code to register
 * @param code -- special code to register 
 * @param ctrl -- control key specific
 * @param alt -- Alternate key specific
 */
extern void _DeodhaiRegisterSpecialKey(int key, int code,bool ctrl, bool alt);

/**
 *  @brief _DeodhaiGetSpecialCode -- return
 * registered special code for given key
 * @param key -- key to search
 * @param ctrl -- ctrl check 
 */
extern int _DeodhaiGetSpecialCode(int key, bool ctrl);

#endif