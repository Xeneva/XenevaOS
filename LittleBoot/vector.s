.extern sync_el1_handler
.extern fault_el1_handler
.extern irq_el1_handler

.global vectors
.balign 0x800
vectors:
   b .
.balign 0x80
   b .
.balign 0x80
   b .
.balign 0x80 
   b .

.balign 0x80
   b fault_el1_wrapper
.balign 0x80
   b .
.balign 0x80
   b .
.balign 0x80
   b .
.balign 0x80
   b sync_el1_wrapper
.balign 0x80
   b irq_el1_wrapper
.balign 0x80
   b .
.balign 0x80
   b .
.balign 0x80
   b .
.balign 0x80
   b .
.balign 0x80
   b .
.balign 0x80
   b .


.global sync_el1_wrapper
sync_el1_wrapper:
   mov x18, 100
   mov x0,sp
   mov x1,x18
   bl sync_el1_handler
   eret

.global fault_el1_wrapper
fault_el1_wrapper:
   mov x0,sp
   bl fault_el1_handler
   eret

.global irq_el1_wrapper
irq_el1_wrapper:
   mov x0,sp
   bl irq_el1_handler
   eret
