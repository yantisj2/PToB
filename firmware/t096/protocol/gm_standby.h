/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "group_join.h"
#include "election_policy.h"
#define R2_GM5_STATUS_BYTES 164u
#define R2_GM5_PROTOCOL 6u
#define R2_GM5_CANDIDATES 3u
#define R2_GM5_HISTORY 3u
/* One generous control/recovery window; source/PT validity stays independent. */
#define R2_GM5_CONTROL_US UINT64_C(45000000)
#define R2_GM5_REPAIR_US R2_GM5_CONTROL_US
#define R2_GM5_CONTACT_US R2_GM5_CONTROL_US
#define R2_GM5_ACQUIRE_US R2_GM5_CONTROL_US
#define R2_GM5_STABLE_US UINT64_C(5000000)
#define R2_GM5_COORDINATION_US R2_GM5_CONTROL_US
/* Fast probes renew early; a larger control window must not slow source delivery. */
#define R2_GM5_PROBE_US UINT64_C(500000)
#define R2_GM5_RECORD_US (2 * R2_GM5_PROBE_US)
/* Original per-generation grant. An outstanding issuance retains its control
 * owner until acknowledgement or original expiry, even after phase timeout. */
#define R2_GM5_GRANT_US (2 * R2_GM5_CONTROL_US)
#define R2_GM5_RETIRE_GUARD_US UINT64_C(50000)
/* Discovery, initiation and one aggregate CONTROL GATT pass share this bound. */
#define R2_GM5_IO_US UINT64_C(1000000)
#define R2_GM5_STATUS_US UINT64_C(120000000)
enum r2_gm5_role {R2_GM5_COLD,R2_GM5_ACQUIRING,R2_GM5_HOT,R2_GM5_ACTIVE};
enum r2_gm5_reason {R2_GM5_NONE,R2_GM5_SELECTED,R2_GM5_SOURCE,R2_GM5_INHIBITED,
 R2_GM5_TRANSPORT,R2_GM5_MAP,R2_GM5_AUTH,R2_GM5_TIMEOUT,R2_GM5_RETIRED,
 R2_GM5_GRANT_EXPIRED,R2_GM5_TAKEOVER,R2_GM5_MERGE,R2_GM5_NO_CANDIDATE,
 R2_GM5_COMMITTED,R2_GM5_ROOT_CHANGED,R2_GM5_CONTACT};
struct r2_gm5_candidate {
 uint64_t identity,boot,at,until,cooldown,physical_at,physical_until,withdraw_created;
 uint64_t root,root_boot,selected,selected_boot,grant_until,attempt_until,negative_root;
 struct r2_clock_quality quality;
 uint32_t epoch,generation,source_age_ms,withdraw_epoch;uint16_t last_reason;uint8_t reported_reason;
 uint16_t failures,successes;uint8_t role;bool eligible,inhibited;
};
struct r2_gm5_transition {uint64_t at,peer;uint32_t generation;uint8_t role,reason;};
struct r2_gm5 {
 struct r2_gm5_candidate candidate[R2_GM5_CANDIDATES];
 struct r2_gm5_transition history[R2_GM5_HISTORY];
 uint64_t active,active_boot,active_until,selected,negative_root,negative_boot;
 uint64_t retired_root,retired_boot,retired_until;uint32_t negative_epoch,retired_epoch;
 uint64_t attempt_started,attempt_until,stable_since,next_attempt;
 uint64_t grant_until,issued_until,retire_until,next_poll,ready_at,round_until;
 uint32_t generation,grant_generation,transitions,last_reason;
 uint32_t attempts,failures,successes,status_tx,status_rx,control_tx,control_rx;
 uint8_t role;bool enabled,inhibited,bridge,repair_used,grant_pending,contacting;
};
/* Every COLD GM follows the ordinary timing stream as a passive leaf.
 * The existing bridge flag selects that receive path; it grants no HOT role. */
static inline bool r2_gm5_cold_listener(const struct r2_gm5 *g)
{return g->enabled&&g->role==R2_GM5_COLD;}
struct r2_network;
/* STATUS is accepted only inside the authenticated, still-live causal JOIN.
 * It is candidate inventory and negative incumbent evidence, never timing or
 * takeover permission. phase0 request,1 response,2 final confirmation. */
int r2_gm5_status_encode(struct r2_network *,const struct r2_join_exchange *,unsigned phase,uint8_t out[R2_GM5_STATUS_BYTES]);
int r2_gm5_status_receive(struct r2_network *,const struct r2_join_exchange *,unsigned phase,const uint8_t *,size_t,uint64_t now);
bool r2_gm5_wants_control(const struct r2_network *,uint64_t identity);
uint64_t r2_gm5_control_peer(const struct r2_network *);
bool r2_gm5_poll_due(const struct r2_network *,uint64_t now);
void r2_gm5_poll_completed(struct r2_network *,uint64_t now);
