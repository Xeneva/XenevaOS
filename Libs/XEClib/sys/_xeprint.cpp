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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <_xeprint.h>

/*int _ldigits(unsigned long long num, int base, int sign) {
	int digits = 1;
	if (base < 2) {
		return (digits = -1);
	}

	if (sign && ((long long)num < 0)) {
		num = ((long long)num * -1);
		digits += 1;
	}

	while (num >= (unsigned long long)base) {
		digits += 1;
		num /= (unsigned long long)base;
	}

	return (digits);
}

int _digits(unsigned num, int base, int sign) {
	int digits = 1;
	if (base < 2) {
		return (digits = -1);
	}

	if (sign && ((int)num < 0)) {
		num = ((int)num * -1);
		digits += 1;
	}

	while (num >= (unsigned)base) {
		digits += 1;
		num /= (unsigned)base;
	}

	return digits;
}*/

//int _XEPrint(char* output, int len, const char* format, va_list list) {
//	int inCount = 0;
//	int outCount = 0;
//	int formatLen = 0;
//	int zeroPad = 0;
//	int leftJust = 0;
//	int fieldWidth = 0;
//	int isLong = 0;
//	long long intArg = 0;
//	double doubleArg; // Don't initialize unnecessarily
//	int digits = 0;
//
//	// How long is the format string?
//	formatLen = strlen(format);
//	if (formatLen < 0)
//	{
//		errno = formatLen;
//		return (outCount = 0);
//	}
//
//	if (formatLen > outputLen)
//	{
//		errno = ERR_BOUNDS;
//		return (outCount = 0);
//	}
//
//	formatLen = min(formatLen, MAXSTRINGLENGTH);
//
//	// The argument list must already have been initialized using va_start
//
//	// Loop through all of the characters in the format string
//	for (inCount = 0; ((inCount < formatLen) &&
//		(outCount < (outputLen - 1)));)
//	{
//		if (format[inCount] != '%')
//		{
//			output[outCount++] = format[inCount++];
//			continue;
//		}
//		else if ((format[inCount] == '%') && (format[inCount + 1] == '%'))
//		{
//			// Copy it, but skip the next one
//			output[outCount++] = format[inCount];
//			inCount += 2;
//			continue;
//		}
//
//		// Move to the next character
//		inCount += 1;
//
//		// Look for a zero digit, which indicates that any field width argument
//		// is to be zero-padded
//		zeroPad = 0;
//		if (format[inCount] == '0')
//		{
//			zeroPad = 1;
//			inCount += 1;
//		}
//
//		// Look for left-justification (applicable if there's a field-width
//		// specifier to follow
//		leftJust = 0;
//		if (format[inCount] == '-')
//		{
//			leftJust = 1;
//			inCount += 1;
//		}
//
//		// Look for field length indicator
//		fieldWidth = 0;
//		if ((format[inCount] >= '1') && (format[inCount] <= '9'))
//		{
//			fieldWidth = atoi(format + inCount);
//			while ((format[inCount] >= '0') && (format[inCount] <= '9'))
//				inCount++;
//		}
//
//		// If there's an 'll' qualifier for long values, make note of it
//		isLong = 0;
//		if (format[inCount] == 'l')
//		{
//			inCount += 1;
//			if (format[inCount] == 'l')
//			{
//				isLong = 1;
//				inCount += 1;
//			}
//		}
//
//		// We have some format characters.  Get the corresponding argument.
//
//		// If it's a long long value, we need to get two 32-bit values
//		// and combine them.
//		if (isLong)
//		{
//			intArg = (long long)va_arg(list, unsigned);
//			intArg |= (((long long)va_arg(list, unsigned)) << 32);
//		}
//		// If it's a double (or float), we need to gather the argument
//		// differently as well.
//		else if ((format[inCount] == 'e') || (format[inCount] == 'E') ||
//			(format[inCount] == 'f') || (format[inCount] == 'F') ||
//			(format[inCount] == 'g') || (format[inCount] == 'G'))
//		{
//			// Do it in the following switch statement so that we don't need
//			// to initialize the variable prematurely, above, causing an
//			// FPU operation.
//		}
//		else
//		{
//			intArg = va_arg(list, unsigned);
//		}
//
//		// What is it?
//		switch (format[inCount])
//		{
//		case 'd':
//		case 'i':
//		{
//					// This is a decimal integer
//
//					if (fieldWidth)
//					{
//						if (isLong)
//							digits = _ldigits(intArg, 10, 1);
//						else
//							digits = _digits(intArg, 10, 1);
//
//						if (!leftJust)
//						{
//							while (digits++ < fieldWidth)
//								output[outCount++] = (zeroPad ? '0' : ' ');
//						}
//					}
//
//					if (isLong)
//						lltoa(intArg, (output + outCount));
//					else
//						itoa(intArg, (output + outCount));
//
//					outCount = strlen(output);
//
//					if (fieldWidth && leftJust)
//					{
//						while (digits++ < fieldWidth)
//							output[outCount++] = ' ';
//					}
//
//					break;
//		}
//
//		case 'u':
//		{
//					// This is an unsigned decimal integer
//
//					if (fieldWidth)
//					{
//						if (isLong)
//							digits = _ldigits(intArg, 10, 0);
//						else
//							digits = _digits(intArg, 10, 0);
//
//						if (!leftJust)
//						{
//							while (digits++ < fieldWidth)
//								output[outCount++] = (zeroPad ? '0' : ' ');
//						}
//					}
//
//					if (isLong)
//						ulltoa(intArg, (output + outCount));
//					else
//						utoa(intArg, (output + outCount));
//
//					outCount = strlen(output);
//
//					if (fieldWidth && leftJust)
//					{
//						while (digits++ < fieldWidth)
//							output[outCount++] = ' ';
//					}
//
//					break;
//		}
//
//		case 'c':
//		{
//					// A character.
//					output[outCount++] = (unsigned char)intArg;
//					break;
//		}
//
//		case 's':
//		{
//					// This is a string.  Copy the string from the next argument
//					// to the destnation string and increment outCount
//					// appropriately
//
//					if (intArg)
//					{
//						strcpy((output + outCount), (char *)((unsigned)intArg));
//						outCount += strlen((char *)((unsigned)intArg));
//					}
//					else
//					{
//						// Eek.
//						strncpy((output + outCount), "(NULL)", 7);
//						outCount += 6;
//					}
//
//					break;
//		}
//
//		case 'p':
//		{
//					// This is a pointer.  Bit of special stuff for pointer args.
//
//					output[outCount++] = '0';
//					output[outCount++] = 'x';
//					fieldWidth = (2 * sizeof(void *));
//
//					if (isLong)
//						digits = _ldigits(intArg, 16, 0);
//					else
//						digits = _digits(intArg, 16, 0);
//
//					if (!leftJust)
//					{
//						while (digits++ < fieldWidth)
//							output[outCount++] = '0';
//					}
//
//					if (isLong)
//						lltoux(intArg, (output + outCount));
//					else
//						itoux(intArg, (output + outCount));
//
//					outCount = strlen(output);
//
//					if (fieldWidth && leftJust)
//					{
//						while (digits++ < fieldWidth)
//							output[outCount++] = ' ';
//					}
//
//					break;
//		}
//
//		case 'o':
//		{
//					// This is an octal value
//
//					if (fieldWidth)
//					{
//						if (isLong)
//							digits = _ldigits(intArg, 8, 0);
//						else
//							digits = _digits(intArg, 8, 0);
//
//						if (!leftJust)
//						{
//							while (digits++ < fieldWidth)
//								output[outCount++] = (zeroPad ? '0' : ' ');
//						}
//					}
//
//					if (isLong)
//						lltouo(intArg, (output + outCount));
//					else
//						itouo(intArg, (output + outCount));
//
//					outCount = strlen(output);
//
//					if (fieldWidth && leftJust)
//					{
//						while (digits++ < fieldWidth)
//							output[outCount++] = ' ';
//					}
//
//					break;
//		}
//
//		case 'x':
//		case 'X':
//		{
//					// This is a hexadecimal value
//
//					if (fieldWidth)
//					{
//						if (isLong)
//							digits = _ldigits(intArg, 16, 0);
//						else
//							digits = _digits(intArg, 16, 0);
//
//						if (!leftJust)
//						{
//							while (digits++ < fieldWidth)
//								output[outCount++] = (zeroPad ? '0' : ' ');
//						}
//					}
//
//					if (isLong)
//						lltoux(intArg, (output + outCount));
//					else
//						itoux(intArg, (output + outCount));
//
//					outCount = strlen(output);
//
//					if (fieldWidth && leftJust)
//					{
//						while (digits++ < fieldWidth)
//							output[outCount++] = ' ';
//					}
//
//					break;
//		}
//
//		case 'e':
//		case 'E':
//		case 'f':
//		case 'F':
//		case 'g':
//		case 'G':
//		{
//					// This is a double.  Doubles are a special case, and we get
//					// the argument here instead of above.
//					doubleArg = (double)va_arg(list, double);
//
//					// Skip the next word of arguments, since it was part of our
//					// double.
//					list += sizeof(int);
//
//					// Put the characters for the double into the destination
//					// string
//					if (fieldWidth)
//						dtoa(doubleArg, (output + outCount), fieldWidth);
//					else
//						dtoa(doubleArg, (output + outCount), 6);
//
//					outCount = strlen(output);
//
//					break;
//		}
//
//		default:
//		{
//				   // Umm, we don't know what this is.  Just copy the '%' and
//				   // the format character to the output stream
//				   output[outCount++] = format[inCount - 1];
//				   output[outCount++] = format[inCount];
//				   break;
//		}
//		}
//
//		inCount += 1;
//	}
//
//	output[outCount] = '\0';
//
//	// Return the number of characters we wrote
//	return (outCount);
//}