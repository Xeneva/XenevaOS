/**
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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

#ifndef __CALCULATOR_H__
#define __CALCULATOR_H__

#include <stdint.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/window.h>

#define CALC_OPERATOR_DIVIDE 1
#define CALC_OPERATOR_MULT	 2
#define CALC_OPERATOR_ADD	 3
#define CALC_OPERATOR_SUB	 4
#define CALC_OPERATOR_MOD	 5

typedef struct _display_widget_ {
	ChWidget wid;
	char* inputnum;
	uint16_t inputidx;
	char* outputnum;
	uint8_t operator_;
	char* historyBuf;
	uint16_t historyIdx;
	int num1;
	int num2;
	bool output;
	bool freshResult;
} CalculatorDisplay;

/*
* CalculatorGetMainDisplay -- returns the main display
*/
extern CalculatorDisplay* CalculatorGetMainDisplay();

/*
* CalcUpdateDisplay -- update the newly modified display
* @param disp -- Pointer to calculator display
*/
extern void CalcUpdateDisplay(CalculatorDisplay* disp);

/* CalcAddDigit -- adds a digit to the calculator
* processor, only one digit
* @param disp -- Calculator display processor
* @param number -- number with one digit
*/
extern void CalcAddDigit(CalculatorDisplay* disp, int number);

/*
* CalcRemoveDigit -- removes a digit from input buffer
* @param disp -- Calculator display
*/
extern void CalcRemoveDigit(CalculatorDisplay* disp);

/*
* CalcAllClear -- clear all digits
* @param disp -- Pointer to calculator display
* processor
*/
extern void CalcAllClear(CalculatorDisplay* disp);

/*
* CalculatorProcess -- process calculator calculations
* @param calc -- Pointer to calculator display
*/
extern void CalculatorProcess(CalculatorDisplay* calc);

/*
* CalcAddToHistory -- add recent calculations to history
* @param disp -- Pointer to calculator display
* @param num -- numbers to add
* @param operator_ -- operator to add
*/
extern void CalcAddToHistory(CalculatorDisplay* disp, char* num, uint8_t operator_);

/*
* CalcClearHistory -- clears recent digits history
* @param disp -- Pointer to calculator display
*/
extern void CalcClearHistory(CalculatorDisplay* disp);

/*
* CalcClearOutput -- clear all output
* @param disp -- Pointer to calculator display
*/
extern void CalcClearOutput(CalculatorDisplay* disp);

void SevenAction(ChWidget* wid, ChWindow* win);
void SixAction(ChWidget* wid, ChWindow* win);
void FiveAction(ChWidget* wid, ChWindow* win);
void FourAction(ChWidget* wid, ChWindow* win);
void ThreeAction(ChWidget* wid, ChWindow* win);
void TwoAction(ChWidget* wid, ChWindow* win);
void OneAction(ChWidget* wid, ChWindow* win);
void EightAction(ChWidget* wid, ChWindow* win);
void NineAction(ChWidget* wid, ChWindow* win);
void ZeroAction(ChWidget* wid, ChWindow* win);
void BackAction(ChWidget* wid, ChWindow* win);
void AllClearAction(ChWidget* wid, ChWindow* win);
void AddAction(ChWidget* wid, ChWindow* win);
void DivideAction(ChWidget* wid, ChWindow* win);
void MultAction(ChWidget* wid, ChWindow* win);
void SubAction(ChWidget* wid, ChWindow* win);
void ModAction(ChWidget* wid, ChWindow* win);
void EqualAction(ChWidget* wid, ChWindow* win);
void HandleKeyEvents(int keycode);

#endif