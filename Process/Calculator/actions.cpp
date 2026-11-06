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

#include "calculator.h"
#include <keycode.h>

void SevenAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalcAddDigit(calc, 7);
	CalcUpdateDisplay(calc);
}

void SixAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalcAddDigit(calc, 6);
	CalcUpdateDisplay(calc);
}

void FiveAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalcAddDigit(calc, 5);
	CalcUpdateDisplay(calc);
}

void FourAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalcAddDigit(calc, 4);
	CalcUpdateDisplay(calc);
}

void ThreeAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalcAddDigit(calc, 3);
	CalcUpdateDisplay(calc);
}

void TwoAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalcAddDigit(calc, 2);
	CalcUpdateDisplay(calc);
}

void OneAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalcAddDigit(calc, 1);
	CalcUpdateDisplay(calc);
}

void EightAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalcAddDigit(calc, 8);
	CalcUpdateDisplay(calc);
}

void NineAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalcAddDigit(calc, 9);
	CalcUpdateDisplay(calc);
}

void ZeroAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalcAddDigit(calc, 0);
	CalcUpdateDisplay(calc);
}

void BackAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalcRemoveDigit(calc);
	CalcUpdateDisplay(calc);
}

void AllClearAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalcAllClear(calc);
	CalcClearHistory(calc);
	calc->operator_ = 0;
	calc->output = false;
	calc->num1 = 0;
	calc->num2 = 0;
	CalcClearOutput(calc);
	CalcUpdateDisplay(calc);
}

void AddAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	if (calc->operator_ == CALC_OPERATOR_ADD) {
		CalculatorProcess(calc);
		CalcUpdateDisplay(calc);
	} else {
		if (calc->num1 == 0) {
			calc->operator_ = CALC_OPERATOR_ADD;
			calc->num1 = atoi(calc->inputnum);
			CalcAddToHistory(calc, calc->inputnum, 0);
			CalcAddToHistory(calc, NULL, calc->operator_);
			CalcAllClear(calc);
			CalcUpdateDisplay(calc);
		} else {
			CalcClearHistory(calc);
			calc->operator_ = CALC_OPERATOR_ADD;
			itoa_s(calc->num1, 10, calc->historyBuf);
			calc->historyIdx = strlen(calc->historyBuf) - 1;
			CalcUpdateDisplay(calc);
		}
	}
}

void DivideAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	if (calc->operator_ == CALC_OPERATOR_DIVIDE) {
		CalculatorProcess(calc);
		CalcUpdateDisplay(calc);
	} else {
		if (calc->num1 == 0) {
			calc->operator_ = CALC_OPERATOR_DIVIDE;
			calc->num1 = atoi(calc->inputnum);
			CalcAddToHistory(calc, calc->inputnum, 0);
			CalcAddToHistory(calc, NULL, calc->operator_);
			CalcAllClear(calc);
			CalcUpdateDisplay(calc);
		} else {
			CalcClearHistory(calc);
			calc->operator_ = CALC_OPERATOR_DIVIDE;
			itoa_s(calc->num1, 10, calc->historyBuf);
			calc->historyIdx = strlen(calc->historyBuf) - 1;
			CalcUpdateDisplay(calc);
		}
	}
}

void MultAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	if (calc->operator_ == CALC_OPERATOR_MULT) {
		CalculatorProcess(calc);
		CalcUpdateDisplay(calc);
	} else {
		if (calc->num1 == 0) {
			calc->operator_ = CALC_OPERATOR_MULT;
			calc->num1 = atoi(calc->inputnum);
			CalcAllClear(calc);
			CalcUpdateDisplay(calc);
		} else {
			CalcClearHistory(calc);
			calc->operator_ = CALC_OPERATOR_MULT;
			itoa_s(calc->num1, 10, calc->historyBuf);
			calc->historyIdx = strlen(calc->historyBuf) - 1;
			CalcUpdateDisplay(calc);
		}
	}
}

void SubAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	if (calc->operator_ == CALC_OPERATOR_SUB) {
		CalculatorProcess(calc);
		CalcUpdateDisplay(calc);
	} else {
		if (calc->num1 == 0) {
			calc->operator_ = CALC_OPERATOR_SUB;
			calc->num1 = atoi(calc->inputnum);
			CalcAddToHistory(calc, calc->inputnum, 0);
			CalcAddToHistory(calc, NULL, calc->operator_);
			CalcAllClear(calc);
			CalcUpdateDisplay(calc);
		} else {
			CalcClearHistory(calc);
			calc->operator_ = CALC_OPERATOR_SUB;
			itoa_s(calc->num1, 10, calc->historyBuf);
			calc->historyIdx = strlen(calc->historyBuf) - 1;
			CalcUpdateDisplay(calc);
		}
	}
}

void ModAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	if (calc->operator_ == CALC_OPERATOR_MOD) {
		CalculatorProcess(calc);
		CalcUpdateDisplay(calc);
	} else {
		if (calc->num1 == 0) {
			calc->operator_ = CALC_OPERATOR_MOD;
			calc->num1 = atoi(calc->inputnum);
			CalcAddToHistory(calc, calc->inputnum, 0);
			CalcAddToHistory(calc, NULL, calc->operator_);
			CalcAllClear(calc);
			CalcUpdateDisplay(calc);
		} else {
			CalcClearHistory(calc);
			calc->operator_ = CALC_OPERATOR_MOD;
			itoa_s(calc->num1, 10, calc->historyBuf);
			calc->historyIdx = strlen(calc->historyBuf) - 1;
			CalcUpdateDisplay(calc);
		}
	}
}

void EqualAction(ChWidget* wid, ChWindow* win) {
	CalculatorDisplay* calc = CalculatorGetMainDisplay();
	CalculatorProcess(calc);
	CalcClearHistory(calc);
	CalcAddToHistory(calc, calc->outputnum, 0);
	calc->num2 = 0;
	calc->operator_ = 0;
	CalcUpdateDisplay(calc);
}

/*
 * Handle Key Events --- Handles key events 
 */
void HandleKeyEvents(int keycode) {
	switch (keycode) {
	case KEY_0: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		CalcAddDigit(calc, 0);
		CalcUpdateDisplay(calc);
		break;
	}
	case KEY_1: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		CalcAddDigit(calc, 1);
		CalcUpdateDisplay(calc);
		break;
	}
	case KEY_2: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		CalcAddDigit(calc, 2);
		CalcUpdateDisplay(calc);
		break;
	}
	case KEY_3: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		CalcAddDigit(calc, 3);
		CalcUpdateDisplay(calc);
		break;
	}
	case KEY_4: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		CalcAddDigit(calc, 4);
		CalcUpdateDisplay(calc);
		break;
	}
	case KEY_5: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		CalcAddDigit(calc, 5);
		CalcUpdateDisplay(calc);
		break;
	}
	case KEY_6: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		CalcAddDigit(calc, 6);
		CalcUpdateDisplay(calc);
		break;
	}
	case KEY_7: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		CalcAddDigit(calc, 7);
		CalcUpdateDisplay(calc);
		break;
	}
	case KEY_8: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		CalcAddDigit(calc, 8);
		CalcUpdateDisplay(calc);
		break;
	}
	case KEY_9: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		CalcAddDigit(calc, 9);
		CalcUpdateDisplay(calc);
		break;
	}
	case KEY_ASTERISK: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		if (calc->operator_ == CALC_OPERATOR_MULT) {
			CalculatorProcess(calc);
			CalcUpdateDisplay(calc);
		} else {
			if (calc->num1 == 0) {
				calc->operator_ = CALC_OPERATOR_MULT;
				calc->num1 = atoi(calc->inputnum);
				CalcAllClear(calc);
				CalcUpdateDisplay(calc);
			} else {
				CalcClearHistory(calc);
				calc->operator_ = CALC_OPERATOR_MULT;
				itoa_s(calc->num1, 10, calc->historyBuf);
				calc->historyIdx = strlen(calc->historyBuf) - 1;
				CalcUpdateDisplay(calc);
			}
		}
		break;
	}
	case KEY_KP_DIVIDE: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		if (calc->operator_ == CALC_OPERATOR_DIVIDE) {
			CalculatorProcess(calc);
			CalcUpdateDisplay(calc);
		} else {
			if (calc->num1 == 0) {
				calc->operator_ = CALC_OPERATOR_DIVIDE;
				calc->num1 = atoi(calc->inputnum);
				CalcAddToHistory(calc, calc->inputnum, 0);
				CalcAddToHistory(calc, NULL, calc->operator_);
				CalcAllClear(calc);
				CalcUpdateDisplay(calc);
			} else {
				CalcClearHistory(calc);
				calc->operator_ = CALC_OPERATOR_DIVIDE;
				itoa_s(calc->num1, 10, calc->historyBuf);
				calc->historyIdx = strlen(calc->historyBuf) - 1;
				CalcUpdateDisplay(calc);
			}
		}
		break;
	}
	case KEY_KP_PLUS: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		if (calc->operator_ == CALC_OPERATOR_ADD) {
			CalculatorProcess(calc);
			CalcUpdateDisplay(calc);
		} else {
			if (calc->num1 == 0) {
				calc->operator_ = CALC_OPERATOR_ADD;
				calc->num1 = atoi(calc->inputnum);
				CalcAddToHistory(calc, calc->inputnum, 0);
				CalcAddToHistory(calc, NULL, calc->operator_);
				CalcAllClear(calc);
				CalcUpdateDisplay(calc);
			} else {
				CalcClearHistory(calc);
				calc->operator_ = CALC_OPERATOR_ADD;
				itoa_s(calc->num1, 10, calc->historyBuf);
				calc->historyIdx = strlen(calc->historyBuf) - 1;
				CalcUpdateDisplay(calc);
			}
		}
		break;
	}
	case KEY_KP_MINUS: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		if (calc->operator_ == CALC_OPERATOR_SUB) {
			CalculatorProcess(calc);
			CalcUpdateDisplay(calc);
		} else {
			if (calc->num1 == 0) {
				calc->operator_ = CALC_OPERATOR_SUB;
				calc->num1 = atoi(calc->inputnum);
				CalcAddToHistory(calc, calc->inputnum, 0);
				CalcAddToHistory(calc, NULL, calc->operator_);
				CalcAllClear(calc);
				CalcUpdateDisplay(calc);
			} else {
				CalcClearHistory(calc);
				calc->operator_ = CALC_OPERATOR_SUB;
				itoa_s(calc->num1, 10, calc->historyBuf);
				calc->historyIdx = strlen(calc->historyBuf) - 1;
				CalcUpdateDisplay(calc);
			}
		}
		break;
	}
	case KEY_PERCENT: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		if (calc->operator_ == CALC_OPERATOR_MOD) {
			CalculatorProcess(calc);
			CalcUpdateDisplay(calc);
		} else {
			if (calc->num1 == 0) {
				calc->operator_ = CALC_OPERATOR_MOD;
				calc->num1 = atoi(calc->inputnum);
				CalcAddToHistory(calc, calc->inputnum, 0);
				CalcAddToHistory(calc, NULL, calc->operator_);
				CalcAllClear(calc);
				CalcUpdateDisplay(calc);
			} else {
				CalcClearHistory(calc);
				calc->operator_ = CALC_OPERATOR_MOD;
				itoa_s(calc->num1, 10, calc->historyBuf);
				calc->historyIdx = strlen(calc->historyBuf) - 1;
				CalcUpdateDisplay(calc);
			}
		}
		break;
	}
	case KEY_BACKSPACE: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		CalcRemoveDigit(calc);
		CalcUpdateDisplay(calc);
		break;
	}
	case KEY_RETURN: {
		CalculatorDisplay* calc = CalculatorGetMainDisplay();
		CalculatorProcess(calc);
		CalcClearHistory(calc);
		CalcAddToHistory(calc, calc->outputnum, 0);
		calc->num2 = 0;
		calc->operator_ = 0;
		CalcUpdateDisplay(calc);
		break;
	}
	default:
		break;
	}
}
