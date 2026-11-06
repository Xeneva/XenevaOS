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

#include <Drivers/mouse.h>
#include <Hal/x86_64_cpu.h>
#include <Hal/hal.h>
#include <Hal/x86_64_lowlevel.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <aucon.h>
#include <Fs/pipe.h>
#include <Fs/Dev/devinput.h>
#include <Fs/Dev/devfs.h>
#include <_null.h>
#include <Hal/serial.h>
#include <loader.h>
#include <process.h>

#define MOUSE_PORT   0x60
#define MOUSE_STATUS 0x64
#define MOUSE_ABIT   0x02
#define MOUSE_BBIT   0x01
#define MOUSE_WRITE  0xD4
#define MOUSE_F_BIT  0x20
#define MOUSE_V_BIT  0x08

#define MOUSE_DEFAULT 0
#define MOUSE_SCROLLWHEEL 1
#define MOUSE_BUTTONS 2

#define MOUSE_LEFT_CLICK   		  0x01
#define MOUSE_RIGHT_CLICK  		  0x02
#define MOUSE_MIDDLE_CLICK 		  0x04
#define MOUSE_MOUSE_SCROLL_UP     0x10
#define MOUSE_MOUSE_SCROLL_DOWN   0x20

#define MOUSE_SET_REMOTE   0xF0
#define MOUSE_DEVICE_ID    0xF2
#define MOUSE_SAMPLE_RATE  0xF3
#define MOUSE_DATA_ON      0xF4
#define MOUSE_DATA_OFF     0xF5
#define MOUSE_SET_DEFAULTS 0xF6

PS2Mouse *__ps2mouse;

void PS2MouseWaitInput() {
	uint64_t time_out = 100000UL;
	while (--time_out){
		if (!(x64_inportb(0x64) & (1 << 1))) return;
	}
}

/*
 * PS2MouseWait -- waits for a while
 * @param a_type -- wait type
 */
void PS2MouseWaitOutput() {
	uint32_t _timer_out = 100000;
	while (--_timer_out) {
		if (x64_inportb(0x64) & (1<<0))
			return;
	}
}

/*
 * AuPS2MouseWrite -- write a data to ps2 port
 * @param write -- write data 
 */
uint8_t AuPS2MouseWrite(uint8_t write) {
	PS2MouseWaitInput();
	x64_outportb(0x64, 0xD4);
	PS2MouseWaitInput();
	x64_outportb(0x60, write);
	PS2MouseWaitOutput();
	return x64_inportb(0x60);
}

uint8_t AuPS2MouseRead() {
	PS2MouseWaitOutput();
	return x64_inportb(0x60);
}

