.global __chkstk
__chkstk:
ret


.global _store_stack_param
_store_stack_param:
    stp x2, x3, [x0,#0]
    stp x4, x5, [x0,#16]
    stp x6, x7, [x0,#32]
    str x8,     [x0,#48]

    stp q0,q1,[x0,#64]
    stp q2,q3,[x0,#96]
    stp q4,q5,[x0,#128]
    stp q6,q7,[x0,#160]
    ret