/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "gm7_session.h"
#ifdef __cplusplus
extern "C" {
#endif
#define GM7_PEERS 8u
#define GM7_RETAINED 8u
#define GM7_DWELL_US UINT64_C(60000000)
#define GM7_QUIET_US UINT64_C(5000000)
#define GM7_BLACKOUT_US UINT64_C(2750000)
#define GM7_BLACKOUT_BOUND_US UINT64_C(3000000)
#define GM7_ARM_RETAIN_US UINT64_C(5000000)
#define GM7_PREPARE_US UINT64_C(5000000)
typedef uint64_t (*gm7_now_fn)(void *context);
typedef bool (*gm7_reservation_fence_fn)(void *context);
enum gm7_owner_role { GM7_COLD, GM7_ACTIVE, GM7_ARMED, GM7_WARM, GM7_RECEIVER };
enum gm7_reason {
  GM7_R_NONE,
  GM7_R_INITIAL,
  GM7_R_MERGE,
  GM7_R_SOURCE,
  GM7_R_NOMINATION,
  GM7_R_PREPARE,
  GM7_R_TAKEOVER,
  GM7_R_YIELD,
  GM7_R_WITHDRAW,
  GM7_R_TIMEBASE,
  GM7_R_RESOURCE,
  GM7_R_MISSED_BOUND,
  GM7_R_SCOPE,
  GM7_R_STOP,
  GM7_R_EXHAUSTED
};
enum gm7_blocker {
  GM7_B_SOURCE = 1,
  GM7_B_DWELL = 2,
  GM7_B_UTC = 4,
  GM7_B_CALENDAR = 8,
  GM7_B_MAP = 16,
  GM7_B_LOCK = 32,
  GM7_B_CORRECTION = 64,
  GM7_B_SCOPE = 128,
  GM7_B_PREPARE = 256,
  GM7_B_PHASE = 512,
  GM7_B_NOMINATION = 1024,
  GM7_B_UNCERTAINTY = 2048,
  GM7_B_RESOURCE = 4096
};
struct gm7_quality {
  uint8_t priority1, clock_class, accuracy, priority2;
  uint16_t variance;
};
struct gm7_scope {
  uint64_t birth, founder, term, active, boot;
  uint32_t timing_epoch, source_gen, capture_gen, train;
};
/* Original independent GNSS evidence and functional UTC, from the time owner.
 * calibrated_utc is deliberately absent; functional UTC grants no accuracy. */
struct gm7_source {
  uint64_t utc_us, sampled_us, until_us, pps_at;
  uint32_t source_gen, capture_gen, association_gen;
  struct gm7_quality quality;
  bool gnss_healthy, pps_healthy, utc_available, physical_ready, leap_ambiguous,
      commissioned_utc_association, measured_quality;
};
/* Physical matcher/fitter output only. A nonce or diagnostic cannot fill this.
 */
struct gm7_preparation {
  struct gm7_scope scope;
  uint32_t shadow_gen, correction_age_us, phase_enclosure_us,
      retained_enclosure_us;
  uint64_t yield_sequence, yield_after_us, yield_before_us;
  bool map_valid, fit_valid, locked, phase_available, retained_prediction,
      health_lost, yield_phase_valid;
};
/* Descriptive only: reported durations remain values at the sender sample,
 * never observer-estimated continuity or an authority input. */
struct gm7_reported_role {
  uint64_t sampled_mono_us, until;
  uint32_t hot_ms, active_ms, pps_ms;
  uint16_t blockers;
  uint8_t role, known;
};
struct gm7_peer {
  struct gm7_reported_role reported;
  struct gm7_session session;
  uint64_t identity, boot, locator_at, locator_first, retry_at, retry_attempt,
      original;
  uint16_t radio_refs, retry_offset;
  uint16_t flags;
  uint8_t address[6], kind;
  bool used, quarantined, locator_seen;
};
/* Floors and terminal negatives share one bounded ledger. No live ledger entry
 * is silently evicted to admit an unfamiliar lineage. */
struct gm7_retained {
  uint64_t birth, founder, max_term, term, active, boot;
  bool used, terminal;
};
struct gm7_event {
  struct gm7_packet packet;
  uint64_t first, deadline;
  uint32_t generation;
  uint8_t sent;
  bool valid, negative;
};
struct gm7_owner {
  gm7_now_fn clock_read;
  void *clock_context;
  gm7_reservation_fence_fn reservation_fence;
  void *reservation_context;
  struct gm7_requester requester;
  struct gm7_peer peer[GM7_PEERS];
  struct gm7_retained retained[GM7_RETAINED];
  struct gm7_source source;
  struct gm7_preparation preparation;
  struct gm7_scope scope, reserved_scope;
  uint64_t reserved_nominal, reserved_end, reserved_sequence,
      first_announce_start, first_announce_end;
  bool reserved;
  struct gm7_control selected;
  struct gm7_event event;
  uint64_t now, boot_at, sequence, response_sequence, role_sequence,
      gnss_sequence;
  uint64_t source_since, source_previous_until, pps_since, active_since,
      armed_since, prepared_since, quiet_since;
  uint64_t selected_original, selected_until, selected_source_until, nominee,
      nominee_boot, nominee_better_since, nominee_better_id,
      nominee_better_boot, quality_better_since, quality_better_id,
      quality_better_boot;
  uint64_t yield_deadline, remote_yield_sequence, remote_yield_after, last_utc,
      last_utc_at;
  uint32_t timing_epoch, train, shadow_gen, event_generation, transitions,
      duplicates, drops, late_windows, missed_bounds, overflow, reason;
  uint16_t blockers;
  uint8_t role;
  bool gm_capable, eligible, source_observing, pps_observing, quiet, armed,
      prepared, serving, failed, stopped, takeover_due, initial_due, withdrawn,
      yield_ready;
};
int gm7_quality_compare(struct gm7_quality, struct gm7_quality);
bool gm7_scope_same(struct gm7_scope, struct gm7_scope);
bool gm7_calendar_clear(uint64_t utc_us);
int gm7_owner_init(struct gm7_owner *, const struct r2_group_config *,
                   uint64_t identity, uint64_t boot, bool gm, uint64_t now);
/* All owner entry points fence every session, permission, nonce and positive
 * event on local monotonic regression. One serialized worker owns this state.
 */
/* Set once before use. B1 adapters MUST supply their documented monotonic
 * reader so response U is captured after MAC authentication inside receive. */
void gm7_owner_clock(struct gm7_owner *, gm7_now_fn, void *);
void gm7_owner_reservation_guard(struct gm7_owner *, gm7_reservation_fence_fn,
                                 void *);
int gm7_owner_receive(struct gm7_owner *, const uint8_t *ad, size_t,
                      const uint8_t address[6], uint64_t actual_rx,
                      uint64_t now);
void gm7_owner_update(struct gm7_owner *, const struct gm7_source *,
                      const struct gm7_preparation *, uint64_t now);
/* Only after the controller accepted the bounded opportunity; actual completion
 * remains separately required. A timeout callback is not RF completion. */
/* Reserve a future complete controller opportunity (at most one frame ahead).
 * Effective ACTIVE/output begins at nominal; intervening inputs still fence
 * the queued generation. The caller must recheck before submission/dispatch. */
int gm7_owner_reserve(struct gm7_owner *, uint64_t nominal,
                      uint64_t latest_complete, uint64_t now);
/* If any lineage-bearing AUX could have started, retain/terminal-fence the
 * provisional lineage and emit WITHDRAW. Unknown completion is exposure. */
void gm7_owner_reservation_failed(struct gm7_owner *, bool aux_exposed,
                                  uint64_t now);
void gm7_owner_reservation_cancel(struct gm7_owner *, uint64_t now);
int gm7_owner_reservation_commit(struct gm7_owner *, uint64_t actual_start,
                                 uint64_t actual_end, uint64_t now);
int gm7_owner_planned_control(struct gm7_owner *, struct gm7_packet *,
                              uint64_t now);
int gm7_owner_activate(struct gm7_owner *, uint64_t nominal,
                       uint64_t latest_complete, uint64_t now);
int gm7_owner_control(struct gm7_owner *, struct gm7_packet *, uint64_t now);
int gm7_owner_anchor(struct gm7_owner *, struct gm7_packet *,
                     const struct gm7_packet *request, uint64_t received,
                     uint64_t now);
void gm7_owner_withdraw(struct gm7_owner *, unsigned reason, uint64_t now);
/* Bind only the first primary opportunity. Negative TTL remains the original
 *1s; YIELD action remains its immutable original750ms deadline. */
int gm7_owner_event_align(struct gm7_owner *, uint64_t first_primary,
                          uint64_t now);
void gm7_owner_stop(struct gm7_owner *, uint64_t now);
int gm7_owner_request_peer(struct gm7_owner *, struct gm7_packet *,
                           uint64_t peer, uint64_t boot, uint64_t now,
                           uint64_t planned_tx, gm7_random_fn, void *);
int gm7_owner_next_request_at(struct gm7_owner *, struct gm7_packet *,
                              uint64_t now, uint64_t planned_tx, gm7_random_fn,
                              void *);
int gm7_owner_next_request(struct gm7_owner *, struct gm7_packet *,
                           uint64_t now, gm7_random_fn, void *);
int gm7_owner_peer_pin(struct gm7_owner *, uint64_t peer, uint64_t boot,
                       bool pin, uint64_t now);
/* Pure conditional not-before check for a future reserved primary. No due,
 * serving, clock or timer state changes; activate rechecks live inventory. */
bool gm7_owner_takeover_at(const struct gm7_owner *, uint64_t nominal,
                           uint64_t complete, uint64_t now);
bool gm7_owner_output(const struct gm7_owner *, uint64_t now);
/* Receiver-derived association stays separate from decoded sender token0. */
bool gm7_candidate_mesh_current(const struct gm7_peer *,
                                const struct gm7_scope *accepted_root,
                                uint64_t now);
#ifdef __cplusplus
}
#endif

bool gm7_peer_role(const struct gm7_owner *, unsigned peer, uint64_t now,
                   struct gm7_reported_role *, bool *mesh_current);
