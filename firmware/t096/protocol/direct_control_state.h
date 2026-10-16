/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "control_codec.h"

/* Portable CONTROL-ONLY lab state. It never grants precision/publication.
 * Single serialized owner; no heap, ISR work or transport calls. The adapter
 * must authenticate MAC, silicon/address/link/challenge/boot/config/replay and
 * supply conservative clock bounds BEFORE input. RAM credentials are not a
 * durable autonomous provisioning or reboot-fence implementation. */
#define V12_DC_NONE 255u
#define V12_DC_LEASE_US UINT64_C(500000)
#define V12_DC_REPORT_US UINT64_C(100000)
#define V12_DC_RENEW_US UINT64_C(200000)
#define V12_DC_COARSE_MAX_AGE_US UINT64_C(2500000)
#define V12_DC_FLAG_COARSE_VALID 1u
#define V12_DC_FLAG_CONTROL_VIEW 2u
#define V12_DC_FLAG_RELATIVE_RATE 4u /* Config2: physical PPS relative-rate capability. */
enum v12_dc_kind { V12_DC_REPORT=1, V12_DC_PREPARE, V12_DC_READY,
  V12_DC_COMMIT, V12_DC_ACTIVE, V12_DC_STOP, V12_DC_STOPPED,
  V12_DC_SERVICE, V12_DC_SERVICE_ACK, V12_DC_HELLO };
enum v12_dc_role { V12_DC_CLIENT=0, V12_DC_FOLLOWER, V12_DC_STANDBY,
  V12_DC_CONTROL_GM };
enum v12_dc_phase { V12_DC_IDLE=0, V12_DC_PREPARING, V12_DC_COMMITTING,
  V12_DC_RUNNING, V12_DC_STOPPING, V12_DC_UNCERTAIN };
enum v12_dc_refusal { V12_DC_REF_NONE=0, V12_DC_REF_SCOPE,
  V12_DC_REF_NON_DIRECT, V12_DC_REF_OVERFLOW, V12_DC_REF_TIME,
  V12_DC_REF_AUTHORITY, V12_DC_REF_DECISION, V12_DC_REF_QUEUE,
  V12_DC_REF_CLOCK, V12_DC_REF_SOURCE };
struct v12_dc_times {
  uint64_t created_lower_us, created_upper_us;
  uint64_t evidence_lower_us, evidence_upper_us;
  uint64_t expires_lower_us, permit_lower_us;
};
struct v12_dc_coarse {
  uint64_t utc_second, evidence_us, valid_until_us;
  uint32_t generation;
  bool available, qualified; /* qualified=true is refused in this lab core. */
  bool relative_ready; /* Never absolute UTC/PPS or PTS qualification. */
};
struct v12_dc_service_tuple {
  uint64_t sequence,decision,second,evidence_us,expires_us,root_boot;
  uint32_t epoch;
  uint8_t root;
  bool issued;
};
struct v12_dc_member {
  uint64_t boot, identity, live_until_us, last_report_us, source_sequence;
  struct v12_dc_coarse coarse;
  uint8_t wire;
  bool occupied, connected, authenticated;
  /* Four bounded outstanding publicly accepted SERVICE tuples. ACK metadata is
   * not a time grant; no ACK can refresh this original expiry. */
  struct v12_dc_service_tuple service[4];
};
struct v12_dc_outgoing {
  struct v12_control_record record;
  uint64_t recipient_boot;
  uint8_t recipient;
};
struct v12_dc_snapshot {
  uint64_t decision, retired_decision, role_until_us, view_until_us;
  uint64_t root_boot, last_now_us;
  struct v12_dc_coarse coarse;
  uint32_t config_epoch, accepted, refused, queue_drops, queue_expired;
  uint32_t prepares, ready_acks, commits, active_acks, stops, stopped_acks;
  uint32_t elections, lease_expired;
  uint8_t self_wire, coordinator_wire, elected_root, hot_standby;
  uint8_t role, phase, members, queued, last_refusal;
  bool coordinator, running, connected, authenticated, uncertain;
  bool coarse_control_view, precision_permitted; /* always false precision */
  bool functional_service, serving;
  /* Local elected/source-ready GM does not require an external listener.
   * serving retains its separate authenticated recipient-delivery meaning. */
  bool local_gm_ready;
  bool relative_precision_permitted;
  uint8_t service_peer;
  uint64_t service_until_us;
  uint32_t service_receipts,service_acks;
};
struct v12_direct_control_state {
  struct v12_dc_member members[V12_CONTROL_MAX_GMS];
  struct v12_dc_outgoing outgoing[V12_CONTROL_QUEUE_LIMIT];
  struct v12_dc_coarse local, view;
  uint64_t boot, now_us, decision, active_decision, pending_decision;
  uint64_t retired_decision, sequence, standby_decision, standby_ready_until_us;
  uint64_t active_root_boot, pending_root_boot;
  uint64_t active_until_us, pending_until_us, fence_until_us;
  uint64_t next_report_us, next_renew_us, view_until_us, view_root_boot, view_decision;
  uint32_t config_epoch, active_source_epoch, pending_source_epoch;
  uint32_t accepted, refused, queue_drops, queue_expired;
  uint32_t prepares, ready_acks, commits, active_acks, stops, stopped_acks;
  uint32_t elections, lease_expired;
  uint8_t self_wire, coordinator_wire, active_root, pending_root, standby;
  uint8_t phase, last_refusal, queue_head, queue_count;
  bool running, uncertain, service_relative;
  uint64_t service_received_until_us,service_acked_until_us,service_decision;
  uint64_t next_service_us,service_peer_boot;
  uint32_t service_source_epoch,service_receipts,service_acks;
  uint8_t service_peer;
};

