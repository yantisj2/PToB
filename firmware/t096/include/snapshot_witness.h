/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Diagnostic-only ARM call-boundary witness. No clock or source authority. */
#ifndef R2_SNAPSHOT_WITNESS_H
#define R2_SNAPSHOT_WITNESS_H
#define R2_WITNESS_MAGIC 0x57534731
#define R2_WITNESS_VERSION 0x00010000
#define R2_WITNESS_COMMIT 0x57434d31
#define R2_WITNESS_WORD 62
#define R2_WITNESS_WORDS 32
#define R2_WITNESS_HANDOFF 1
#define R2_WITNESS_GM 2
#define R2_WITNESS_REQUIRED 3
#define R2_WITNESS_STARTED 4
#define R2_WITNESS_COMPARED 0x40
#define R2_WITNESS_LIVE_MISMATCH 0x100
#define R2_WITNESS_SLOT_MISMATCH 0x200
#define R2_WITNESS_LR_MISMATCH 0x400
#define R2_WITNESS_SP_MISMATCH 0x800
#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>
/* word1: version16, mismatch8..11, in-progress4..5, completed0..1.
 * bit6 means comparisons completed. Stage3 alone means views captured only.
 * Marker is invalidated before entry writes. Completed stages are committed
 * only after their entire register view. The fatal record CRC binds all bytes.
 * 0magic,1stage,2sequence,3entrySP,4entryLR,5..12entryR4..R11,
 * 13..20post-innerR4..R11,21..28savedR4..R11,29savedLR,
 * 30original capture epoch LOW32 only,31generation commit. */
static inline bool r2_witness_valid(const volatile uint32_t *p) {
  uint32_t stage=p[1],done=stage&3,inflight=(stage>>4)&3;
  return p[0]==R2_WITNESS_MAGIC && (stage&0xffff0000)==R2_WITNESS_VERSION &&
    !(stage&0x0000f08c) && done>=1 && (!inflight||inflight==done+1) &&
    (!(stage&R2_WITNESS_COMPARED)||(done==3&&!inflight)) &&
    ((stage&R2_WITNESS_COMPARED)||!(stage&0xf00)) &&
    p[2] && p[31]==(p[2]^R2_WITNESS_COMMIT);
}
#endif
#endif
