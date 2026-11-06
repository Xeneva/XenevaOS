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

#include "keycode.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

ChitralekhaKey* chkey;

static int _kkybrd_scancode_std[] = {

	//! key			scancode
	KEY_UNKNOWN,	  //0
	KEY_ESCAPE,		  //1
	KEY_1,			  //2
	KEY_2,			  //3
	KEY_3,			  //4
	KEY_4,			  //5
	KEY_5,			  //6
	KEY_6,			  //7
	KEY_7,			  //8
	KEY_8,			  //9
	KEY_9,			  //0xa
	KEY_0,			  //0xb
	KEY_MINUS,		  //0xc
	KEY_EQUAL,		  //0xd
	KEY_BACKSPACE,	  //0xe
	KEY_TAB,		  //0xf
	KEY_Q,			  //0x10
	KEY_W,			  //0x11
	KEY_E,			  //0x12
	KEY_R,			  //0x13
	KEY_T,			  //0x14
	KEY_Y,			  //0x15
	KEY_U,			  //0x16
	KEY_I,			  //0x17
	KEY_O,			  //0x18
	KEY_P,			  //0x19
	KEY_LEFTBRACKET,  //0x1a
	KEY_RIGHTBRACKET, //0x1b
	KEY_RETURN,		  //0x1c
	KEY_LCTRL,		  //0x1d
	KEY_A,			  //0x1e
	KEY_S,			  //0x1f
	KEY_D,			  //0x20
	KEY_F,			  //0x21
	KEY_G,			  //0x22
	KEY_H,			  //0x23
	KEY_J,			  //0x24
	KEY_K,			  //0x25
	KEY_L,			  //0x26
	KEY_SEMICOLON,	  //0x27
	KEY_QUOTE,		  //0x28
	KEY_GRAVE,		  //0x29
	KEY_LSHIFT,		  //0x2a
	KEY_BACKSLASH,	  //0x2b
	KEY_Z,			  //0x2c
	KEY_X,			  //0x2d
	KEY_C,			  //0x2e
	KEY_V,			  //0x2f
	KEY_B,			  //0x30
	KEY_N,			  //0x31
	KEY_M,			  //0x32
	KEY_COMMA,		  //0x33
	KEY_DOT,		  //0x34
	KEY_SLASH,		  //0x35
	KEY_RSHIFT,		  //0x36
	KEY_KP_ASTERISK,  //0x37
	KEY_RALT,		  //0x38
	KEY_SPACE,		  //0x39
	KEY_CAPSLOCK,	  //0x3a
	KEY_F1,			  //0x3b
	KEY_F2,			  //0x3c
	KEY_F3,			  //0x3d
	KEY_F4,			  //0x3e
	KEY_F5,			  //0x3f
	KEY_F6,			  //0x40
	KEY_F7,			  //0x41
	KEY_F8,			  //0x42
	KEY_F9,			  //0x43
	KEY_F10,		  //0x44
	KEY_KP_NUMLOCK,	  //0x45
	KEY_SCROLLLOCK,	  //0x46
	KEY_HOME,		  //0x47
	KEY_KP_8,		  //0x48	//keypad up arrow
	KEY_PAGEUP,		  //0x49
	KEY_KP_2,		  //0x50	//keypad down arrow
	KEY_KP_3,		  //0x51	//keypad page down
	KEY_KP_0,		  //0x52	//keypad insert key
	KEY_KP_DECIMAL,	  //0x53	//keypad delete key
	KEY_UNKNOWN,	  //0x54
	KEY_KP_2,		  //0x55   //mapped because, got fired by extended code keys
	KEY_KP_2,		  //0x56   //mapped because extended key code fire this
	KEY_F11,		  //0x57
	KEY_F12			  //0x58
};

#define SCANCODE_EXTENDED_MASK 0xFF00
#define SCANCODE_EXTENDED_FLAG 0xE000
#define SCANCODE_KEY_MASK	   0x7F
#define SCANCODE_RELEASE_BIT   0x80

