.extern __bss_start
.extern __bss_size 

.section .text
.global _start
_start:
   ldr x30, =stack_top
   mov x1,x4
   mov sp, x30
   ldr x5, =__bss_start
   ldr w6, =__bss_size
3:
   cbz w6, 4f
   str xzr, [x5], #8
   sub w6, w6, #1
   cbnz w6, 3b
4:
   bl LittleBootMain
hang:
   wfe
   b hang

.align 12
stack_bottom:
   .skip 16384
stack_top: