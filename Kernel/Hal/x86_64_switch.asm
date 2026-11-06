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

section .text
extern  x64_syscall_handler
extern  funct
extern  ;current_thread
extern AuPrintStack
[BITS 64]


;;====================================================
;; Save context saves current registers to a thread
;;====================================================

global save_context
save_context: 	
    mov [rcx + 0x30], rbx     ;;from here to r15
	mov [rcx + 0x48], rsi   
	mov [rcx + 0x50], rdi 
	mov [rcx + 0x58], rbp    ;rflags /rsp 
	mov [rcx + 0x80], r12    ;rsp /ss
	mov [rcx + 0x88], r13    ;ss  /rip
	mov [rcx + 0x90], r14
	mov [rcx + 0x98], r15

	pushfq  
	pop rax
	mov [rcx + 0x10],rax
	
 	pop rdx            ;return address
	mov [rcx + 0x20], rdx

    mov [rcx + 0x08], rsp
	;rsp savings here

	xor rax, rax  
	jmp rdx



;=======================================================
; Execute Idle, executes provided thread registers
;=======================================================

global execute_idle
execute_idle:
	mov rbx, [rcx + 0x30]
	mov rsi, [rcx + 0x48]
	mov rdi, [rcx + 0x50]
	mov rbp, [rcx + 0x58]
	mov r12, [rcx + 0x80]
	mov r13, [rcx + 0x88]
	mov r14, [rcx + 0x90]
	mov r15, [rcx + 0x98]
	
	mov ds, [rcx + 0xA0]
	mov es, [rcx + 0xA8]
	mov fs, [rcx + 0xB0]
	;mov gs, [rcx + 0xB8]

	mov r9, 1
	cmp r8, 0
	cmove r8, r9
	mov r9,[rcx + 0x10]   ;rflags
	
	mov r10, [rcx + 0xC0]
	mov cr3, r10

    mov rsp, [rcx + 0x08]
	mov rdx, [rcx + 0x20]  ;rip
	mov rcx, [rcx + 0x38]
	push r9
	popfq
	jmp rdx

	

global x64_get_rsp
x64_get_rsp:
    mov rax, rsp
	ret


;;===========================================================
;;  System Call Functions
;;===========================================================

global syscall_entry
syscall_entry:
     ;RCX : return address
	 ;R11 : FLAGS
	 cli
	 
	 mov rdx, rsp         ;we save the user stack
	 mov r9, [gs:1]             ;[rel current_thread]
	 ;mov [r9 + 0xD0], rdx
	 mov rsp, [r9 + 0xC8] 

	 sub rsp, 40
	 mov r10, rdx ;[rel current_thread]   ;store the user stack in r10 because rdx will be modified
	 ;mov rsp, qword[fs:0x20]    ;load kernel stack
	 ;swapgs

	 ;save syscall return stuff
	 push rcx
	 push rdx
	 push r11
	 push rbp

	 mov  rbp, rsp

	 ;align the stack
	 and sp, 0xFFF0
	 ;kernel
	 ;sti

	 
	 mov rcx, r12
	 
	 mov [r9 + 0xE0], r13
	 mov [r9 + 0xE8], r14
	 mov [r9 + 0xF0], r15
	 mov [r9 + 0xF8], rdi

	 mov rax, [r10 + 40]
	 mov [r9 + 0x100], rax
	 mov rax, [r10 + 48]
	 mov [r9 + 0x108] , rax

	 call  x64_syscall_handler

	 ;add rsp, 40
	 ;return 
	 ;cli

	 mov rsp, rbp
	 pop rbp
	 pop r11
	 pop rdx
	 pop rcx
	 ;swapgs

	 ;user stack
	 ;mov r10, [rel current_thread] 
	 mov rsp, r10 ;[r10 + 0xD0]
	 o64 sysret

x64_compat_common:
     sti
	 call x64_syscall_handler
	 cli
	 ret

global x64_syscall_entry_compat
x64_syscall_entry_compat:
     mov rdx, rsp
	 mov rsp, [fs:0x20]
	 ;swapgs
	 call x64_compat_common
	 ;swapgs
	 mov rsp, rdx
	 sysret


global x64_force_sched
x64_force_sched:
    int 0x40
	ret

global SigRet
SigRet:
    mov r12, 24
	syscall
	ret
