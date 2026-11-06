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

#include <keycode.h>
#include "deodxr.h"
#include "keybind.h"

typedef struct _key_bind_{
    int key;
    bool ctrl;
    bool alt;
    int spCode;
}DeodhaiKeyBind;

DeodhaiKeyBind _key_reg[128];

#define REGISTER_KEY_BIND(keycode, spcode,ctrl,alt)  _DeodhaiRegisterSpecialKey(keycode,spcode,ctrl,alt)
/**
 * @brief _DeodhaiKeyBindInitialize -- initialize
 * key binding structure to zero
 */
void _DeodhaiKeyBindInitialize(){
    for (int i = 0; i < 128; i++){
        _key_reg[i].key = 0;
        _key_reg[i].ctrl = 0;
        _key_reg[i].alt = 0;
        _key_reg[i].spCode = 0;
    }

    /** FOR NOW, let's register only two special key  */
    REGISTER_KEY_BIND(KEY_X,DEODHAI_FORCE_OPEN_LAUNCHER,1,0);
    REGISTER_KEY_BIND(KEY_A,DEODHAI_FORCE_OPEN_TERMINAL,1,0);
}

/**
 *  @brief _DeodhaiRegisterSpecialKey -- register
 * special key with special code
 * @param key -- Keyboard KEY code to register
 * @param code -- special code to register 
 * @param ctrl -- is ctrl specific
 * @param alt -- is alt specific
 */
void _DeodhaiRegisterSpecialKey(int key, int code, bool ctrl, bool alt){
    for (int i = 0; i < 128; i++){
        if (_key_reg[i].key == 0){
           _key_reg[i].key = key;
           _key_reg[i].spCode = code;
           _key_reg[i].ctrl = ctrl;
           _key_reg[i].alt = alt;
           return;
        }
    }
}

/**
 *  @brief _DeodhaiGetSpecialCode -- return
 * registered special code for given key
 * @param key -- key to search
 */
int _DeodhaiGetSpecialCode(int keyl, bool ctrl){
    for (int i = 0; i < 128; i++){
        if (_key_reg[i].key == keyl && _key_reg[i].ctrl == ctrl)
            return _key_reg[i].spCode;
        else if (_key_reg[i].key == keyl && _key_reg[i].alt == 1)
            return _key_reg[i].spCode;
    }
    return 0;
}



