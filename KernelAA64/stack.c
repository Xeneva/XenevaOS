/**
* @file stack.c
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2024, Manas Kamal Choudhury
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

#include <stack.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <_null.h>
#include <Drivers/uart.h>

/**
 * @brief AuStackCreate -- create a new stack
 */
AuStack* AuStackCreate() {
	AuStack* stack = (AuStack*)kmalloc(sizeof(AuStack));
	if (!stack)
		return NULL;
	memset(stack, 0, sizeof(AuStack));
	return stack;
}

/**
 * @brief AuStackPush -- push a new item to the stack
 * @param stack -- pointer to the stack where to push
 * @param data -- data to be pushed
 */
void AuStackPush(AuStack* stack, void* data) {
	if (!stack)
		return;
	AuStackItem* newItem = (AuStackItem*)kmalloc(sizeof(AuStackItem));
	if (!newItem)
		return;
	newItem->data = data;
	newItem->link = NULL;

	if (stack->bottom)
		stack->bottom->link = newItem;
	else
		stack->top = newItem;

	stack->bottom = newItem;
	stack->itemCount += 1;
}

/**
 * @brief AuStackPop -- pop a new item from the stack
 * @param stack -- pointer to the stack from where to pop
 */
void* AuStackPop(AuStack* stack) {
	if (!stack)
		return NULL;
	void* data = NULL;
	if (!stack->top)
		return NULL;
	AuStackItem* si;
	si = stack->top;
	stack->top = si->link;
	if (!stack->top)
		stack->bottom = NULL;
	si->link = NULL;
	stack->itemCount--;
	data = si->data;
	kfree(si);
	return data;
}