int v12_dc_init(struct v12_direct_control_state *s, uint8_t self_wire,
                uint64_t boot, uint8_t coordinator_wire, uint32_t config_epoch);
/* direct=false/overflow refuses GM participation. A nonzero boot is a trusted
 * provisioned scope: retain it while disconnected/unverified for bootstrap.
 * Only initial REPORT may leave a connected unverified link; that does not
 * admit a member or grant a role. boot=0 with disconnected forgets the scope.
 * Repeated unchanged notifications do NOT renew membership; accepted REPORTs do. */
int v12_dc_set_member(struct v12_direct_control_state *s, uint8_t peer_wire,
                      uint64_t peer_boot, bool direct, bool connected,
                      bool authenticated, uint64_t now_us);
/* Provisioned stable silicon identity for deterministic ties (self included).
 * No identity is learned from unauthenticated discovery. Until set, a stable
 * roster index is the lab fallback tie, never a hard assigned role. */
int v12_dc_identity(struct v12_direct_control_state *s, uint8_t wire,
                    uint64_t stable_identity);
int v12_dc_source(struct v12_direct_control_state *s,
                  const struct v12_dc_coarse *source, uint64_t now_us);
int v12_dc_input(struct v12_direct_control_state *s,
                 const struct v12_control_record *record,
                 const struct v12_dc_times *mapped, uint64_t reception_us);
int v12_dc_tick(struct v12_direct_control_state *s, uint64_t now_us);
/* Adapter fills the authenticated link challenge and signs once. Core sequence,
 * original local creation/expiry and decision remain immutable on retry. Pop
 * rejects expired/retired records; popping is not a transport/application ACK. */
bool v12_dc_pop(struct v12_direct_control_state *s, uint64_t now_us,
                struct v12_dc_outgoing *out);
/* Leave a live FIFO head owned by the core until a transport byte slot accepts
 * it. Busy transport does not consume, reorder, or renew the original record. */
bool v12_dc_peek(struct v12_direct_control_state *s,uint64_t now_us,
                 struct v12_dc_outgoing *out);
/* Skip a blocked recipient, preserving FIFO order within every recipient and
 * all original bytes/deadlines. The same recipient is popped only after actual
 * public transport submission. SERVICE ledger exhaustion blocks its peer only. */
bool v12_dc_peek_ready(struct v12_direct_control_state *s,uint64_t now_us,
                       uint8_t available,struct v12_dc_outgoing *out);
bool v12_dc_pop_recipient(struct v12_direct_control_state *s,uint64_t now_us,
                          uint8_t peer,struct v12_dc_outgoing *out);
/* Serialized owner's late transport admission, no lifetime renewal or mutation. */
bool v12_dc_outgoing_live(const struct v12_direct_control_state *s,
                          const struct v12_control_record *record,uint64_t now_us);
bool v12_dc_service_has_slot(const struct v12_direct_control_state *s,
  const struct v12_control_record *record,uint64_t now_us);
int v12_dc_service_submitted(struct v12_direct_control_state *s,
  const struct v12_control_record *record,uint64_t now_us);
void v12_dc_snapshot(const struct v12_direct_control_state *s,
                      struct v12_dc_snapshot *out);
/* Stop queues bounded STOP/STOPPED where possible, withdraws all local roles
 * immediately and never depends on queue success. Re-init is a new session. */
int v12_dc_stop(struct v12_direct_control_state *s, uint64_t now_us);
