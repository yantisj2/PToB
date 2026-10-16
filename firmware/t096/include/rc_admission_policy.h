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
 * uncertainty gate. Request 15ms ACL events, rather than the previous 30ms.
 * Same-scope renewal keeps its existing 50ms connection interval. */
static inline unsigned rc_admission_interval(bool renewing){return renewing?40u:12u;}
