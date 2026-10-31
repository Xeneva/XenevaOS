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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int _xeinput(const char* input, const char* format, va_list list) {
	int matchItems = 0;
	int inputLen = 0;
	int formatLen = 0;
	int inputCount = 0;
	int formatCount = 0;
	unsigned long long* argument = NULL;

	int isLong = 0;
	int count;

	// How long are the input and format strings?
	inputLen = strlen(input);
	if (inputLen < 0) {
		return (matchItems = 0);
	}

	inputLen = min(inputLen, MAX_STRING_LENGTH);
	formatLen = strlen(format);
	if (formatLen < 0) {
		return (matchItems = 0);
	}

	formatLen = min(formatLen, MAX_STRING_LENGTH);

	for (formatCount = 0; formatCount < formatLen;) {
		if ((formatCount < formatLen) && isspace(format[formatCount])) {
			while ((formatCount < formatLen) && isspace(format[formatCount]))
				formatCount += 1;

			while ((inputCount < inputLen) && isspace(input[inputCount]))
				inputCount += 1;
			continue;
		}

		if ((format[formatCount] == '%') && (format[formatCount + 1] == '%')) {
			if (input[inputCount] != '%') {
				return (matchItems);
			}

			formatCount += 2;
			inputCount += 1;
			continue;
		}

		if (format[formatCount] != '%') {
			if (format[formatCount] != input[inputCount]) {
				return (matchItems);
			}

			formatCount += 1;
			inputCount += 1;
			continue;
		}

		// Move to the next character
		formatCount += 1;

		if (format[formatCount] == '0') {
			//zeroPad = 1;
			formatCount += 1;
		}

		if (format[formatCount] == '-') {
			//leftJust = 1;
			formatCount += 1;
		}
		//else
		//	leftJust = 0;

		// Look for field length indicator
		if ((format[formatCount] >= '1') && (format[formatCount] <= '9')) {
			//fieldWidth = atoi(format + formatCount);
			while ((format[formatCount] >= '0') && (format[formatCount] <= '9'))
				formatCount++;
		}
		//else
		//	fieldWidth = 0;

		// If there's a 'll' qualifier for long values, make note of it
		if (format[formatCount] == 'l') {
			formatCount += 1;
			if (format[formatCount] == 'l') {
				isLong = 1;
				formatCount += 1;
			}
		} else
			isLong = 0;

		// We have some format characters.  Get the corresponding argument.
#ifdef ARCH_X64
		argument = (unsigned long long*)va_arg(list, unsigned);
#elif ARCH_ARM64
		argument = (unsigned long long*)va_arg(list, uint64_t);
#endif

		// What is it?
		switch (format[formatCount]) {
		case 'd':
		case 'i':
			// This is a decimal integer.  Read the characters for the
			// integer from the input string
			if (isLong) {
				*argument = atoll(input + inputCount);
				inputCount += _ldigits(*argument, 10, 1);
			} else {
				*((int*)argument) = atoi(input + inputCount);
				inputCount += _digits(*argument, 10, 1);
			}
			break;

		case 'u':
			// This is an unsigned decimal integer.  Put the characters
			// for the integer into the destination string
			if (isLong) {
				*argument = atoull(input + inputCount);
				inputCount += _ldigits(*argument, 10, 0);
			} else {
				*((unsigned*)argument) = atou(input + inputCount);
				inputCount += _digits(*argument, 10, 0);
			}
			break;

		case 'c':
			// A character.
			*((char*)argument) = input[inputCount++];
			break;

		case 's':
			// This is a string.  Copy until we meet a whitespace
			// character
			for (count = 0; ((inputCount < inputLen) && !isspace(input[inputCount])); count++) {
				((char*)argument)[count] = input[inputCount++];
			}
			((char*)argument)[count] = '\0';
			break;

		case 'o':
			// This is an octal number.  Put the characters for the number
			// into the destination string
			if (isLong) {
				*argument = atooll(input + inputCount);
				inputCount += _ldigits(*argument, 8, 1);
			} else {
				*((int*)argument) = atoo(input + inputCount);
				inputCount += _digits(*argument, 8, 1);
			}
			break;

		case 'p':
		case 'x':
		case 'X':
			if (format[formatCount] == 'p') {
				// Bit of special stuff for pointer args
				if (!strncmp((input + inputCount), "0x", 2))
					inputCount += 2;
			}
			if (isLong) {
				*argument = xtoll(input + inputCount);
				inputCount += _ldigits(*argument, 16, 1);
			} else {
				*((int*)argument) = xtoi(input + inputCount);
				inputCount += _digits(*argument, 16, 1);
			}
			break;

		default:
			// Umm, we don't know what this is.  Fail.
			return (matchItems);
			break;
		}

		matchItems += 1;
		formatCount += 1;
	}

	// Return the number of items we matched
	return (matchItems);
}