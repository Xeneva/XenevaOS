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

#include <Mm/vmarea.h>
#include <Mm/kmalloc.h>
#include <list.h>

/*
 * AuInsertVMArea -- insert a memory segment to the given process
 * @param proc -- pointer to the process
 * @param area -- pointer to the vm area
 */
void AuInsertVMArea(AuProcess* proc, AuVMArea* area) {
	list_add(proc->vmareas, area);
}

/*
 * AuRemoveVMArea -- removes a memory segment from the given
 * process
 * @param proc -- pointer to the process
 * @param area -- pointer to the vm area
 */
void AuRemoveVMArea(AuProcess* proc, AuVMArea* area) {
	if (!area)
		return;
	if (proc && proc->vmareas) {
		for (int i = 0; i < proc->vmareas->pointer; i++) {
			AuVMArea* area_ = (AuVMArea*)list_get_at(proc->vmareas, i);
			if (area_ == area) {
				/** list indices changed, so break the loop */
				list_remove(proc->vmareas, i);
				break;
			}
		}
	}
	kfree(area);
}

/*
 * AuVMAreaCreate -- create virtual memory segment for
 * current process
 * @param start -- starting address
 * @param end -- ending address
 * @param prot -- protection flags
 * @param len -- length of the area
 * @param type -- type of the area
 */
AuVMArea* AuVMAreaCreate(size_t start, size_t end, uint8_t prot, size_t len, uint8_t type) {
	AuVMArea* area = (AuVMArea*)kmalloc(sizeof(AuVMArea));
	if (!area)
		return NULL;
	memset(area, 0, sizeof(AuVMArea));
	area->start = start;
	area->end = end;
	area->prot_flags = prot;
	area->len = len;
	area->type = type;
	area->file = NULL;
	return area;
}

/*
 * AuVMAreaGet -- finds a virtual memory area from the
 * given address
 * @param proc -- pointer to the process
 * @param address -- address to search
 */
AuVMArea* AuVMAreaGet(AuProcess* proc, size_t address) {
	if (!proc || !proc->vmareas)
		return NULL;
	for (unsigned int i = 0; i < proc->vmareas->pointer; i++) {
		AuVMArea* area_ = (AuVMArea*)list_get_at(proc->vmareas, i);
		if (!area_)
			continue;
		if (address > area_->start && address < area_->end)
			return area_;
	}
	return NULL;
}
