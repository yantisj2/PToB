/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
.syntax unified
.thumb
.section .vectors,"a",%progbits
.word 0x20010000
.word _start
.rept 14
.word test_fault
.endr
.text
.thumb_func
.global _start
_start:
 ldr r0,=__bss_start
 ldr r1,=__bss_end
 movs r2,#0
1: cmp r0,r1
 bhs 2f
 str r2,[r0],#4
 b 1b
2: bl test_main
 b .
.thumb_func
.global test_fault
test_fault:
 mov r0,sp
 b test_fault_c
.thumb_func
.global test_snapshot_registers
.type test_snapshot_registers,%function
test_snapshot_registers:
 push {r4-r11,lr}
 sub sp,sp,#36
 .set off,0
 .rept 8
 ldr r12,[sp,#(72+off)]
 str r12,[sp,#off]
 .set off,off+4
 .endr
 movw r4,#0x4444
 movt r4,#0x4444
 movw r5,#0x5555
 movt r5,#0x5555
 movw r6,#0x6666
 movt r6,#0x6666
 movw r7,#0x7777
 movt r7,#0x7777
 movw r8,#0x8888
 movt r8,#0x8888
 movw r9,#0x9999
 movt r9,#0x9999
 movw r10,#0xaaaa
 movt r10,#0xaaaa
 movw r11,#0xbbbb
 movt r11,#0xbbbb
 bl gm7_source_snapshot
 str r0,[sp,#32]
 ldr r0,=test_returned_regs
 stmia r0,{r4-r11}
 ldr r0,[sp,#32]
 add sp,sp,#36
 pop {r4-r11,pc}
.size test_snapshot_registers,.-test_snapshot_registers
#if TEST_CASE > 0 && TEST_CASE < 5
.thumb_func
.global gm7_source_snapshot_inner
gm7_source_snapshot_inner:
#if TEST_CASE == 1 || TEST_CASE == 4
 movs r5,#1
#endif
#if TEST_CASE == 2 || TEST_CASE == 4
 movs r0,#1
 str r0,[sp,#56] /* Synthetic offline bad callee touches veneer savedR5. */
#endif
#if TEST_CASE == 3
 movs r0,#1
 str r0,[sp,#84] /* Synthetic offline savedLR corruption. */
#endif
 movs r0,#0
 bx lr
#endif
.ltorg
.section .note.GNU-stack,"",%progbits
