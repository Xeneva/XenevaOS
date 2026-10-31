/**
* BSD 2-Clause License
*
* Copyright (c) 2024, Manas Kamal Choudhury
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

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/button.h>
#include <widgets/scrollpane.h>
#include <widgets/slider.h>
#include <widgets/progress.h>
#include <keycode.h>
#include <widgets/window.h>
#include <widgets/msgbox.h>
#include <widgets/view.h>
#include <string.h>
#include <widgets/menubar.h>
#include <stdlib.h>
#include <time.h>
#include <widgets/menu.h>

typedef struct _partition_button_ {
	ChWidget wid;
	ChIcon* icon;
	char partitionName[32];
	char guidString[32];
	bool mounted;
} FileManagerPartitionButton;

typedef struct _partitionList_ {
	ChWidget wid;
	uint8_t numPartitions;
	list_t* partitionButtons;
	int button_pos_y;
	int button_pos_x;
} FileManagerPartitionList;

/*
 * FileManagerCreatePartitionList -- create a new partition list widget
 * for file manager
 * @param x -- X location
 * @param y -- Y location
 * @param w -- Width of the widget
 * @param h -- Height of the widget
 */
FileManagerPartitionList* FileManagerCreatePartitionList(int x, int y, int w, int h);
FileManagerPartitionButton*
FileManageCreatePartitionButton(FileManagerPartitionList* partitionList);
