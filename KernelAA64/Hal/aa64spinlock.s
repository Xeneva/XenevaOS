/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

.global aa64_spinlock_acquire
aa64_spinlock_acquire:
1:
    ldaxr w1, [x0]
    cbnz  w1, 2f
    mov   w2, #1
    stxr  w1, w2, [x0]
    cbnz  w1, 1b
    ret
2:
    wfe
    b  1b

.global aa64_spinlock_try_acquire
aa64_spinlock_try_acquire:
    ldaxr w1, [x0]
    cbnz  w1, 1f
    mov   w2, #1
    stxr  w1, w2, [x0]
    cbnz  w1, 1f
    mov   w0, #1
    ret
1:
    clrex  
    mov w0, #0 
    ret 


.global aa64_spinlock_release 
aa64_spinlock_release:
    mov w1, #0
    stlr w1, [x0]
    sev 
    ret 
    

.global aa64_spinlock_acquire_irq_save
aa64_spinlock_acquire_irq_save:
    mrs x3, daif
    msr daifset, #2
    stp x3, x30, [sp, #-16]!
    bl aa64_spinlock_acquire
    ldp x3, x30, [sp], #16
    mov x0, x3
    ret

.global aa64_spinlock_release_irq_restore
aa64_spinlock_release_irq_restore:
    stp x1, x30, [sp, #-16]!
    bl aa64_spinlock_release
    ldp x1, x30, [sp], #16
    msr daif, x1
    ret



