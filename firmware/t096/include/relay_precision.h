/* SPDX-License-Identifier: Apache-2.0
 * Native2 causal one-relay engine. No controller calls, ISR work or UTC claim. */
#pragma once
#include "radio_precision.h"
#include "durability.h"
#include "../protocol/relay_codec.h"
#include "../protocol/group_precision.h"
#define R2_RELAY_RETIRED 16u
#define R2_RELAY_MAPS 8u
#define R2_RELAY_FRAME_NS UINT64_C(62500000)
#define R2_RELAY_SLOT_NS UINT64_C(15625000)
/* This ONE-relay profile admits the slot1 target inside its nearest cell.
 * It does not prove separation of multiple relays at opposite cell edges.
 * These are future admission criteria, not retrospective preflight scoring.
 * Measured full RX2655+prepare1495+250ms widening628+tick31+capture1=4810us,
 * below the7812.5us half-cell and separated from root and next root event.
 * Actual reservations and every freshly mapped TX phase must still be checked.
 * Stock controller arbitrates GATT; losses remain losses. No exact lock claim. */
#define R2_RELAY_GUARD_NS UINT64_C(4810000)
#define R2_RELAY_PHASE_MIN_NS UINT64_C(7812500)
#define R2_RELAY_PHASE_MAX_NS UINT64_C(23437500)
struct r2_relay_map_record {
  r2_servo_map_t map;
  struct r2_relay_origin origin;
  uint64_t accepted;
  uint32_t bound;
  bool used;
  uint8_t proof[32];
};
struct r2_relay_seen {
  uint64_t arrived;
  uint32_t publication,generation;
  uint16_t event;
  uint8_t channel;
  bool used;
};
struct r2_relay_precision {
 bool v14;
 struct r2_hold_bound hold_bound;uint32_t hold_horizon_us,hold_budget_ns;
 bool passive_receiver; /* Local display only; forbids relay TX preparation. */
 uint32_t experimental_fresh_us; /* Rev2 functional cutoff; UNKNOWN accuracy. */
 struct r2_recovery recovery;
 struct r2_group_config group;uint64_t identities[4];
  struct r2_radio_precision upstream;
  struct r2_relay_origin origin;
  struct r2_relay_origin published_origin;
  uint64_t published_until_tick;
  bool published_origin_valid; /* Authenticated envelope identity, not map readiness. */
  struct r2_relay_route remote;
  struct r2_relay_correction sent[2];
  struct r2_relay_origin sent_origin[2];
  struct r2_relay_map_record map[R2_RELAY_MAPS];
  struct r2_relay_seen seen[R2_RADIO_PENDING];
  uint8_t proof[32];
  uint8_t packet_proof[32];
  uint64_t boot,epoch,origin_highwater,frame_ns,revision,root_boot,origin_until_tick;
  uint64_t retired_root[R2_RELAY_RETIRED],retired_sender[R2_RELAY_RETIRED];
  uint32_t source_epoch,train,map_generation,publication,bound;
  uint32_t rejected,duplicates,bootstrap,scope_changes,phase_rejected,tx_corrections;
  unsigned self,root,relay,parent,sent_count,map_head,retired_root_count,retired_sender_count;
  uint64_t last_tx_tick,last_root_ns;
  int64_t phase_ns;
  bool origin_valid,phase_admitted,failed;
};
void r2_relay_precision_init(struct r2_relay_precision *s,unsigned self,
  unsigned root,unsigned relay,uint64_t boot,uint64_t epoch);
/* Clear causal timing on owner withdrawal, preserving retired boot fences. */
void r2_relay_precision_withdraw(struct r2_relay_precision *s);
/* Quarantine downstream timing on a fresh-map gap without resetting an
 * unchanged authenticated upstream estimator. A restart still needs a new
 * local train and fresh actual TX admission. Old source deadlines remain. */
void r2_relay_precision_downstream_withdraw(struct r2_relay_precision *s);
/* Public sync recreation clears pending captures/corrections, retaining a
 * still-permitted delivered map on the unchanged local counter/source scope. */
void r2_relay_precision_pause(struct r2_relay_precision *s);
/* A new local train is allocated before any PA restart, including admission
 * retry. Zero/wrap refuses service. It cannot reset upstream authority. */
bool r2_relay_precision_train(struct r2_relay_precision *s,uint64_t epoch);
bool r2_relay_precision_rx(struct r2_relay_precision *s,
  const struct r2_relay_packet *p,const uint8_t proof[32],struct r2_capture rx,
  uint16_t event,uint8_t channel,struct r2_capture available,uint64_t now_us,
  int64_t trusted_coarse_utc_ns);
/* Read the actual delivered map; never source the selected clock from local
 * coarse GNSS. Coarse input above is only a replay/freshness check. */
bool r2_relay_precision_read(const struct r2_relay_precision *s,
  struct r2_capture now,uint64_t now_us,int64_t *root_ns,int64_t *utc_ns);
/* Conservative software-snapshot lifetime, clipped by the same delivered
 * map age and immutable source expiry using the actual affine projection. */
uint32_t r2_relay_precision_valid_for(const struct r2_relay_precision *s,
  struct r2_capture now,uint64_t now_us,uint32_t maximum_us);
bool r2_relay_precision_selected_origin(const struct r2_relay_precision *s,
  struct r2_capture now,uint64_t now_us,struct r2_relay_origin *out);
/* Create a root bootstrap/fresh origin from actual source projection. The
 * revision is native64 PPS revision, NOT the stable32 wire map incarnation. */
bool r2_relay_precision_root(struct r2_relay_precision *s,struct r2_capture now,
  uint32_t source_epoch,uint64_t relative_ns,uint64_t expires_ns,int64_t utc_ns,
  uint64_t revision,const uint8_t root_keys[4][16]);
/* Fixed-size publication transaction; no estimator copy or mutation in prepare.
 * Train is reserved only by commit, before controller calls which may emit it. */
struct r2_relay_prepared {
 struct r2_relay_packet packet;
 uint8_t proof[32];
 uint64_t valid_until_tick, published_until_tick;
 uint32_t previous_train,previous_publication,previous_generation;
 bool new_train,mapped;
};
bool r2_relay_precision_prepare(const struct r2_relay_precision *s,
 struct r2_capture now,uint64_t us,bool new_train,struct r2_relay_prepared *out);
bool r2_relay_precision_commit(struct r2_relay_precision *s,
 const struct r2_relay_prepared *p,struct r2_capture now);
/* Prepare count0 acquisition until an actual TX phase is admitted. */
bool r2_relay_precision_packet(struct r2_relay_precision *s,struct r2_capture now,
  uint64_t now_us,struct r2_relay_packet *out);
/* Exact captured TX PDU. Root projection is supplied from the source history;
 * relay uses a map which was delivered BEFORE the actual physical TX. */
bool r2_relay_precision_tx(struct r2_relay_precision *s,
  const struct r2_relay_packet *p,struct r2_capture tx,uint16_t event,uint8_t channel,
  uint64_t source_ns,uint64_t source_revision,uint64_t source_expiry);
void r2_relay_precision_view(const struct r2_relay_precision *s,
  struct r2_capture now,uint64_t now_us,struct r2_radio_precision_view *out);

/* Public start requests are hints; actual captured TX still admits phase. */
bool r2_relay_phase_retry(uint64_t now_us,uint64_t requested_us,int64_t phase_ns,uint32_t *delay_us);
bool r2_relay_precision_shadow_map(const struct r2_relay_precision *,struct r2_capture,uint64_t,r2_servo_map_t *,struct r2_relay_origin *,uint64_t *);
