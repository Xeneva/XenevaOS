/**
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

/*
 * AuStackCreate -- create a new stack
 */
AuStack* AuStackCreate(){
	AuStack* stack = (AuStack*)kmalloc(sizeof(AuStack));
	memset(stack, 0, sizeof(AuStack));
	return stack;
}

/*
 * AuStackPush -- push a new item to the stack
 * @param stack -- pointer to the stack where to push
 * @param data -- data to be pushed
 */
void AuStackPush(AuStack *stack, void *data){
	AuStackItem* newItem = (AuStackItem*)kmalloc(sizeof(AuStackItem));
	newItem->data = data;
	newItem->link = stack->top;
	stack->top = newItem;
	stack->itemCount += 1;
}

/*
 * AuStackPop -- pop a new item from the stack
 * @param stack -- pointer to the stack from where to pop
 */
void* AuStackPop(AuStack* stack) {
	void* data = NULL;
	if (!stack->top)
		return NULL;
	AuStackItem* si;
	si = stack->top;
	stack->top = stack->top->link;
	si->link = NULL;
	data = si->data;
	kfree(si);
	stack->itemCount--;
	return data;
}