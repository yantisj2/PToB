/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct rc_admission_scan {uint16_t interval,window;};
/* GAP units of 625 us. Every attempt gets a fresh non-continuous scan pattern
 * to avoid repeatedly sampling the same phase of a busy timing chain.
 * Non-continuous scanning retains the split LL's cancel-on-preemption path.
 * This never changes the owner's original 2s absolute deadline. */
static inline struct rc_admission_scan rc_admission_scan(uint64_t nonce){
 return (struct rc_admission_scan){.interval=(uint16_t)(80u+nonce%81u),
  .window=(uint16_t)(16u+(nonce/81u)%17u)};
}
/* Cold JOIN has several dependent ATT exchanges and the unchanged <100ms
 * uncertainty gate. At15ms,120ms proof rounds fail with a40ms source bound.
 * Use the public7.5ms minimum for this short authenticated bootstrap only.
 * Same-scope renewal keeps its existing 50ms connection interval. */
static inline unsigned rc_admission_interval(bool renewing){return renewing?40u:6u;}
/* G1 version1's passive-client bit is not a direct GM locator. Such a peer
 * can still be considered through its separately authenticated RC7 capability.
 * This rejects unsuitable discovery, never grants source/time authority. */
static inline bool rc_admission_g1_direct(uint8_t flags,bool connectable)
{return connectable&&(flags&63u)==1u&&!(flags&64u);}