void PS2MouseHandler(size_t v, void* p) {
	x64_cli();
	uint8_t status = x64_inportb(MOUSE_STATUS);
	while ((status & MOUSE_BBIT) && (status & MOUSE_F_BIT)) {
		int8_t mouse_in = x64_inportb(MOUSE_PORT);
		switch (__ps2mouse->mouse_cycle) {
		case 0:
			__ps2mouse->mouse_byte[0] = mouse_in;
			if (!(mouse_in & MOUSE_V_BIT)) break;
			++__ps2mouse->mouse_cycle;
			break;
		case 1:
			__ps2mouse->mouse_byte[1] = mouse_in;
			++__ps2mouse->mouse_cycle;
			break;
		case 2:
			__ps2mouse->mouse_byte[2] = mouse_in;
			if (__ps2mouse->mouse_mode == MOUSE_SCROLLWHEEL){
				++__ps2mouse->mouse_cycle;
				break;
			}
			goto finish_packet;
		case 3:
			__ps2mouse->mouse_byte[3] = mouse_in;
			goto finish_packet;
		case 4:
			__ps2mouse->mouse_byte[4] = mouse_in;
			goto finish_packet;
		}

		goto read_next;

	finish_packet:
		__ps2mouse->mouse_cycle = 0;

		int x = __ps2mouse->mouse_byte[1];
		int y = __ps2mouse->mouse_byte[2];
		if (x && __ps2mouse->mouse_byte[0] & (1 << 4))
			x = x - 0x100;

		if (y && __ps2mouse->mouse_byte[0] & (1 << 5))
			y = y - 0x100;

		__ps2mouse->mouse_x_diff = x;
		__ps2mouse->mouse_y_diff = y;
		__ps2mouse->mouse_x += __ps2mouse->mouse_x_diff;
		__ps2mouse->mouse_y -= __ps2mouse->mouse_y_diff;

		if (__ps2mouse->mouse_x < 0)
			__ps2mouse->mouse_x = 0;

		if (__ps2mouse->mouse_y < 0)
			__ps2mouse->mouse_y = 0;

		__ps2mouse->mouse_butt_state = 0;

		if (__ps2mouse->mouse_byte[0] & 0x01) {    //0x01 for PS/2
			__ps2mouse->curr_button[0] = 1;
			__ps2mouse->mouse_butt_state |= LEFT_CLICK;
		}
		else
			__ps2mouse->curr_button[0] = 0;

		if (__ps2mouse->mouse_byte[0] & 0x02) {
			__ps2mouse->curr_button[2] = 1;
			SeTextOut("Right clicked \r\n");
			__ps2mouse->mouse_butt_state |= RIGHT_CLICK;
		}
		else
			__ps2mouse->curr_button[2] = 0;

		if (__ps2mouse->mouse_byte[0] & 0x04)
			__ps2mouse->mouse_butt_state |= MOUSE_MIDDLE_CLICK;

		if (__ps2mouse->mouse_byte[3]){
			if ((int8_t)__ps2mouse->mouse_byte[3] > 0)
				__ps2mouse->mouse_butt_state |= MOUSE_SCROLL_DOWN;
			else if ((int8_t)__ps2mouse->mouse_byte[3] < 0)
				__ps2mouse->mouse_butt_state |= MOUSE_SCROLL_UP;
		}


		AuInputMessage newmsg;
		memset(&newmsg, 0, sizeof(AuInputMessage));
		newmsg.type = AU_INPUT_MOUSE;
		newmsg.xpos = __ps2mouse->mouse_x;
		newmsg.ypos = __ps2mouse->mouse_y;
		newmsg.button_state = __ps2mouse->mouse_butt_state;

		/*AuInputMessage oldmsg;
		AuDevReadMice(&oldmsg);*/

		AuDevWriteMice(&newmsg);
		memcpy(__ps2mouse->prev_button, __ps2mouse->curr_button, 3);
		memset(__ps2mouse->curr_button, 0x00, 3);

	read_next:
		break;
	}

	x64_sti();
	AuInterruptEnd(12);
}


/*
 * AuPS2MouseSetPos -- set custom mouse position
 * rather than default (0,0) position
 * @param x -- x position
 * @param y -- y position
 */
void AuPS2MouseSetPos(int32_t x, int32_t y) {
	if (!__ps2mouse)
		return;
	__ps2mouse->mouse_x = x;
	__ps2mouse->mouse_y = y;
}

void AuPS2DisableMouse() {
	x64_outportb(0x64, 0xA7);
	x64_outportb(0x64, 0xAD);
}

/*
 * AuPS2MouseInitialise -- initialise the ps2 mouse system
 */
void AuPS2MouseInitialise() {
	__ps2mouse = (PS2Mouse*)kmalloc(sizeof(PS2Mouse));
	memset(__ps2mouse, 0, sizeof(PS2Mouse));

	__ps2mouse->mouse_mode = MOUSE_DEFAULT;
	uint8_t status;

	PS2MouseWaitInput();
	x64_outportb(0x64, 0xA8);

	PS2MouseWaitInput();
	x64_outportb(0x64, 0x20);

	PS2MouseWaitInput();
	status = (x64_inportb(0x60) | 2);

	PS2MouseWaitInput();
	x64_outportb(0x64, 0x60);

	PS2MouseWaitInput();
	x64_outportb(0x60, status);

	AuPS2MouseWrite(0xF6);
	AuPS2MouseRead();

	AuPS2MouseWrite(0xF4);
	AuPS2MouseRead();

	/* Enable the scroll wheel */
	AuPS2MouseWrite(MOUSE_DEVICE_ID);
	status = AuPS2MouseRead();
	AuPS2MouseWrite(MOUSE_SAMPLE_RATE);
	AuPS2MouseWrite(200);
	AuPS2MouseWrite(MOUSE_SAMPLE_RATE);
	AuPS2MouseWrite(100);
	AuPS2MouseWrite(MOUSE_SAMPLE_RATE);
	AuPS2MouseWrite(80);
	AuPS2MouseWrite(MOUSE_DEVICE_ID);
	status = AuPS2MouseRead();
	if (status == 3) {
		AuTextOut("PS2 Mouse with scroll wheel \n");
		__ps2mouse->mouse_mode = MOUSE_SCROLLWHEEL;
	}

	__ps2mouse->mouse_x = 0;
	__ps2mouse->mouse_y = 0;

	AuHalRegisterIRQ(34, PS2MouseHandler, 12, false);  //34
}