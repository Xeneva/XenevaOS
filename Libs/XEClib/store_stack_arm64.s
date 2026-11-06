/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2026, Manas Kamal Choudhury
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

.global _store_stack_param
_store_stack_param:
    stp x1, x2, [x0,#0]
    stp x3, x4, [x0,#16]
    stp x5, x6, [x0,#32]
    str x7,     [x0,#48]

    stp q0,q1,[x0,#64]
    stp q2,q3,[x0,#96]
    stp q4,q5,[x0,#128]
    stp q6,q7,[x0,#160]
    ret

.global _store_stack_param_snprintf
_store_stack_param_snprintf:
    stp x3, x4, [x0,#0]
    stp x5, x6, [x0,#16]
    str x7,  [x0,#32]

    stp q0,q1,[x0,#64]
    stp q2,q3,[x0,#96]
    stp q4,q5,[x0,#128]
    stp q6,q7,[x0,#160]
    ret

.global _store_stack_param_sprintf
_store_stack_param_sprintf:
    stp x2, x3, [x0,#0]
    stp x4, x5, [x0,#16]
    stp x6, x7, [x0,#32]

    stp q0,q1,[x0,#64]
    stp q2,q3,[x0,#96]
    stp q4,q5,[x0,#128]
    stp q6,q7,[x0,#160]
    ret

    