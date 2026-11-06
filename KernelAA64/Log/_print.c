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
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <_null.h>
#include <Log/_print.h>
#include <stdio.h>

#define MAX_STRING_LENGTH 4095

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

static double floor(double d) {
	int i = (int)d;
	return ((float)i);
}

static double pow(double x, double y) {
	int count;

	if ((x < 0) && (floor(y) != y))
		return 0;

	if (!y)
		x = 1;
	else
		for (count = 1; count < y; count++)
			x *= x;

	return x;
}
static int _ldigits(unsigned long long num, int base, int sign) {
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

static int _digits(unsigned num, int base, int sign) {
	int digits = 1;
	if (base < 2)
		return (digits = -1);

	if (sign && ((int)num < 0)) {
		num = ((int)num * -1);
		digits += 1;
	}

	while (num >= (unsigned)base) {
		digits += 1;
		num /= (unsigned)base;
	}

	return digits;
}

static unsigned long long _str2num(const char* string, unsigned base, int sign, int* consumed) {
	unsigned long long result = 0;
	int64_t length = 0;
	int64_t negative = 0;
	int64_t count = 0;

	if (!string) {
		return 0;
	}

	length = strlen(string);

	while ((count < length) && isspace(string[count]))
		count += 1;

	if (count >= length)
		goto out;

	if ((string[count] == '+') || (string[count] == '-')) {
		if (sign && (string[count] == '-'))
			negative = 1;
		count += 1;
	}

	if (count >= length) {
		goto out;
	}

	if ((count < (length - 1)) && (!base || (base == 16))) {
		if ((string[count] == '0') && (string[count + 1] == 'x')) {
			base = 16;
			count += 2;
		}
	}

	if (count >= length) {
		goto out;
	}

	if (!base) {
		if (string[count] == '0') {
			base = 8;
			count += 1;
		} else {
			base = 10;
		}
	}

	if (count >= length)
		goto out;

	while (count < length) {
		switch (base) {
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
			if (!isdigit(string[count]) || ((string[count] - '0') >= (int)base)) {
				goto out;
			}
			result *= base;
			result += (string[count] - '0');
			break;

		case 16:
			if (!isxdigit(string[count]))
				goto out;
			result *= base;
			if ((string[count] >= '0') && (string[count] <= '9'))
				result += (string[count] - '0');
			else if ((string[count] >= 'a') && (string[count] <= 'f'))
				result += ((string[count] - 'a') + 10);
			else
				result += ((string[count] - 'A') + 10);
			break;

		default:
			goto out;
		}
		count += 1;
	}

out:
	if (negative)
		result = ((long long)result * -1);

	if (consumed)
		*consumed = count;

	return result;
}

static void _lnum2str(unsigned long long num, char* string, int base, int sign) {
	int digits = _ldigits(num, base, sign);
	int charCount = 0;
	unsigned long long place = 1;
	unsigned long long rem = 0;
	int count;

	if (!string) {
		return;
	}

	// Negative?
	if (sign && ((long long)num < 0)) {
		string[charCount++] = '-';
		num = ((long long)num * -1);
		digits -= 1;
	}

	for (count = 0; count < (digits - 1); count++)
		place *= (unsigned long long)base;

	while (place) {
		rem = (num % place);
		num = (num / place);

		if (num < 10)
			string[charCount++] = ('0' + num);
		else
			string[charCount++] = ('a' + (num - 10));
		num = rem;
		place /= (unsigned long long)base;
	}

	string[charCount] = '\0';

	// Done
	return;
}

static void _dbl2str(double num, char* string, int roundPlaces) {
	int charCount = 0;
	unsigned long long* u = NULL;
	int sign = 0;
	long long exponent = 0;
	unsigned long long intPart = 0;
	unsigned long long fractPart = 0;
	unsigned long long place = 0;
	unsigned long long outputFraction = 0;
	unsigned long long rem = 0;
	unsigned long long count;

	if (!string) {
		return;
	}

	string[0] = '\0';

	u = (unsigned long long*)&num;
	sign = (int)(*u >> 63);
	exponent = (((*u & (0x7FFULL << 52)) >> 52) - 1023);
	intPart = 1;
	fractPart = ((*u & 0x000FFFFFFFFFFFFFULL) << 12);

	// Output the sign, if any
	if (sign)
		string[charCount++] = '-';

	// Special case exponents
	if (exponent == 0x7FF) {
		strcat((string + charCount), "Infinity");
		return;
	}

	while (exponent) {
		if (exponent > 0) {
			intPart <<= 1;
			if (fractPart & (0x1ULL << 63))
				intPart |= 1;
			fractPart <<= 1;
			exponent -= 1;
		} else {
			fractPart >>= 1;
			if (intPart & 0x1ULL)
				fractPart |= (0x1ULL << 63);
			intPart >>= 1;
			exponent += 1;
		}
	}

	// Output the whole number part
	_lnum2str(intPart, (string + charCount), 10, 0);
	charCount = strlen(string);

	string[charCount++] = '.';

	// Calculate the fraction part
	place = 10000000000000000000ULL;
	for (count = 2; fractPart; count *= 2) {
		if (!count)
			break;

		if (fractPart & (0x1ULL << 63))
			outputFraction += (place / count);

		fractPart <<= 1;
	}

	// Output the fraction part
	place = 1000000000000000000ULL;
	while (place) {
		rem = (outputFraction % place);
		outputFraction = (outputFraction / place);

		if (roundPlaces) {
			string[charCount++] = ('0' + outputFraction);
			roundPlaces -= 1;
		} else {
			if ((string[charCount - 1] < '9') && (outputFraction > 4))
				string[charCount - 1] += 1;
			break;
		}

		outputFraction = rem;
		place /= 10;
	}

	string[charCount] = '\0';
	return;
}

static void _num2str(unsigned num, char* string, int base, int sign) {
	int digits = _digits(num, base, sign);
	int charCount = 0;
	unsigned place = 1;
	unsigned rem = 0;
	int count;

	if (!string)
		return;

	// Negative?
	if (sign && ((int)num < 0)) {
		string[charCount++] = '-';
		num = ((int)num * -1);
		digits -= 1;
	}

	for (count = 0; count < (digits - 1); count++)
		place *= (unsigned)base;

	while (place) {
		rem = (num % place);
		num = (num / place);

		if (num < 10)
			string[charCount++] = ('0' + num);
		else
			string[charCount++] = ('a' + (num - 10));
		num = rem;
		place /= (unsigned)base;
	}

	string[charCount] = '\0';

	//errno = 0;
}

static void _flt2str(float num, char* string, int roundPlaces) {
	int charCount = 0;
	unsigned* u = NULL;
	int sign = 0;
	int exponent = 0;
	unsigned intPart = 0;
	unsigned fractPart = 0;
	unsigned place = 0;
	unsigned outputFraction = 0;
	unsigned rem = 0;
	unsigned count;

	if (!string) {
		return;
	}

	string[0] = '\0';

	u = (unsigned*)&num;
	sign = (*u >> 31);
	exponent = (((*u & 0x7F800000) >> 23) - 127);
	intPart = 1;
	fractPart = ((*u & 0x007FFFFF) << 9);

	// Output the sign, if any
	if (sign)
		string[charCount++] = '-';

	// Special case exponents
	if (exponent == 0xFF) {
		strcat((string + charCount), "Infinity");
		return;
	}

	while (exponent) {
		if (exponent > 0) {
			intPart <<= 1;
			if (fractPart & (0x1 << 31))
				intPart |= 1;
			fractPart <<= 1;
			exponent -= 1;
		} else {
			fractPart >>= 1;
			if (intPart & 0x01)
				fractPart |= (0x1 << 31);
			intPart >>= 1;
			exponent += 1;
		}
	}

	// Output the whole number part
	_num2str(intPart, (string + charCount), 10, 0);
	charCount = strlen(string);

	string[charCount++] = '.';

	// Calculate the fraction part
	place = 1000000000;
	for (count = 2; fractPart; count *= 2) {
		if (!count)
			break;

		if (fractPart & (0x1 << 31))
			outputFraction += (place / count);

		fractPart <<= 1;
	}

	// Output the fraction part
	place = 100000000;
	while (place) {
		rem = (outputFraction % place);
		outputFraction = (outputFraction / place);

		if (roundPlaces) {
			string[charCount++] = ('0' + outputFraction);
			roundPlaces -= 1;
		} else {
			if ((string[charCount - 1] < '9') && (outputFraction > 4))
				string[charCount - 1] += 1;
			break;
		}

		outputFraction = rem;
		place /= 10;
	}

	string[charCount] = '\0';
	return;
}

static double strtod(const char* nptr, char** endptr) {
	int sign = 1;
	if (*nptr == '-') {
		sign = -1;
		nptr++;
	}

	long long decimal_part = 0;

	while (*nptr && *nptr != '.') {
		if (*nptr < '0' || *nptr > '9') {
			break;
		}
		decimal_part *= 10LL;
		decimal_part += (long long)(*nptr - '0');
		nptr++;
	}

	double sub_part = 0;
	double multiplier = 0.1;

	if (*nptr == '.') {
		nptr++;

		while (*nptr) {
			if (*nptr < '0' || *nptr > '9') {
				break;
			}

			sub_part += multiplier * (*nptr - '0');
			multiplier *= 0.1;
			nptr++;
		}
	}

	double expn = (double)sign;

	if (*nptr == 'e' || *nptr == 'E') {
		nptr++;

		int exponent_sign = 1;

		if (*nptr == '+') {
			nptr++;
		} else if (*nptr == '-') {
			exponent_sign = -1;
			nptr++;
		}

		int64_t exponent = 0;

		while (*nptr) {
			if (*nptr < '0' || *nptr > '9') {
				break;
			}
			exponent *= 10;
			exponent += (*nptr - '0');
			nptr++;
		}

		expn = pow(10.0, (double)(exponent * exponent_sign));
	}

	if (endptr) {
		*endptr = (char*)nptr;
	}
	double result = ((double)decimal_part + sub_part) * expn;
	return result;
}

static float strtof(const char* nptr, char** endptr) {
	return strtod(nptr, endptr);
}

#define atou(string)			 ((unsigned)_str2num(string, 10, 0, NULL))
#define atoull(string)			 _str2num(string, 10, 0, NULL)
#define atoo(string)			 ((unsigned)_str2num(string, 8, 0, NULL))
#define atooll(string)			 _str2num(string, 8, 0, NULL)
#define dtoa(num, string, round) _dbl2str(num, string, round)
#define ftoa(num, string)		 _flt2str(num, string, round)
#define itoa(num, string)		 _num2str(num, string, 10, 1)
#define itouo(num, string)		 _num2str(num, string, 10, 1)
#define itoux(num, string)		 _num2str(num, string, 16, 0);
#define itox(num, string)		 _num2str(num, string, 16, 1)
#define lltoa(num, string)		 _lnum2str(num, string, 10, 1)
#define lltouo(num, string)		 _lnum2str(num, string, 8, 0)
#define lltoux(num, string)		 _lnum2str(num, string, 16, 0)
#define lltox(num, string)		 _lnum2str(num, string, 16, 1)
#define xtoi(string)			 ((int)_str2num(string, 16, 1, NULL))
#define xtoll(string)			 ((long long)_str2num(string, 16, 1, NULL))
#define ulltoa(num, string)		 _lnum2str(num, string, 10, 0)
#define utoa(num, string)		 _num2str(num, string, 10, 0)

/**
 * @brief _xeprint -- copied from XEClib implementation
 * @param output -- pointer to memory buffer, where to put formatted
 * string
 * @param outputLen -- output len
 * @param format -- formatted string
 * @param list -- pointer to variable argument list
 */
int _xeprint(char* output, int outputlen, const char* format, va_list list) {
	int inCount = 0;
	int64_t outCount = 0;
	int formatLen = 0;
	int zeroPad = 0;
	int leftJust = 0;
	int fieldWidth = 0;
	int isLong = 0;
	long long intArg = 0;
	double doubleArg;
	int digits = 0;

	formatLen = strlen(format);
	if (formatLen < 0) {
		//errno = formatLen;
		return (outCount = 0);
	}

	if (formatLen > outputlen)
		return (outCount = 0);

	formatLen = min(formatLen, MAX_STRING_LENGTH);

	for (inCount = 0; ((inCount < formatLen) && (outCount < outputlen));) {
		if (format[inCount] != '%') {
			output[outCount++] = format[inCount++];
			continue;
		} else if ((format[inCount] == '%') && (format[inCount + 1] == '%')) {
			output[outCount++] = format[inCount];
			inCount += 2;
			continue;
		}

		inCount += 1;
		zeroPad = 0;
		if (format[inCount] == '0') {
			zeroPad = 1;
			inCount += 1;
		}

		leftJust = 0;
		if (format[inCount] == '-') {
			leftJust = 1;
			inCount += 1;
		}

		fieldWidth = 0;
		if ((format[inCount] >= '1') && (format[inCount] <= '9')) {
			fieldWidth = atoi(format + inCount);
			while ((format[inCount] >= '0') && (format[inCount] <= '9'))
				inCount++;
		}

		isLong = 0;
		if (format[inCount] == 'l') {
			inCount += 1;
			if (format[inCount] == 'l') {
				isLong = 1;
				inCount += 1;
			}
		}

		if (isLong) {
			intArg = (long long)va_arg(list, unsigned);
			intArg |= (((long long)va_arg(list, unsigned)) << 32);
		} else if ((format[inCount] == 'e') || (format[inCount] == 'E') ||
				   (format[inCount] == 'f') || (format[inCount] == 'F') ||
				   (format[inCount] == 'g') || (format[inCount] == 'G')) {
		} else {
			intArg = va_arg(list, unsigned long long);
		}
		switch (format[inCount]) {
		case 'd':
		case 'i': {
			if (fieldWidth) {
				if (isLong)
					digits = _ldigits(intArg, 10, 1);
				else
					digits = _digits(intArg, 10, 1);

				if (!leftJust) {
					while (digits++ < fieldWidth)
						output[outCount++] = (zeroPad ? '0' : ' ');
				}
			}
			if (isLong)
				lltoa(intArg, (output + outCount));
			else
				itoa(intArg, (output + outCount));

			outCount = strlen(output);

			if (fieldWidth && leftJust) {
				while (digits++ < fieldWidth) {
					output[outCount++] = ' ';
				}
			}
			break;
		}

		case 'u': {
			if (fieldWidth) {
				if (isLong)
					digits = _ldigits(intArg, 10, 0);
				else
					digits = _digits(intArg, 10, 0);

				if (!leftJust) {
					while (digits++ < fieldWidth)
						output[outCount++] = (zeroPad ? '0' : ' ');
				}
			}

			if (isLong)
				ulltoa(intArg, (output + outCount));
			else
				utoa(intArg, (output + outCount));

			outCount = strlen(output);

			if (fieldWidth && leftJust) {
				while (digits++ < fieldWidth)
					output[outCount++] = ' ';
			}
			break;
		}
		case 'c': {
			output[outCount++] = (unsigned char)intArg;
			break;
		}
		case 's': {
			const char* str = intArg ? (const char*)(uint64_t)intArg : "(NULL)";
			int slen = strlen(str);
			int pad = fieldWidth;

			if (fieldWidth && !leftJust) {
				while (pad-- > 0)
					output[outCount++] = ' ';
			}

			strcpy(output + outCount, str);
			outCount += slen;

			if (fieldWidth && leftJust) {
				while (pad-- > 0)
					output[outCount++] = ' ';
			}
			break;
		}
		case 'p': {
			output[outCount++] = '0';
			output[outCount++] = 'x';
			fieldWidth = (2 * sizeof(void*));

			if (isLong)
				digits = _ldigits(intArg, 16, 0);
			else
				digits = _digits(intArg, 16, 0);

			if (!leftJust) {
				while (digits++ < fieldWidth)
					output[outCount++] = '0';
			}

			if (isLong)
				lltoux(intArg, (output + outCount));
			else
				itoux(intArg, (output + outCount));

			outCount = strlen(output);

			if (fieldWidth && leftJust) {
				while (digits++ < fieldWidth)
					output[outCount++] = ' ';
			}
			break;
		}
		case 'o': {
			if (fieldWidth) {
				if (isLong)
					digits = _ldigits(intArg, 8, 0);
				else
					digits = _digits(intArg, 8, 0);

				if (!leftJust) {
					while (digits++ < fieldWidth)
						output[outCount++] = (zeroPad ? '0' : ' ');
				}
			}

			if (isLong)
				lltouo(intArg, (output + outCount));
			else
				itouo(intArg, (output + outCount));

			outCount = strlen(output);

			if (fieldWidth && leftJust) {
				while (digits++ < fieldWidth)
					output[outCount++] = ' ';
			}
			break;
		}
		case 'x':
		case 'X': {
			if (fieldWidth) {
				if (isLong)
					digits = _ldigits(intArg, 16, 0);
				else
					digits = _digits(intArg, 16, 0);

				if (!leftJust) {
					while (digits++ < fieldWidth)
						output[outCount++] = (zeroPad ? '0' : ' ');
				}
			}

			if (isLong)
				lltoux(intArg, (output + outCount));
			else
				itoux(intArg, (output + outCount));

			outCount = strlen(output);

			if (fieldWidth && leftJust) {
				while (digits++ < fieldWidth)
					output[outCount++] = ' ';
			}
			break;
		}

		case 'e':
		case 'E':
		case 'f':
		case 'F':
		case 'g':
		case 'G': {
			doubleArg = (double)va_arg(list, double);
			list += sizeof(int);

			if (fieldWidth)
				dtoa(doubleArg, (output + outCount), fieldWidth);
			else
				dtoa(doubleArg, (output + outCount), 6);

			outCount = strlen(output);
			break;
		}
		default: {
			output[outCount++] = format[inCount - 1];
			output[outCount++] = format[inCount];
			break;
		}
		}
		inCount += 1;
	}
	output[outCount] = '\0';
	return (outCount);
}

int _vsnprintf(char* output, size_t sz, const char* format, va_list ap) {
	int len = 0;
	sz = min(sz, MAX_STRING_LENGTH);

	memset(output, 0, sz);

	len = _xeprint(output, sz, format, ap);
	return len;
}

extern void store_x2_x7(uint8_t* buffer);
extern void store_x3_x7(uint8_t* buffer);

int _sprintf(char* output, const char* format, ...) {
	va_list list;
	uint8_t buffer[192];
	store_x2_x7(buffer);
	list = (va_list)buffer;
	int len = 0;
	len = _xeprint(output, MAX_STRING_LENGTH, format, list);
	va_end(list);
	return len;
}

int _snprintf(char* output, size_t sz, const char* format, ...) {
	va_list list;
	uint8_t buffer[192];
	store_x3_x7(buffer);
	list = (va_list)buffer;
	int len = 0;
	memset(output, 0, sz);
	len = _xeprint(output, MAX_STRING_LENGTH, format, list);
	va_end(list);
	return len;
}
