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
static inline uint64_t rc_admission_warm_retry_us(uint64_t nonce)
{return UINT64_C(20000000)+nonce%UINT64_C(20000001);}
/* A GM binding lasts at most360s. Starting renewal at300s leaves too little
 * time for two failed bounded exchanges under the rolling two-start budget.
 * Reserve two minutes for retries; this requests fresh authentication earlier
 * and never extends an existing binding or grants source/precision authority.
 * Units:16MHz. */
static inline uint64_t rc_admission_gm_renew_ticks(void)
{return UINT64_C(3840000000);}
/* Relay authentication has the same360s ceiling and rolling start budget.
 * Reserve two minutes there too; earlier authentication grants no time. */
static inline uint64_t rc_admission_relay_renew_ticks(void)
{return UINT64_C(240000000)*16;}
/* Warm same-parent RC7 renewal only. Reserve the original2s exchange plus1s
 * for scheduling/cleanup, without extending either deadline. The independent
 * two-start/60s and20s minimum start spacing remain the final start gate.
 * After expiry seek fresh authentication at the earliest allowed start; an
 * expired context never becomes usable because a retry is scheduled. */
static inline uint64_t rc_admission_relay_retry_us(uint64_t nonce,uint64_t tick,
                                                   uint64_t context_until){
 uint64_t delay=rc_admission_warm_retry_us(nonce);
 if(context_until<=tick)return 0;
 uint64_t remaining=(context_until-tick)/16;
 if(remaining<=UINT64_C(3000000))return 0;
 uint64_t latest=remaining-UINT64_C(3000000);
 return delay<latest?delay:latest;
}
/* Fresh admitted count0 originals identify an unavailable relay source, not a
 * broken authentication context. Preserve scarce NEW recovery starts while
 * that stream is present. Nominal240s renewal and original expiry always win;
 * startup, lost PA and stale/missing originals never wait. This scheduling
 * hint grants no time, source, output or controller-lifetime permission. */
static inline bool rc_admission_source_wait(bool warm_relay,bool pa,bool bound,
    uint64_t admitted,uint64_t context_until,uint64_t count0_rx,uint64_t tick){
 return warm_relay&&pa&&bound&&count0_rx&&count0_rx!=UINT64_MAX&&tick>=count0_rx&&
  tick-count0_rx<UINT64_C(8000000)&&tick>=admitted&&tick<context_until&&
  tick-admitted<rc_admission_relay_renew_ticks();
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
/* GM JOIN and renewal retain the250ms reply-age gate. Sparse50ms ACL events
 * can exhaust it across dependent exchanges even while PA timing is healthy.
 * Use the existing7.5ms cold cadence for all direct-GM transactions; relay
 * renewals retain50ms. Precision preempts control and original bounds remain. */
static inline unsigned rc_admission_interval(bool renewing,bool relay)
{return renewing&&relay?40u:6u;}
/* G1 version1's passive-client bit is not a direct GM locator. Such a peer
 * can still be considered through its separately authenticated RC7 capability.
 * This rejects unsuitable discovery, never grants source/time authority. */
static inline bool rc_admission_g1_direct(uint8_t flags,bool connectable)
{return connectable&&(flags&63u)==1u&&!(flags&64u);}
