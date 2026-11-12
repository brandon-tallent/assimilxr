/* Reset entry. The boot loader or a chain-load enters in Supervisor mode
 * with the MMU off. Interrupts run through StarterWare's handler, which
 * calls service routines in System mode. */
    .syntax unified
    .arm
    .cpu cortex-a8
    .fpu neon

    .equ MODE_FIQ, 0x11
    .equ MODE_IRQ, 0x12
    .equ MODE_SUPERVISOR, 0x13
    .equ MODE_ABORT, 0x17
    .equ MODE_UNDEFINED, 0x1b
    .equ MODE_SYSTEM, 0x1f
    .equ SCTLR_HIGH_VECTORS, (1 << 13)
    .equ CPACR_CP10_CP11_FULL, (0xf << 20)
    .equ FPEXC_ENABLE, (1 << 30)

    .section .vectors, "ax"
    .global _start
_start:
    ldr pc, reset_vector
    ldr pc, undefined_vector
    ldr pc, svc_vector
    ldr pc, prefetch_vector
    ldr pc, data_vector
    b .
    ldr pc, irq_vector
    ldr pc, fiq_vector
reset_vector:       .word reset
undefined_vector:   .word undefined_entry
svc_vector:         .word SVC_Handler
prefetch_vector:    .word prefetch_abort_entry
data_vector:        .word data_abort_entry
    @ Offset 0x34: the module's boot loader stores 1 here just before it
    @ starts the image. Nothing the firmware needs may live at this offset.
    .global loader_flag
loader_flag:        .word 0
irq_vector:         .word IRQHandler
fiq_vector:         .word FIQHandler

    .text
reset:
    cpsid aif, #MODE_SUPERVISOR
    @ The image as loaded, before any variable changes, for a reload to
    @ difference against (app/reload.c)
    ldr r0, =image_start
    ldr r1, =reload_pristine
    ldr r2, =image_end
1:  ldmia r0!, {r3-r10}
    stmia r1!, {r3-r10}
    cmp r0, r2
    blo 1b
    ldr r0, =_start
    mcr p15, 0, r0, c12, c0, 0  @ VBAR
    mrc p15, 0, r0, c1, c0, 0
    bic r0, r0, #SCTLR_HIGH_VECTORS
    mcr p15, 0, r0, c1, c0, 0
    isb

    cps #MODE_FIQ
    ldr sp, =fiq_stack_top
    cps #MODE_IRQ
    ldr sp, =irq_stack_top
    cps #MODE_ABORT
    ldr sp, =abort_stack_top
    cps #MODE_UNDEFINED
    ldr sp, =abort_stack_top
    cps #MODE_SYSTEM
    ldr sp, =system_stack_top
    cps #MODE_SUPERVISOR
    ldr sp, =stack_top

    mrc p15, 0, r0, c1, c0, 2
    orr r0, r0, #CPACR_CP10_CP11_FULL
    mcr p15, 0, r0, c1, c0, 2
    isb
    mov r0, #FPEXC_ENABLE
    vmsr fpexc, r0

    ldr r0, =bss_start
    ldr r1, =bss_end
    mov r2, #0
1:  cmp r0, r1
    strlo r2, [r0], #4
    blo 1b

    bl __libc_init_array
    bl main
    b .

/* During a Peek or Poke a faulting load or store is skipped and counted,
 * so a probe of an unclocked peripheral reports an error. Any other fault
 * is recorded in the trace and stops the firmware; the watchdog then
 * restarts the module. */
    .equ TRACE_ADDRESS, 0x9b0000c0
    .equ TRACE_FAULT, 16
    .equ FAULT_DATA_ABORT, 1
    .equ FAULT_PREFETCH_ABORT, 2
    .equ FAULT_UNDEFINED, 3
data_abort_entry:
    push {r0, r1}
    ldr r0, =peek_active
    ldr r0, [r0]
    cmp r0, #0
    beq 1f
    ldr r0, =data_aborts
    ldr r1, [r0]
    add r1, r1, #1
    str r1, [r0]
    pop {r0, r1}
    subs pc, lr, #4
1:  sub r1, lr, #8
    mov r0, #FAULT_DATA_ABORT
    mrc p15, 0, r2, c6, c0, 0       @ DFAR
    mrc p15, 0, r3, c5, c0, 0       @ DFSR
    b record_fault
prefetch_abort_entry:
    sub r1, lr, #4
    mov r0, #FAULT_PREFETCH_ABORT
    mrc p15, 0, r2, c6, c0, 2       @ IFAR
    mrc p15, 0, r3, c5, c0, 1       @ IFSR
    b record_fault
undefined_entry:
    sub r1, lr, #4
    mov r0, #FAULT_UNDEFINED
    mov r2, #0
    mov r3, #0
record_fault:
    ldr r12, =TRACE_ADDRESS
    add r12, r12, #TRACE_FAULT
    stmia r12, {r0-r3}
    dsb
    b .

/* Reload's last step, run from a copy in DDR outside the image (MMU and
 * caches already clean): MMU off, copy r1 bytes from r0 to the load
 * address, invalidate the instruction cache, branch predictor and TLB,
 * and enter the new image as a chain-load does. */
    .equ SCTLR_MMU_ENABLE, 1
    .equ IMAGE_LOAD_ADDRESS, 0x80000000
    .global reload_trampoline_start, reload_trampoline_end
reload_trampoline_start:
    cpsid if, #MODE_SUPERVISOR
    mrc p15, 0, r3, c1, c0, 0
    bic r3, r3, #SCTLR_MMU_ENABLE
    mcr p15, 0, r3, c1, c0, 0
    isb
    mov r2, #IMAGE_LOAD_ADDRESS
1:  ldr r3, [r0], #4
    str r3, [r2], #4
    subs r1, r1, #4
    bne 1b
    dsb
    mov r3, #0
    mcr p15, 0, r3, c7, c5, 0       @ instruction cache
    mcr p15, 0, r3, c7, c5, 6       @ branch predictor
    mcr p15, 0, r3, c8, c7, 0       @ TLB
    dsb
    isb
    mov pc, #IMAGE_LOAD_ADDRESS
reload_trampoline_end:
