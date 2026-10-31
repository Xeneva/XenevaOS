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

#ifndef __ESCCODE_H__
#define __ESCCODE_H__

#define ASCII_ESC_CHAR	  '^' //octal: \033, hex -> \x1B, or '^[ 'or 27 in decimal
#define ASCII_ESC_OCTAL	  '\033'
#define ASCII_ESC_HEX	  '\x1B'
#define ASCII_ESC_DECIMAL 27

#define SEQUENCE_DCS 'P'
#define SEQUENCE_CSI '[' //control
#define SEQUENCE_ST	 '\\'
#define SEQUENCE_OSC ']'

#define CSI_CURSOR_UP		'A'
#define CSI_CURSOR_DOWN		'B'
#define CSI_CURSOR_FORWARD	'C'
#define CSI_CURSOR_BACKWARD 'D'
#define CSI_CURSOR_HOME		'H'
#define CSI_CNL				'E' //cursor next line
#define CSI_CPL				'F' //Cursor previous line
#define CSI_CHA				'G' //Cursor horizontal absolute
#define CSI_CUP				'H' //Cursor Position
#define CSI_ED				'J' //Erase in Line
#define CSI_EL				'K' //Erase in Line
#define CSI_IL				'L' //Insert n Blank Lines
#define CSI_DL				'M' //Delete n Lines
#define CSI_SCROLL_UP		'S' //Scroll Up
#define CSI_SCROLL_DOWN		'T' //Scroll Down

#define CSI_SET_GRAPHICS_RENDITION 'm'

/* color attributes */
#define CSI_SET_FG_BLACK   30
#define CSI_SET_FG_RED	   31
#define CSI_SET_FG_GREEN   32
#define CSI_SET_FG_BROWN   33
#define CSI_SET_FG_BLUE	   34
#define CSI_SET_FG_MAGENTA 35
#define CSI_SET_FG_CYAN	   36
#define CSI_SET_FG_WHITE   37
#define CSI_SET_FG_DEFAULT 39

#define CSI_SET_BG_BLACK   40
#define CSI_SET_BG_RED	   41
#define CSI_SET_BG_GREEN   42
#define CSI_SET_BG_BROWN   43
#define CSI_SET_BG_BLUE	   44
#define CSI_SET_BG_MAGENTA 45
#define CSI_SET_BG_CYAN	   46
#define CSI_SET_BG_WHITE   47
#define CSI_SET_BG_DEFAULT 49

#define CSI_ERASE_TEXT_NONLINE 'J'
#define CSI_ERASE_TEXT_LINE	   'K'

#define CSI_SET_MODE	   'h'
#define CSI_RESET_MODE	   'l'
#define CSI_SAVE_CURSOR	   's'
#define CSI_RESTORE_CURSOR 'u'
#endif