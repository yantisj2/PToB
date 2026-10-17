/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Extended discovery/capability events use625us GAP units. They carry no
 * precision observations. Keep the independent periodic train at62.5ms. */
#define RC_DISCOVERY_ADV_INTERVAL 160u /*100ms, before controller advDelay*/
/* Failed control work is not the precision stream. Spread retries, including
 * controller/API failures, instead of immediately opening another ACL. */
static inline uint64_t rc_admission_retry_us(unsigned attempt,uint64_t nonce){
 uint64_t base=attempt<=1?UINT64_C(2000000):attempt==2?UINT64_C(5000000):
  attempt==3?UINT64_C(10000000):UINT64_C(30000000);
 return base+nonce%(base/2+1);
}
/* Absolute start history, retained across warm retries and parent changes. */
static inline bool rc_admission_start_allowed(const uint64_t starts[2],uint64_t tick){
 if(starts[1]&&tick<starts[1])return false;
 if(starts[0]&&(tick<starts[0]||tick-starts[0]<UINT64_C(960000000)))return false;
 return !starts[1]||tick-starts[1]>=UINT64_C(320000000);
}
/* Connected links may complete teardown through their negotiated supervision
 * timeout. Add a finite callback margin. A create/cancel has no live ACL and
 * retains500ms; explicit profile/update shutdown separately retains500ms. */
static inline uint64_t rc_admission_drain_us(bool connected,unsigned supervision){
 return UINT64_C(500000)+(connected?(uint64_t)supervision*10000:0);
}
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
