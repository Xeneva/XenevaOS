/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2025, Manas Kamal Choudhury
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

.global setjmp
setjmp:
    //x0 = pointer to buffer
    //save six callee saved registers
    //into buffer
    //[buf+0] = x19
    //[buf+8] = x20
    //[buf+16] = x21
    //[buf+24] = x22
    //[buf+32] = x23
    //[buf+40] = x24
    stp x19,x20,[x0]
    stp x21,x22,[x0,#16]
    stp x23,x24,[x0,#32]
    mov x1,sp
    str x1,[x0,#48]
    str x30,[x0,#56]
    mov w0,wzr
    ret

.global longjmp
longjmp:
    mov x2,#1
    cmp x1,#0
    csel x9,x1,x2,NE
    ldp x19,x20,[x0]
    ldp x21,x22,[x0,#16]
    ldp x23,x24,[x0,#32]
    ldr x3, [x0,#48]
    mov sp,x3
    ldr x30,[x0,#56]
    mov x0, x9
    br x30
    ret
