/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Explicit AAPCS frame, diagnostic canary only. All original arguments are
 * forwarded unchanged. A mismatch is retained and faults before any restore;
 * the observer never repairs a pointer or continues timing after a violation.
 */
#include "snapshot_witness.h"
.syntax unified
.thumb
.cfi_sections .debug_frame
.text
.align 2
.global gm7_source_snapshot
.type gm7_source_snapshot,%function
.thumb_func
gm7_source_snapshot:
 .cfi_startproc
 ldr r12,=r2_gm7_witness_state
 ldrb r12,[r12]
 and r12,r12,#R2_WITNESS_REQUIRED
 cmp r12,#R2_WITNESS_REQUIRED
 bne gm7_source_snapshot_inner /* Prior retained crash still pending, or RC. */
 push {r0-r11,lr}
 .cfi_def_cfa_offset 52
 .cfi_offset r4,-36
 .cfi_offset r5,-32
 .cfi_offset r6,-28
 .cfi_offset r7,-24
 .cfi_offset r8,-20
 .cfi_offset r9,-16
 .cfi_offset r10,-12
 .cfi_offset r11,-8
 .cfi_offset lr,-4
 sub sp,sp,#36
 .cfi_def_cfa_offset 88
 /* Outgoing32B arguments0..31, result32, originalR0..R3 at36..48,
  * savedR4..R11 at52..80, savedLR84. EntrySP is8-byte aligned. */
 ldr r12,=r2_crash_record
 movs r3,#0
 str r3,[r12,#508] /* Invalidate old capsule only after durable handoff. */
 add r12,r12,#(R2_WITNESS_WORD*4)
 str r3,[r12,#124] /* Invalidate witness before rewriting any live array. */
 ldr r3,=(R2_WITNESS_VERSION|0x10)
 str r3,[r12,#4]
 ldr r3,[r12,#0]
 ldr r2,=R2_WITNESS_MAGIC
 cmp r3,r2
 ite eq
 ldreq r3,[r12,#8]
 movne r3,#0
 adds r3,r3,#1
 it eq
 moveq r3,#1
 str r3,[r12,#8]
 str r2,[r12,#0]
 add r3,sp,#88
 str r3,[r12,#12]
 ldr r3,[sp,#84]
 str r3,[r12,#16]
 add r3,r12,#20
 stmia r3,{r4-r11}
 ldr r3,[sp,#96] /* capture.epoch LOW32; high32 remains original stack arg. */
 str r3,[r12,#120]
 ldr r3,[r12,#8]
 ldr r2,=R2_WITNESS_COMMIT
 eors r3,r3,r2
 str r3,[r12,#124]
 dmb
 ldr r3,=(R2_WITNESS_VERSION|1)
 str r3,[r12,#4]
 /* Only this byte update is masked. A concurrent STOP wins and cannot be
  * undone by publishing a stale ownership bit after it revoked the witness. */
 mrs r0,PRIMASK
 cpsid i
 ldr r2,=r2_gm7_witness_state
 ldrb r3,[r2]
 tst r3,#R2_WITNESS_GM
 beq 2f
 orr r3,r3,#R2_WITNESS_STARTED
 strb r3,[r2]
2:
 msr PRIMASK,r0
 /* r0..r3 are restored after forwarding; r4..r11 are untouched. */
 .set arg_offset,0
 .rept 8
 ldr r3,[sp,#(88+arg_offset)]
 str r3,[sp,#arg_offset]
 .set arg_offset,arg_offset+4
 .endr
 add r12,sp,#36
 ldmia r12,{r0-r3}
 bl gm7_source_snapshot_inner
 str r0,[sp,#32]
 ldr r12,=(r2_crash_record+R2_WITNESS_WORD*4)
 ldr r0,=(R2_WITNESS_VERSION|0x21)
 str r0,[r12,#4]
 add r0,r12,#52
 stmia r0,{r4-r11}
 dmb
 ldr r0,=(R2_WITNESS_VERSION|2)
 str r0,[r12,#4]
 ldr r0,=(R2_WITNESS_VERSION|0x32)
 str r0,[r12,#4]
 .set slot_offset,0
 .rept 9
 ldr r0,[sp,#(52+slot_offset)]
 str r0,[r12,#(84+slot_offset)]
 .set slot_offset,slot_offset+4
 .endr
 dmb
 ldr r0,=(R2_WITNESS_VERSION|3)
 str r0,[r12,#4]
 movs r0,#0
 movs r3,#0
1:
 add r1,r12,#20
 ldr r1,[r1,r0]
 add r2,r12,#52
 ldr r2,[r2,r0]
 cmp r1,r2
 it ne
 orrne r3,r3,#R2_WITNESS_LIVE_MISMATCH
 add r2,r12,#84
 ldr r2,[r2,r0]
 cmp r1,r2
 it ne
 orrne r3,r3,#R2_WITNESS_SLOT_MISMATCH
 adds r0,r0,#4
 cmp r0,#32
 bne 1b
 ldr r0,[r12,#16]
 ldr r1,[r12,#116]
 cmp r0,r1
 it ne
 orrne r3,r3,#R2_WITNESS_LR_MISMATCH
 ldr r0,[r12,#12]
 add r1,sp,#88
 cmp r0,r1
 it ne
 orrne r3,r3,#R2_WITNESS_SP_MISMATCH
 ldr r0,=(R2_WITNESS_VERSION|3|R2_WITNESS_COMPARED)
 orrs r0,r0,r3
 str r0,[r12,#4]
 dmb
 cmp r3,#0
 bne gm7_snapshot_witness_fault
 ldr r0,[sp,#32]
 .cfi_remember_state
 add sp,sp,#52
 .cfi_def_cfa_offset 36
 pop {r4-r11,pc}
 .cfi_def_cfa_offset 0
gm7_snapshot_witness_fault:
 .cfi_restore_state
 /* Real detected boundary violation, not a command or injected bad value.
  * Existing UsageFault path captures original live registers and witness. */
 udf #188
 b gm7_snapshot_witness_fault
 .cfi_endproc
.size gm7_source_snapshot,.-gm7_source_snapshot
.ltorg
.global gm7_snapshot_witness_fault
.section .note.GNU-stack,"",%progbits
