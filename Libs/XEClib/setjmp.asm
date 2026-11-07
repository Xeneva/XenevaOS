;; 
;; BSD 2-Clause License
;;
;; Copyright (c) 2022-2023, Manas Kamal Choudhury
;; All rights reserved.
;;
;; Redistribution and use in source and binary forms, with or without
;; modification, are permitted provided that the following conditions are met:
;;
;; 1. Redistributions of source code must retain the above copyright notice, this
;;    list of conditions and the following disclaimer.
;;
;; 2. Redistributions in binary form must reproduce the above copyright notice,
;;    this list of conditions and the following disclaimer in the documentation
;;    and/or other materials provided with the distribution.
;;
;; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
;; AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
;; IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
;; DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
;; FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
;; DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
;; SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
;; CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
;; OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;; OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;
;;

section .text
[BITS 64]


global setjmp
export setjmp
setjmp:
     mov [rcx], rbx
	 mov [rcx + 8], rbp
	 mov [rcx + 16], r12
	 mov [rcx + 24], r13
	 mov [rcx + 32], r14
	 mov [rcx + 40],r15
	 mov rdx, rsp
	 mov [rcx + 48], rdx
	 mov rdx,[rsp]
	 mov [rcx + 56], rdx
	 mov rax, [rcx + 56]
	 xor eax, eax
	 ret
	 

global longjmp
export longjmp
longjmp:
     xor rax, rax
	 cmp rdx, 1
	 adc rax, rdx
	 mov rbx, [rcx]
	 mov rbp, [rcx + 8]
	 mov r12, [rcx + 16]
	 mov r13, [rcx + 24]
	 mov r14, [rcx + 32]
	 mov r15, [rcx + 40]
	 mov rsp, [rcx + 48]
	 jmp [rcx + 56] 