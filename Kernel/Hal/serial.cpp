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

#include <Hal/serial.h>
#include <aucon.h>
#include <Hal/hal.h>
#include <Hal/x86_64_hal.h>
#include <stdio.h>

/*
 * this kernel module is only used for debug purpose
 */

#define SERIAL_PORT 0x3f8

void SerialInterruptHandler(size_t v, void* p) {
	AuInterruptEnd(4);
}

/*
 * AuInitialiseSerial -- initialise the serial
 * port
 */
void AuInitialiseSerial() {
	x64_outportb(SERIAL_PORT + 1, 0x00);
	x64_outportb((SERIAL_PORT + 3), 0x80);
	x64_outportb((SERIAL_PORT + 0), 0x03);
	x64_outportb((SERIAL_PORT + 1), 0x00);
	x64_outportb((SERIAL_PORT + 3), 0x03);
	x64_outportb((SERIAL_PORT + 2), 0xC7);
	x64_outportb((SERIAL_PORT + 4), 0x0B);

	//x64_outportb (PORT + 4, 0x0F);
	//interrupt_set (4,serial_handler, 4);
}

int is_transmit_empty() {
	return x64_inportb(SERIAL_PORT + 5) & 0x20;
}

void WriteSerial(char a) {
	while (is_transmit_empty() == 0);
	x64_outportb(SERIAL_PORT, a);
}

void DebugSerial(char* string) {
	for (int i = 0; i < strlen(string); i++)
		WriteSerial(string[i]);
}

/*
 * SeTextOut -- Serial Text Out
 * @param format -- text to out
 */
AU_EXTERN AU_EXPORT void SeTextOut(char* format, ...) {

	_va_list_ args;
	va_start(args, format);

	while (*format)
	{
		if (*format == '%')
		{
			++format;
			if (*format == 'd')
			{
				size_t width = 0;
				if (format[1] == '.')
				{
					for (size_t i = 2; format[i] >= '0' && format[i] <= '9'; ++i)
					{
						width *= 10;
						width += format[i] - '0';
					}
				}
				size_t i = va_arg(args, size_t);
				char buffer[sizeof(size_t)* 8 + 1];
				//	size_t len
				if ((int)i < 0) {
					DebugSerial("-");
					i = ((int)i * -1);
					sztoa(i, buffer, 10);
				}
				else {
					sztoa(i, buffer, 10);
					size_t len = strlen(buffer);
				}
				/*	while (len++ < width)
				puts("0");*/
				DebugSerial(buffer);
			}
			else if (*format == 'c')
			{
				char c = va_arg(args, char);
				//char buffer[sizeof(size_t) * 8 + 1];
				//sztoa(c, buffer, 10);
				//puts(buffer);
				WriteSerial(c);
			}
			else if (*format == 'x')
			{
				size_t val = va_arg(args,size_t);
				char buffer[sizeof(size_t)* 8 + 1];
				sztoa(val, buffer, 16);
				//puts("0x");
				DebugSerial(buffer);
			}
			else if (*format == 's')
			{
				char* x = va_arg(args, char*);
				DebugSerial(x);
			}
			else if (*format == 'f')
			{
				double x = va_arg(args, double);
				DebugSerial(ftoa(x, 2));
			}
			else if (*format == '%')
			{
				DebugSerial(".");
			}
			else
			{
				char buf[3];
				buf[0] = '%'; buf[1] = *format; buf[2] = '\0';
				DebugSerial(buf);
			}
		}
		else
		{
			char buf[2];
			buf[0] = *format; buf[1] = '\0';
			DebugSerial(buf);
		}
		++format;
	}
	va_end(args);

}

