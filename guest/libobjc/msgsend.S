// objc_msgSend & friends for the guest Objective-C runtime (arm64).
//
// class->cache points to a cache_table { u64 mask; u64 occupied; {SEL,IMP} buckets[] }.
// Hash: (sel >> 3) & mask, linear probing, empty slot (sel == 0) ends the probe.

    .text
    .p2align 2

// x16 = class to search, x0 = receiver, x1 = selector. Jumps to the IMP.
.macro CACHE_LOOKUP_AND_CALL miss
    ldr     x10, [x16, #16]        // cache table
    ldr     x11, [x10]             // mask
    lsr     x12, x1, #3
    and     x12, x12, x11
1:
    add     x13, x10, #16
    add     x13, x13, x12, lsl #4
    ldp     x9, x17, [x13]
    cmp     x9, x1
    b.eq    2f
    cbz     x9, \miss
    add     x12, x12, #1
    and     x12, x12, x11
    b       1b
2:
    br      x17
.endm

// Saves argument registers, calls __objc_msg_lookup(receiver, sel, cls) and jumps to the result.
.macro SLOW_PATH
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    sub     sp, sp, #(10*8 + 8*16)
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x16, [sp, #64]
    stp     q0, q1, [sp, #80]
    stp     q2, q3, [sp, #112]
    stp     q4, q5, [sp, #144]
    stp     q6, q7, [sp, #176]
    mov     x2, x16
    bl      __objc_msg_lookup
    mov     x17, x0
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x16, [sp, #64]
    ldp     q0, q1, [sp, #80]
    ldp     q2, q3, [sp, #112]
    ldp     q4, q5, [sp, #144]
    ldp     q6, q7, [sp, #176]
    mov     sp, x29
    ldp     x29, x30, [sp], #16
    br      x17
.endm

    .globl _objc_msgSend
_objc_msgSend:
    cbz     x0, Lnil
    ldr     x16, [x0]              // isa
    CACHE_LOOKUP_AND_CALL Lmiss
Lmiss:
    SLOW_PATH
Lnil:
    mov     x0, #0
    mov     x1, #0
    movi    d0, #0
    movi    d1, #0
    movi    d2, #0
    movi    d3, #0
    ret

// objc_msgSendSuper2(struct objc_super *super, SEL op, ...)  super->super_class is the *current* class
    .globl _objc_msgSendSuper2
_objc_msgSendSuper2:
    ldp     x0, x16, [x0]          // receiver, current class
    ldr     x16, [x16, #8]         // superclass
    CACHE_LOOKUP_AND_CALL Lmiss_s2
Lmiss_s2:
    SLOW_PATH

// objc_msgSendSuper(struct objc_super *super, SEL op, ...)  super->super_class is the class to search
    .globl _objc_msgSendSuper
_objc_msgSendSuper:
    ldp     x0, x16, [x0]
    CACHE_LOOKUP_AND_CALL Lmiss_s
Lmiss_s:
    SLOW_PATH

// Variants that only exist on other architectures but may be referenced
    .globl _objc_msgSend_stret
_objc_msgSend_stret:
    b       _objc_msgSend
    .globl _objc_msgSend_fpret
_objc_msgSend_fpret:
    b       _objc_msgSend
    .globl _objc_msgSendSuper2_stret
_objc_msgSendSuper2_stret:
    b       _objc_msgSendSuper2
    .globl _objc_msgSendSuper_stret
_objc_msgSendSuper_stret:
    b       _objc_msgSendSuper

// IMP that returns zero/nil in every return register (unrecognized selectors, nil)
    .globl __objc_nil_imp
__objc_nil_imp:
    mov     x0, #0
    mov     x1, #0
    movi    d0, #0
    movi    d1, #0
    movi    d2, #0
    movi    d3, #0
    ret

// _objc_msgForward: report and return nil
    .globl __objc_msgForward
__objc_msgForward:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    bl      __objc_forward_report
    ldp     x29, x30, [sp], #16
    b       __objc_nil_imp

    .data
    .p2align 4
    .globl __objc_empty_cache
__objc_empty_cache:
    .quad 0, 0, 0, 0
    .globl __objc_empty_vtable
__objc_empty_vtable:
    .quad 0
