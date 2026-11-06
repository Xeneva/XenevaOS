/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2025, Manas Kamal Choudhury
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

#include "xnout.h"
#include "xnldr.h"
#include "clib.h"
#include "video.h"
#include <cstdarg>

/*
 * XEClearScreen -- clears the screen 
 */
void XEClearScreen() {
	gSystemTable->ConOut->ClearScreen(gSystemTable->ConOut);
}

/*
 * XESetCursor -- Set the cursor on or off
 * @param on -- boolean value
 */
EFI_STATUS XESetCursor(const unsigned char on) {
	return gSystemTable->ConOut->EnableCursor(gSystemTable->ConOut, on);
}

/*
 * XEGetCurrentCursorPos -- returns the current
 * cursor pos to given variable
 * @param Col -- Pointer to where Column value should be stored
 * @param Row -- Pointer to where Row value should be stored
 */
void XEGetCurrentCursorPos(int* Col, int* Row) {
	*Col = gSystemTable->ConOut->Mode->CursorColumn;
	*Row = gSystemTable->ConOut->Mode->CursorRow;
}

/*
 * XESetCurrentCursorPos -- Set the current cursor pos
 * @param Col -- Column value
 * @param Row -- Row value
 */
EFI_STATUS XESetCurrentCursorPos(const int Col, const int Row) {
	return gSystemTable->ConOut->SetCursorPosition(gSystemTable->ConOut, Col, Row);
}

/*
 * XESetTextAttribute -- Set text and background color
 * @param Back -- Background color
 * @param Fore -- Foreground color
 */
EFI_STATUS XESetTextAttribute(const int Back, const int Fore) {
	return gSystemTable->ConOut->SetAttribute(gSystemTable->ConOut, Fore | Back);
}


void XEPutChar(const int ch) {
	unsigned short text[2];
	text[0] = (unsigned short)ch;
	text[1] = 0;
	gSystemTable->ConOut->OutputString(gSystemTable->ConOut, reinterpret_cast<CHAR16*>(text));
}

/*
 * XEPrintf -- print function that will
 * show output to the screen
 * @param fmt -- format to print
 */
int XEPrintf(const char16_t* fmt, ...) {
	const char16_t* fmtPtr = fmt;

	va_list vl;
	va_start(vl, fmt);

	CHAR16 out[1024];
	int o = 0;
	char16_t c;
	int sign, width, precision, lmodifier;
	unsigned char ljust, alt, lzeroes;

	while ((c = *fmtPtr++)) {
		if (c != u'%' || *fmtPtr == u'%') {
			out[o++] = c;
			fmtPtr += (c == '%');
			continue;
		}
		if ((c = (uint8_t)*fmtPtr++) == '\0')
			return -1;
		ljust = alt = lzeroes = FALSE;
		sign = 0;
		for (;;) {
			if (c == u'-') {
				ljust = TRUE;
				lzeroes = FALSE;
			}
			else if (c == u'+')
				sign = u'+';
			else if (c == u' ') {
				if (!sign)
					sign = u' ';
			}
			else if (c == u'#')
				alt = TRUE;
			else if (c == u'0') {
				if (!ljust)
					lzeroes = TRUE;
			}
			else
				break;

			if ((c = (uint8_t)*fmtPtr++) == u'\0')
				return -1;
		}

		width = -1;
		if (is_digit(c)) {
			width = 0;
			while (is_digit(c)) {
				width = width * 10 + (c - u'0');
				if ((c = (uint8_t)*fmtPtr++) == u'\0')
					return -1;
			}
		}
		else if (c == u'*') {
			width = va_arg(vl, int);
			if (width < 0) {
				ljust = TRUE;
				lzeroes = FALSE;
				width = -width;
			}
			if ((c = *fmtPtr++) == u'\0')
				return -1;
		}

		if (c == u'[') {
			if (o > 0) {
				out[o] = 0;
				gSystemTable->ConOut->OutputString(gSystemTable->ConOut, reinterpret_cast<CHAR16*>(out));
				o = 0;
			}
			/**uint32 attr = *(unsigned long*)vl;
			vl += sizeof(unsigned long);**/
			XESetTextAttribute(0x000000F0, 0x0000000F); //attr &
			continue;
		}
		//restore the default text attribute
		if (c == u']') {
			if (o > 0) {
				out[o] = 0;
				gSystemTable->ConOut->OutputString(gSystemTable->ConOut, reinterpret_cast<CHAR16*>(out));
				o = 0;
			}
			XESetTextAttribute(EFI_BACKGROUND_BLACK, EFI_LIGHTGRAY);
			continue;
		}

		precision = -1;
		if (c == u'.') {
			if ((c = (uint8_t)*fmtPtr++) == u'\0')
				return -1;
			precision = 0;
			lzeroes = FALSE;
			if (is_digit(c)) {
				while (is_digit(c)) {
					precision = precision * 10 + (c - u'0');
					if ((c = (uint8_t)*fmtPtr++) == u'\0')
						return -1;
				}
			}
			else if (c == u'*') {
				precision = va_arg(vl, int);
				if ((c = *fmtPtr++) == u'\0')
					return -1;
			}
		}

		lmodifier = 0;
		if (c == u'h') {
			if (*fmtPtr == u'h') {
				fmtPtr++;
				lmodifier = u'H';
			}
			else
				lmodifier = c;
		}
		else if (wstrchr((wchar_t*)u"jzt", c))
			lmodifier = c;
		if (lmodifier)
			if ((c = (uint8_t)*fmtPtr++) == u'\0')
				return -1;

		if (c == u'i')
			c = u'd';
		if (!wstrchr((wchar_t*)u"douxXcsp", c))
			return -1;

		if (c == u'c') {
			char16_t ch = va_arg(vl, int);
			if (!ljust)
				while (width > 1) {
					out[o++] = u' ';
					width--;
				}
			out[o++] = ch;

			if (ljust)
				while (width > 1) {
					out[o++] = u' ';
					width--;
				}
			continue;
		}
		else if (c == u's') {
			int len, i;
			wchar_t* s = va_arg(vl, wchar_t*);

			if (precision < 0)
				len = wstrlen(s);
			else {
				len = 0;
				while (len < precision)
					if (s[len]) len++;
					else break;
			}

			if (!ljust) {
				while (width > len) {
					out[o++] = u' ';
					width--;
				}
			}

			i = len;
			while (i--)
				out[o++] = *s++;

			if (ljust) {
				while (width > len) {
					out[o++] = u' ';
					width--;
				}
			}
			continue;
		}
		else {
			unsigned v = va_arg(vl, unsigned);
			char s[11];
			char* p = s + sizeof(s);
			unsigned base = (c == u'p') ? 16 : 10;
			char* digits = (char*)"0123456789abcdef";
			char* hexpfx = (char*)NULL;
			int dcnt;
			int len;

			if (c == u'o')
				base = 8;
			else if (to_upper(c) == u'X') {
				base = 16;
				if (c == 'X')
					digits = (char*)"0123456789ABCDEF";
				if (alt && v)
					hexpfx = (char*)((c == u'X') ? "0X" : "0x");
			}

			if (c != u'd') {
				if (lmodifier == u'H')
					v = (uint8_t)v;
				else if ((lmodifier = u'h'))
					v = (unsigned short)v;
				sign = 0;
			}
			else {
				if ((lmodifier = u'H'))
					v = (signed char)v;
				else if ((lmodifier == u'h'))
					v = (short)v;
				if ((int)v < 0) {
					v = -v;
					sign = u'-';
				}
			}

			auto tmp = v;
			do {
				*--p = digits[tmp % base];
				tmp /= base;
			} while (tmp);
			dcnt = s + sizeof(s) - p;

			if (precision < 0)
				precision = 1;
			else if ((v == 0) && (precision == 0))
				dcnt = 0;

			if (alt && (c == u'o'))
				if (((v == 0) && (precision == 0)) || (v && (precision <= dcnt)))
					precision = dcnt + 1;

			if (precision < dcnt)
				precision = dcnt;


			len = (sign != 0) + (hexpfx != NULL) * 2 + precision;

			if (!ljust && !lzeroes)
				while (width > len) {
					out[o++] = u' ';
					width--;
				}

			if (sign)
				out[o++] = sign;
			else if (hexpfx) {
				out[o++] = hexpfx[0];
				out[o++] = hexpfx[1];
			}

			if (!ljust && lzeroes)
				while (width > len) {
					out[o++] = u'0';
					width--;
				}

			while (precision-- > dcnt)
				out[o++] = u'0';

			while (dcnt--)
				out[o++] = *p++;

			if (ljust)
				while (width > len) {
					out[o++] = u' ';
					width--;
				}

			continue;
		}
	}

	va_end(vl);

	out[o++] = 0;
	gSystemTable->ConOut->OutputString(gSystemTable->ConOut, reinterpret_cast<CHAR16*>(out));
	return 0;
}

/*
 * XEGuiPrint -- print formated text using graphics
 * @param format -- formated string
 */
void XEGuiPrint(const char* format, ...) {
	va_list args;
	va_start(args, format);
	while (*format != '\0') {
		if (*format == '%') {
			++format;
			if (*format == 'd') {
				size_t width = 0;
				if (format[1] == '.'){
					for (size_t i = 2; format[i] >= '0' && format[i] <= '9'; ++i)
					{
						width *= 10;
						width += format[i] - '0';
					}
				}
				size_t i = va_arg(args, size_t);
				char buffer[sizeof(size_t) * 8 + 1];
				sztoa(i, buffer, 10);
				size_t len = strlen(buffer);
				while (len++ < width)
					XEGraphicsPuts("0");
				XEGraphicsPuts(buffer);
			}
			else if (*format == 'c') {

				char16_t c = va_arg(args, int);
				XEGraphicsPutC(c);
			}
			else if (*format == 'x') {
				size_t x = va_arg(args, size_t);
				char buffer[sizeof(size_t) * 8 + 1];
				sztoa(x, buffer, 16);
				XEGraphicsPuts(buffer);
			}
			else if (*format == 's') {
				char* x = va_arg(args, char*);
				XEGraphicsPuts(x);
			}
			else if (*format == 'S') {
				char* x = va_arg(args, char*);
				XEGraphicsPuts(x);
			}
			else if (*format == '%') {
				XEGraphicsPuts(".");
			}
			else {
				char buf[3];
				buf[0] = '%'; buf[1] = *format; buf[2] = '\0';
				XEGraphicsPuts(buf);
			}
		}
		else
		{
			char buf[2];
			buf[0] = *format; buf[1] = '\0';
			XEGraphicsPuts(buf);
		}
		++format;
	}
	va_end(args);
}