#define KEYCODE_TABLE_SIZE (sizeof(_kkybrd_scancode_std) / sizeof(_kkybrd_scancode_std[0]))

/**
 * @brief ChitralekhaKeyInitialise -- initialise keycode
 * library, this is automatically called during starting of
 * chitralekha appplication
 */
void ChitralekhaKeyInitialise() {
	chkey = (ChitralekhaKey*)malloc(sizeof(ChitralekhaKey));
	memset(chkey, 0, sizeof(ChitralekhaKey));
}

/* 
 * ChitralekhaProcessKey -- looks for special keys to process
 * @param code -- scancode
 */
void ChitralekhaProcessKey(int code) {
	/* virtio-kbd tags extended keys (arrows, Home/End, Super, ...) as
	 * (0xE0 << 8) | make_code -- mask down to the low 7 bits like
	 * ChitralekhaGetKeyPress already does, instead of indexing the raw
	 * tagged value straight into the table --axiss */
	bool released = (code & SCANCODE_RELEASE_BIT) != 0;
	unsigned index = code & SCANCODE_KEY_MASK;
	if (index >= KEYCODE_TABLE_SIZE)
		return;
	int key = _kkybrd_scancode_std[index];

	if (released) {
		switch (key) {
		case KEY_LCTRL:
		case KEY_RCTRL:
			chkey->_ctrl = false;
			break;
		case KEY_LSHIFT:
		case KEY_RSHIFT:
			chkey->_shift = false;
			break;
		case KEY_LALT:
		case KEY_RALT:
			chkey->_alt = false;
			break;
		}
	} else {
		switch (key) {
		case KEY_LCTRL:
		case KEY_RCTRL:
			chkey->_ctrl = true;
			break;
		case KEY_LSHIFT:
		case KEY_RSHIFT:
			chkey->_shift = true;
			break;
		case KEY_LALT:
		case KEY_RALT:
			chkey->_alt = true;
			break;
		case KEY_CAPSLOCK:
			chkey->_capslock = (chkey->_capslock) ? false : true;
			break;
		case KEY_KP_NUMLOCK:
			chkey->_numlock = (chkey->_numlock) ? false : true;
			break;
		case KEY_SCROLLLOCK:
			chkey->_scrolllock = (chkey->_scrolllock) ? false : true;
			break;
		}
	}
}

/* ChitralekhaGetKeyPress -- Get pressed key information
 * @param code -- code to convert
 */
char ChitralekhaGetKeyPress(int code) {
	int key = 0;
	/* the array only has KEYCODE_TABLE_SIZE entries, not 128 -- axiss */
	if ((unsigned)code < KEYCODE_TABLE_SIZE) {
		key = _kkybrd_scancode_std[code];
	}

	bool ext = ((code & SCANCODE_EXTENDED_MASK) == SCANCODE_EXTENDED_FLAG);
	if (ext) {
		bool released = (code & SCANCODE_RELEASE_BIT) != 0;
		uint8_t keycode = code & SCANCODE_KEY_MASK;
		if (!released && keycode < KEYCODE_TABLE_SIZE) {
			key = _kkybrd_scancode_std[keycode];
		}
	}
	return key;
}
/*
 * ChitralekhaKeyToASCII -- converts a keycode to
 * ascii value
 * @param code -- code to convert
 */
