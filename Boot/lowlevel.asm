;;
; BSD 2-Clause License
;
; Copyright (c) 2023-2025, Manas Kamal Choudhury
; All rights reserved.
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions are met:
;
; 1. Redistributions of source code must retain the above copyright notice, this
;    list of conditions and the following disclaimer.
;
; 2. Redistributions in binary form must reproduce the above copyright notice,
;    this list of conditions and the following disclaimer in the documentation
;    and/or other materials provided with the distribution.
;
; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
; AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
; IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
; DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
; FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
; DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
; SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
; CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
; OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
; OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;
;;


section .text
[BITS 64]

global inportb
inportb:
       xor rax, rax
	   mov dx, cx
	   in  al, dx
	   ret


global inportw
inportw:
       xor rax, rax
	   mov dx, cx
	   in  ax, dx
	   ret

global inportd
inportd:
       xor rax, rax
	   mov dx, cx
	   in  eax, dx
	   ret

global outportb
outportb:
       mov al, dl
	   mov dx, cx
	   out dx, al
	   ret

global outportw
outportw:
       mov ax, dx
	   mov dx, cx
	   out dx, ax
	   ret

global outportd
outportd:
       mov eax, edx
	   mov  dx, cx
	   out  dx, eax
	   ret

global cpuid
cpuid:
       push rbp
	   mov rbp, rsp
	   push rbx
	   mov rax, rcx
	   mov r10, rdx
	   mov rcx, [rbp + 56]
	   cpuid
	   mov [r10], rax
	   mov [r8], rbx
	   mov [r9], rcx
	   mov r11, [rbp+48]
	   mov [r11], rdx
	   pop rbx
	   pop rbp
	   ret

global cacheflush
cacheflush:
wbinvd
ret

global tlbflush
tlbflush:
       invlpg [rcx]
	   ret


global memory_barrier
memory_barrier:
      mfence
	  ret

global get_paging_root
get_paging_root:
      mov rax, cr3
	  ret

global set_paging_root
set_paging_root:
      ;mov rax, 0x000ffffffffff000
	  ;and rcx, rax
      mov cr3, rcx
	  ret


global cpu_startup
cpu_startup:
      ret
	  mov rax, cr0
	  and  ax, 0xFFFB
	  or   ax, 0x2
	  mov  cr0, rax
	  mov  rax, cr4
	  or   ax, (3<<9)
	  mov cr4, rax

	  ; SSE is enabled. Now enable AVX, id AVX is supported

	  push rax
	  push rcx
	  xor  rcx, rcx
	  xgetbv 
	  or  al, 7
	  xsetbv
	  pop rcx
	  pop rax
	  ret





global rdmsr
rdmsr:
       rdmsr
       shl rdx, 32
	   or  rax, rdx
	   ret

global wrmsr
wrmsr:
       mov rax, rdx
	   shr rdx, 32
	   wrmsr
	   ret

global read_cr0
read_cr0:
       mov rax, cr0
	   ret

global write_cr0
write_cr0:
	   mov cr0, rcx
	   ret

global call_kernel
call_kernel:
      ;RCX = PARAM 1
	  ;RDX = KERNEL_ENTRY
	  ;R8 = KERNEL_STACK
	  ;R9 = STACK SIZE
	  add r8, r9
      sub r8, 0x28
      mov rsp, r8
      push rbp
      mov rbp, rsp
      sub rsp, 32
      call rdx
      jmp $
      ret

global activate_sse
activate_sse:
      mov rax, cr0
	  and ax, 0xFFFB
	  or  ax, 0x2
	  mov cr0,rax
	  mov rax,cr4
	  or  ax, 3 << 9
	  mov cr4, rax
	  ret

global activate_avx
activate_avx:
      push rax
	  push rcx
	  push rdx

      xor rcx, rcx
	  xgetbv
	  or  eax, 7
	  xsetbv

	  pop rdx
	  pop rcx
	  pop rax
	  ret

global arch_cli
arch_cli:
      cli
	  ret