char ChitralekhaKeyToASCII(int code) {
	int key = 0;
	int scode = code;

	/* for now only xt standard mapping is supported */
	if ((unsigned)code < KEYCODE_TABLE_SIZE) { //key press events
		key = _kkybrd_scancode_std[scode];

		/*switch (key) {
		case KEY_LCTRL:
		case KEY_RCTRL:
			return 0;
		case KEY_LSHIFT:
		case KEY_RSHIFT:
			return 0;
		case KEY_LALT:
		case KEY_RALT:
			return 0;
		case KEY_CAPSLOCK:
			return 0;
		case KEY_KP_NUMLOCK:
			return 0;
		case KEY_SCROLLLOCK:
			return 0;
		}*/
		if (isascii(key)) {
			if (chkey->_shift || chkey->_capslock)
				if (key >= 'a' && key <= 'z')
					key -= 32;

			if (chkey->_shift && !chkey->_capslock) {
				if (key >= '0' && key <= '9') {
					switch (key) {
					case '0':
						key = KEY_RIGHTPARENTHESIS;
						break;
					case '1':
						key = KEY_EXCLAMATION;
						break;
					case '2':
						key = KEY_AT;
						break;
					case '3':
						key = KEY_HASH;
						break;
					case '4':
						key = KEY_DOLLAR;
						break;
					case '5':
						key = KEY_PERCENT;
						break;
					case '6':
						key = KEY_CARRET;
						break;
					case '7':
						key = KEY_AMPERSAND;
						break;
					case '8':
						key = KEY_ASTERISK;
						break;
					case '9':
						key = KEY_LEFTPARENTHESIS;
						break;
					}
				} else {
					switch (key) {
					case KEY_COMMA:
						key = KEY_LESS;
						break;
					case KEY_DOT:
						key = KEY_GREATER;
						break;
					case KEY_SLASH:
						key = KEY_QUESTION;
						break;
					case KEY_SEMICOLON:
						key = KEY_COLON;
						break;
					case KEY_QUOTE:
						key = KEY_QUOTEDOUBLE;
						break;
					case KEY_LEFTBRACKET:
						key = KEY_LEFTCURL;
						break;
					case KEY_RIGHTBRACKET:
						key = KEY_RIGHTCURL;
						break;
					case KEY_GRAVE:
						key = KEY_TILDE;
						break;
					case KEY_MINUS:
						key = KEY_UNDERSCORE;
						break;
					case KEY_PLUS:
						key = KEY_EQUAL;
						break;
					case KEY_BACKSLASH:
						key = KEY_BAR;
						break;
					}
				}
			}
			return key;
		}
		return 0;
	}
	if (code > 128)
		return 0;

	return 0;
}

/*
 *ChitralekhaKeyGetCTRL -- return ctrl bit
 */
bool ChitralekhaKeyGetCTRL() {
	return chkey->_ctrl;
}

/*
 * ChitralekhaKeyGetAlt -- return alt bit
 */
bool ChitralekhaKeyGetAlt() {
	return chkey->_alt;
}

/*
 * ChitralekhaKeyGetShift -- returns shift
 * bit
 */
bool ChitralekhaKeyGetShift() {
	return chkey->_shift;
}

/*
 * ChitralekhaKeySetCtrl -- forcefully modify the ctrl bit
 * @param bit -- bit to set 0,1
 */
void ChitralekhaKeySetCtrl(bool bit) {
	chkey->_ctrl = bit;
}

/*
 * ChitralekhaKeySetAlt - forcefully modify the alt bit
 * @param bit -- bit to set 0,1
 */
void ChitralekhaKeySetAlt(bool bit) {
	chkey->_ctrl = bit;
}

/*
 * ChitralekhaKeySetShift -- forcefully modify the shift
 * bit
 * @param bit -- bit to set 0,1
 */
void ChitralekhaKeySetShift(bool bit) {
	chkey->_shift = bit;
}

/**
 * @brief ChitralekhaKeySetCapslock -- set capslock bit forcefully
 * @param bit -- bit to set
 */
void ChitralekhaKeySetCapslock(bool bit) {
	chkey->_capslock = bit;
}

/**
 * @brief ChitralekhaKeyGetCapslock -- get capslock bit 
 */
bool ChitralekhaKeyGetCapslock() {
	return chkey->_capslock;
}