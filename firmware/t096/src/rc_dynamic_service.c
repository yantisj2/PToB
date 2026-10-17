/* SPDX-License-Identifier: Apache-2.0
 * Dynamic RC7 profile. Separate reusable controller owner; no GM election.
 * TIMER3 and the native observation adapter supply every timing capture.
 */
#include "ble_service.h"
#include "ble_transport.h"
#include "capture.h"
#include "core.h"
#include "link_quality.h"
#include "radio_audit.h"
#include "radio_capture.h"
#include "rc_boundary.h"
#include "rc_direct_calibration.h"
#include "rc_dynamic.h"
#include "rc_field_policy.h"
#include "rc_gm_adapter.h"
#include "rc_admission_policy.h"
#include "rc_shadow.h"
#include "rc_slots.h"
#include "rc_stream_quality.h"
#include "rc_unicast.h"
#ifdef R2_USB_SOURCE_STREAM
#include "usb_source.h"
#endif
#include "time_service.h"
#include "timing_events.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/atomic.h>
static struct rc5_routes routes;
static struct rc7_receiver receiver;
#define timing receiver.clock
static struct rc7_path upstream_path, publication_path;
static bool transmitter, exclude_direct;
static uint64_t self_identity, forced_parent;
static uint64_t local_boot;
static bool stop(void);
static void slot_hint_update(void);
static uint64_t last_context_progress;
static struct r2_link_quality delivery;
static struct rc5_rate_fit fit;
static uint32_t fit_rms = RC5_UNKNOWN;
static bool fit_rms_valid;
static struct rc5_clock local_clock;
static struct r2_group_config group;
static struct r2_service_status status;
static struct r2_identity identity;
static unsigned node, parent_index;
static int32_t direct_phase_bias;
static uint32_t capture_epoch;
static uint64_t rebind_parent_boot, rebind_root_boot;
static uint32_t rebind_root_epoch, rebind_capture_epoch;
static uint64_t fit_parent_boot, fit_root_boot;
static uint32_t fit_parent_epoch, fit_root_epoch;
static struct rc7_packet published;
static struct rc5_origin current_origin;
static uint8_t current_proof[16];
static struct rc5_correction sent[2];
static unsigned sent_count, publication, train = 1, rx, tx, restarts,
                                         wire_rejected, stale_tx;
static uint32_t reservation_max, prepare_max, window_max;
static uint64_t publish_ticks, discipline_at, discipline_gap_max,
    usable_tx_from;
static uint32_t holdover_expiries, holdover_recoveries;
static bool holdover_expired;
static uint64_t last_rx;
/* Gap-only association evidence. These copy authenticated decoded tuples;
 * they neither queue packets nor affect receiver/publication decisions. */
static uint64_t delivery_packet_tuple,delivery_correction_tuple[2];
static uint64_t delivery_origin_issued,delivery_origin_expires;
static uint64_t delivery_discipline_tuple;
static uint32_t delivery_packet_count,delivery_discipline_rx;
static bool delivery_source_seen,delivery_source_lost;
static struct r2_radio_capture_stats delivery_capture_baseline;
static uint64_t delivery_tuple(uint32_t publication,uint16_t event,uint8_t channel) {
  return publication|((uint64_t)event<<32)|((uint64_t)channel<<48);
}
/* Explicit operator operating policy; not a measured precision bound. */
#define RC_FIELD_HOLD_TICKS UINT64_C(8000000)
/* Transition-only snapshots in this serialized owner. Values are cumulative
 * within its current logical scope; journal sequence/boot survives that scope.
 * RX counts authenticated selected-parent records, not RF packets. Absolute
 * ticks are copied, never subtracted here or treated as fresh permission. */
static void delivery_snapshot(unsigned transition,uint64_t tick) {
  struct r2_radio_capture_stats capture={0};
  r2_radio_capture_stats(&capture);
  unsigned pending=0;
  for(unsigned i=0;i<RC5_HISTORY;i++)pending+=timing.pending[i].used;
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,last_rx,1,rx,wire_rejected);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,timing.reference.completed_at,
                  2,timing.accepted,timing.rejected);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,timing.context_until,
                  3,timing.evicted,pending);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,tick,
                  4,capture.invalid,capture.overflow);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,current_origin.expires,
                  5,local_clock.generation,capture_epoch);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,delivery_discipline_tuple,
                  6,delivery_discipline_rx,rx);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,delivery_packet_tuple,
                  7,delivery_packet_count,0);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,delivery_correction_tuple[0],8,0,0);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,delivery_correction_tuple[1],9,0,0);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,delivery_origin_issued,10,0,0);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,delivery_origin_expires,11,0,0);
  if(transition!=1&&transition!=3)return; /* loss attribution, no recovery burst */
  const struct r2_radio_rx_stats *r=&capture.rx,*b=&delivery_capture_baseline.rx;
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,
      ((uint64_t)r->armed<<32)|b->armed,12,r->completed,b->completed);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,
      ((uint64_t)r->aborted<<32)|b->aborted,13,r->no_done,b->no_done);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,
      ((uint64_t)r->bad_crc<<32)|b->bad_crc,14,r->malformed,b->malformed);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,
      ((uint64_t)r->overflow<<32)|b->overflow,15,r->no_address,b->no_address);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,r->last_abort,16,
      capture.aborted,delivery_capture_baseline.aborted);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,r->last_arm,17,
      capture.completed,delivery_capture_baseline.completed);
  r2_timing_event(R2_TIMING_RC_DELIVERY,(int)transition,r->last_complete,18,
      r->last_arm_owner,r->last_abort_owner);
}
static void holdover_poll(uint64_t tick) {
  if (discipline_at && tick >= discipline_at &&
      tick - discipline_at >= RC_FIELD_HOLD_TICKS && !holdover_expired) {
    holdover_expired = true;
    ++holdover_expiries;
    r2_timing_event(R2_TIMING_HOLDOVER_EXPIRE,0,0,holdover_expiries,
                    (uint32_t)((tick-discipline_at)/16),0);
    delivery_snapshot(1,tick);
    sent_count = 0;
    usable_tx_from = tick;
  }
}
static bool start_prepared, phase_admitted;
static uint64_t phase_rx, start_delay, requested, restart_at;
static int64_t slot_target_ns, parent_slot_delta_ns, phase;
/* RF placement and numerical time have different references. A corrected,
 * authenticated parent ADDRESS capture anchors the local 62.5 ms RF frame.
 * The inherited RootEnvelope.frame remains exclusively a source-time field.
 * Both captures use this owner's epoch and 16 MHz hardware counter; neither
 * host arrival nor local GPS supplies phase. The ordinary map hold is also
 * the maximum age of a usable phase anchor. */
static bool parent_radio_phase(uint64_t tick) {
  if (!phase_rx || tick < phase_rx || tick - phase_rx >= RC_FIELD_HOLD_TICKS)
    return false;
  uint64_t relative = (tick - phase_rx) % UINT64_C(1000000);
  phase = (routes.node[parent_index].slot * INT64_C(15625000) +
           (int64_t)(relative * 125 / 2)) %
          RC5_FRAME_NS;
  return true;
}
static int64_t slot_phase_error(void) {
  const int64_t frame = (int64_t)RC5_FRAME_NS;
  int64_t error = slot_target_ns - phase;
  /* Closest occurrence of the legal slot in the periodic RF frame.
   * This does not permit a child slot at or below its parent's slot. */
  if (error > frame / 2)
    error -= frame;
  else if (error < -frame / 2)
    error += frame;
  return error;
}
static bool awaiting_first_tx, running, scanning, creating, parent_synced,
    found, parent_known;
static struct bt_le_per_adv_sync *sync;
static uint8_t sync_sid = 12;
static uint64_t sync_started;
static uint32_t sync_cancels;
static uint32_t sync_terms, sync_term_ms;
static uint8_t sync_term_reason;
static int sync_close_error;
static struct bt_le_ext_adv *pa;
static bt_addr_le_t parent;
static atomic_t fault, stop_requested, started;
static struct k_thread owner_thread;
K_THREAD_STACK_DEFINE(owner_stack, 8192);
K_MUTEX_DEFINE(state_lock);
static uint32_t transport_snapshot[13];
static uint32_t acquisition_snapshot[20];
static uint32_t cap_received, cap_authenticated, cap_usable;
static uint64_t mono_us(void) {
  return k_ticks_to_us_floor64(k_uptime_ticks());
}
static void check(bool ok, const char *why) {
  (void)why;
  if (!ok && atomic_cas(&fault, 0, -EIO)) {
    r2_timing_event(R2_TIMING_OWNER_FAULT,-EIO,self_identity,r2_crc32(why,strlen(why)),0,0);
#if defined(R2_RC_DYNAMIC_FIXTURE)
    printk("RC7_OWNER_FAULT self=%016llx reason=%s\n",
           (unsigned long long)self_identity, why);
#endif
  }
}

static struct rc_slots slot_state;
/* Advisory placement only; survives scope/owner recovery, never supplies time. */
static uint8_t retained_slot;
static bool slot_startup_checked;
static bool slot_selected, slot_change_requested, slot_scan_failed,
    slot_move_pending;
static uint64_t slot_move_until;
static uint64_t upstream_retry_at;
static uint32_t client_quiet_faults, slot_requests, upstream_recoveries;
static bool stop(void);
static bool parent_path_usable(const struct rc7_path *path) {
  if (rc7_path_empty(path))
    return true; /* Cold locator; never timing authority. */
  if (!rc7_path_valid(path) || !path->depth)
    return false;
  for (unsigned i = 0; i <= path->depth; ++i)
    if (path->node[i].id == self_identity)
      return false;
  return true;
}
static bool make_publication_path(void) {
  if (!transmitter || !routes.node[node].transmitter ||
      !rc7_path_valid(&upstream_path) || upstream_path.depth >= 2 ||
      !capture_epoch)
    return false;
  publication_path = upstream_path;
  publication_path.depth++;
  publication_path.node[publication_path.depth] =
      (struct rc7_node){.id = self_identity,
                        .boot = local_boot,
                        .train = train,
                        .epoch = capture_epoch,
                        .slot = routes.node[node].slot,
                        .role = 1};
  return rc7_path_valid(&publication_path);
}
static bool layout_path(const struct rc7_path *path) {
  if (!rc7_path_valid(path))
    return false;
  /* Relay capability is distinct from the current receiving role. A terminal
   * slot or depth-two parent still supplies valid client time; only extending
   * that path is forbidden. Preserve all authenticated upstream evidence. */
  bool extend =
      transmitter && path->depth < 2 && path->node[path->depth].slot < 3;
  struct rc5_routes next = {.count = path->depth + 2};
  for (unsigned i = 0; i <= path->depth; i++) {
    if (path->node[i].id == self_identity)
      return false;
    next.node[i] = (struct rc5_node){.id = path->node[i].id,
                                     .parent = i ? i - 1 : 0,
                                     .depth = i,
                                     .slot = path->node[i].slot,
                                     .transmitter = true};
  }
  unsigned own = next.count - 1;
  uint8_t slot = extend ? (uint8_t)(path->node[path->depth].slot + 1) : 255;
  bool retained = extend && retained_slot > path->node[path->depth].slot &&
      retained_slot <= 3;
  if (retained)
    slot = retained_slot;
  else if (extend && routes.node[node].slot > path->node[path->depth].slot &&
      routes.node[node].slot <= 3)
    slot = routes.node[node].slot;
  next.node[own] = (struct rc5_node){.id = self_identity,
                                     .parent = own - 1,
                                     .depth = path->depth + (extend ? 1 : 0),
                                     .slot = slot,
                                     .transmitter = extend};
  if (!rc5_routes_valid(&next))
    return false;
  bool changed = !rc7_path_same(&upstream_path, path);
  if (changed && running) {
    if (!stop())
      return false;
    sent_count = 0;
    if (train == UINT32_MAX) {
      check(false, "train exhausted");
      return false;
    }
    ++train;
    phase_admitted = false;
    start_prepared = false;
    restart_at = 0;
    slot_selected = false;
  }
  if (changed) {
    r2_shadow_normal_withdraw();
#ifdef R2_USB_SOURCE_STREAM
    r2_usb_source_withdraw();
#endif
    /* All ancestors belong to the map scope, including changes that leave
     * the final sender's boot/train tuple unchanged. */
    fit = (struct rc5_rate_fit){0};
    local_clock = (struct rc5_clock){.hold_ticks = RC_FIELD_HOLD_TICKS};
    current_origin = (struct rc5_origin){0};
    memset(current_proof, 0, sizeof current_proof);
    sent_count = 0;
    discipline_at = 0;
    phase_rx = 0;
    phase_admitted = holdover_expired = false;
    fit_rms_valid = false;
  }
  node = own;
  parent_index = own - 1;
  routes = next;
  upstream_path = *path;
  slot_target_ns = extend ? slot * 15625000 : 0;
  parent_slot_delta_ns =
      extend ? (slot - path->node[path->depth].slot) * 15625000 : 0;
  if (extend && !slot_selected) {
    rc_slots_init(&slot_state, slot, path->node[path->depth].slot);
    /* A normal first intermediary leaves one legal slot for the second.
     * The policy follows authenticated depth, never a clock name. */
    slot_state.preferred_max_slot = path->depth ? 3 : 2;
#if defined(R2_RC_DYNAMIC_FIXTURE)
    extern uint8_t r2_rc7_fixture_preferred_max_slot(uint64_t, uint8_t);
    slot_state.preferred_max_slot = r2_rc7_fixture_preferred_max_slot(
        self_identity, slot_state.preferred_max_slot);
#endif
    slot_selected = retained;
  }
  return true;
}
static bool set_local_slot(uint8_t slot) {
  struct rc5_routes next = routes;
  next.node[node].slot = slot;
  for (unsigned i = node + 1; i < next.count; i++)
    if (next.node[i].transmitter &&
        next.node[i].slot <= next.node[next.node[i].parent].slot)
      next.node[i].slot = next.node[next.node[i].parent].slot + 1;
  if (!rc5_routes_valid(&next))
    return false;
  routes = next;
  timing.routes = next;
  slot_target_ns = slot * 15625000;
  parent_slot_delta_ns = (slot - routes.node[parent_index].slot) * 15625000;
  return true;
}
static bool downstream_ready(uint64_t tick) {
  struct rc5_stamp st;
  return running && phase_admitted && sent_count && tx &&
         rc5_clock_capture(&local_clock, &timing, tick, &st) &&
         st.ns < current_origin.expires;
}
/* Discovery must not recruit children before this RC has a usable precision
 * clock. Do not require downstream publication/demand: that would deadlock
 * discovery of an otherwise synchronized idle relay. */
static bool precision_synced(uint64_t tick) {
  /* RX processing can install a map/discipline capture newer than the owner's
   * loop-start sample. Judge advertising at a fresh physical capture, never
   * mistake that stale sample for time reversal and stop a healthy train. */
  struct r2_capture current;
  if(!r2_capture_now(&current))return false;
  tick=current.tick;
  struct rc5_stamp st;
  return parent_known && parent_synced && fit.valid && !holdover_expired &&
      timing.bound && tick<timing.context_until &&
      discipline_at && tick >= discipline_at &&
      tick-discipline_at < RC_FIELD_HOLD_TICKS &&
      rc5_clock_capture(&local_clock,&timing,tick,&st) && st.pll_tracking &&
      st.ns < current_origin.expires;
}
static bool stop(void);
static void slot_hint_update(void);
struct candidate {
  bt_addr_le_t address;
  uint64_t id, seen, consumed, cooldown;
  int16_t rssi;
  bool relay, publishing;
};
static struct candidate candidates[8];
static uint64_t candidate_since, upstream_bad_since, parent_selected_at,
    parent_stage_at;
static bool upstream_usable, relay_fallback;
static bool parent_relay, parent_failed, parent_locked, parent_ineligible;
static struct rc_stream_history stream_history;
static struct rc_stream_sample stream_sample;
/* One bounded wait for each selected parent, its first PENDING reply and its
 * first authenticated admission. Repeated replies never restart these waits. */
static unsigned parent_admission;
static bool parent_acquiring(uint64_t now) {
  uint64_t allowance = parent_admission >= 2 && !parent_relay
      ? RC_STREAM_GM_ACQUIRE_US : RC_STREAM_ACQUIRE_US;
  return !parent_ineligible && now >= parent_selected_at && now >= parent_stage_at &&
      now - parent_selected_at < RC_STREAM_TRIAL_US &&
      now - parent_stage_at < allowance;
}
static void parent_wait(unsigned stage) {
  if (stage > parent_admission) {
    parent_admission = stage;
    parent_stage_at = mono_us();
    upstream_bad_since = mono_us();
    relay_fallback = false;
  }
}
/* RC-only, within this MCU boot. Reconfiguration/recovery never rearms an
 * already completed first admission; the separate GM backend is untouched. */
static bool startup_admitted, startup_attempted;
static void startup_complete(void) {
  parent_wait(2);
  if (!startup_admitted) {
    /* Discovery/negotiation was not a delivery failure of this new scope. */
    upstream_bad_since = mono_us();
    relay_fallback = false;
    startup_admitted = true;
  }
}
static void candidate_defer(uint64_t id, uint64_t now) {
  for (unsigned i = 0; i < 8; i++)
    if (candidates[i].id == id && (!parent_known || candidates[i].relay == parent_relay)) {
      candidates[i].consumed = candidates[i].seen;
      candidates[i].cooldown = now + 5000000;
    }
}
/* An authenticated JOIN from a follower proves this locator cannot currently
 * supply direct root timing. Do not spend a full clock-acquisition trial on
 * it or immediately retry the same strong G1 hint. This is advisory only;
 * its bounded cooldown allows a later legitimate promotion to be retried. */
static void candidate_nonroot(uint64_t id,uint64_t now){
 if(!parent_known||parent_relay||id!=routes.node[parent_index].id)return;
 parent_ineligible=true;
 for(unsigned i=0;i<8;i++)if(candidates[i].id==id&&!candidates[i].relay){
  candidates[i].consumed=candidates[i].seen;candidates[i].cooldown=now+60000000;
 }
}
static void candidate_seen(const bt_addr_le_t *address, uint64_t id, bool relay,
                           bool publishing, int8_t rssi, uint64_t now) {
  if (!id || id == self_identity || rssi > 0 || rssi < -127)
    return;
  if (forced_parent && (id != forced_parent || !relay))
    return;
#if defined(R2_RC_DYNAMIC_FIXTURE)
  extern bool r2_rc7_fixture_candidate(uint64_t, uint64_t);
  if (!r2_rc7_fixture_candidate(self_identity, id))
    return;
  extern int8_t r2_rc7_fixture_rssi(uint64_t, uint64_t, int8_t);
  rssi = r2_rc7_fixture_rssi(self_identity, id, rssi);
#endif
  unsigned at = 8;
  for (unsigned i = 0; i < 8; i++)
    if (candidates[i].id == id && candidates[i].relay == relay) {
      at = i;
      break;
    }
  if (at == 8) {
    at = 0;
    for (unsigned i = 0; i < 8; i++) {
      if (!candidates[i].id) {
        at = i;
        break;
      }
      if (candidates[i].seen < candidates[at].seen)
        at = i;
    }
    candidates[at] = (struct candidate){.id = id, .rssi = -127};
  }
  struct candidate *c = &candidates[at];
  c->rssi = c->seen ? (3 * c->rssi + rssi) / 4 : rssi;
  c->seen = now;
  c->relay = relay;
  c->publishing = publishing;
  c->address = *address;
  if (!candidate_since)
    candidate_since = now;
}
static void delivery_tick(uint64_t now, uint64_t tick) {
  struct rc5_stamp stamp;
  upstream_usable = parent_known && parent_synced && fit.valid &&
      rc5_clock_capture(&local_clock, &timing, tick, &stamp) &&
      stamp.pll_tracking && stamp.ns < current_origin.expires;
  rc_stream_observe(&stream_history, &stream_sample, &delivery,
      parent_known ? routes.node[parent_index].id : 0, parent_relay,
      upstream_usable, parent_synced, now);
  bool acquiring = parent_acquiring(now);
  if (upstream_usable) {
    parent_locked = true;
    if (!rc_stream_blocked(&stream_history, routes.node[parent_index].id, parent_relay, now))
      parent_failed = false;
    upstream_bad_since = 0;
    relay_fallback = !acquiring && stream_sample.unstable;
  } else {
    if (!upstream_bad_since || now < upstream_bad_since)
      upstream_bad_since = now;
    /* Audible packets/coarse JOINs cannot conceal missing clock lock. A brief
     * gap or a poor delivery sample cannot evict a still-locked parent. */
    relay_fallback = parent_ineligible || (!acquiring &&
        (stream_sample.unstable ||
         (!parent_locked && now >= parent_selected_at &&
          now - parent_selected_at >= RC_STREAM_TRIAL_US) ||
         (now >= upstream_bad_since &&
          now - upstream_bad_since >= RC_STREAM_LOSS_US)));
  }
}
#include "rc_dynamic_transport.inc"
/* Caller has drained the previous sync/publication and owns no transient ACL.
 * No phase/acquisition latch from that parent may inhibit the next admission. */
static void parent_handoff_reset(void) {
  parent_ineligible=false;
  lt_acquiring = false;
  lt_quiesce(false);
  lt_renew_at = lt_budget_until = lt_attempt_ready = lt_next = 0;
  lt_attempts = 0;
  gm_context = (struct rc5_gm_context){0};
  receiver = (struct rc7_receiver){0};
  delivery = (struct r2_link_quality){0};
  fit = (struct rc5_rate_fit){0};
  local_clock = (struct rc5_clock){.hold_ticks = RC_FIELD_HOLD_TICKS};
  upstream_path = publication_path = (struct rc7_path){0};
  current_origin = (struct rc5_origin){0};
  memset(current_proof, 0, sizeof current_proof);
  sent_count = 0;
  discipline_at = phase_rx = requested = restart_at = start_delay = 0;
  upstream_retry_at = 0;
  phase_admitted = start_prepared = awaiting_first_tx = holdover_expired = false;
  fit_rms = RC5_UNKNOWN;
  fit_rms_valid = false;
  rebind_parent_boot = rebind_root_boot = fit_parent_boot = fit_root_boot = 0;
  rebind_root_epoch = rebind_capture_epoch = fit_parent_epoch = fit_root_epoch = 0;
  r2_shadow_normal_withdraw();
#ifdef R2_USB_SOURCE_STREAM
  r2_usb_source_withdraw();
#endif
}
/* Same-parent renewal keeps an established RX sync and all authentication
 * fences. Only the drained local publisher's phase pause is released. A failed
 * decode is never source authority; elapsed corrected-time loss is sufficient
 * reason to request a fresh authenticated challenge, at most once per10s. */
static void parent_rebind_request(uint64_t tick) {
  if (!parent_known || creating || lt_conn || tick < upstream_retry_at)
    return;
  bool was_running = running;
  if (!stop())
    return;
  if (was_running) {
    if (train == UINT32_MAX) {
      check(false, "rebind train exhausted");
      return;
    }
    ++train;
  }
  lt_acquiring = false;
  lt_quiesce(false);
  sent_count = 0;
  phase_admitted = start_prepared = awaiting_first_tx = false;
  restart_at = 0;
  lt_renew_at = lt_budget_until = lt_attempt_ready = lt_next = 0;
  lt_attempts = 0;
  upstream_retry_at = tick + UINT64_C(160000000);
  ++upstream_recoveries;
}
static bool upstream_admitted(uint64_t tick) {
  return parent_relay ? timing.bound && tick < timing.context_until
      : gm_context.authenticated &&
          (tick / 16 < gm_context.time.until ||
           (parent_known && rc5_gm_reacquire(&gm_context, &timing,
               routes.node[parent_index].id, tick)));
}
static struct candidate *candidate_choose(uint64_t now, bool expired) {
  bool diagnostic = forced_parent || exclude_direct;
  bool startup = !startup_admitted && !startup_attempted && !diagnostic;
  if (!candidate_since || now < candidate_since ||
      now - candidate_since < (startup ? 3000000 : 40000))
    return NULL;
  /* Protect every chosen parent through admission and clock acquisition.
   * Its locator cooldown must not hand the next poll back to the failed GM. */
  if (parent_known && !relay_fallback && !diagnostic)
    return NULL;
  /* First-source collection tries a fresh GM before a publishing relay,
   * including when the older cold-loss fallback timer has already elapsed. */
  bool seek = diagnostic || relay_fallback || startup;
  bool prefer_relay = seek && !startup;
  /* A coarse admission context can outlive usable corrected time. Retention
   * requires live synchronization; an expired map must not pin that locator. */
  bool retained = upstream_usable && !relay_fallback && !diagnostic;
  (void)expired;
  if (parent_known && relay_fallback && !parent_failed && !diagnostic) {
    if (parent_admission >= 2 || parent_locked)
      rc_stream_failed(&stream_history, routes.node[parent_index].id, parent_relay, now);
    else
      rc_stream_admission_failed(&stream_history, routes.node[parent_index].id, parent_relay, now);
    parent_failed = true;
  }
  struct candidate *best = NULL;
  int best_score = INT32_MIN;
  for (unsigned i = 0; i < 8; i++) {
    struct candidate *c = &candidates[i];
    if (!c->id || now < c->seen || now - c->seen >= 200000 ||
        now < c->cooldown || c->seen <= c->consumed ||
        (!diagnostic && rc_stream_blocked(&stream_history, c->id, c->relay, now)) ||
        (exclude_direct && !c->relay) ||
        (forced_parent && (c->id != forced_parent || !c->relay)) ||
        (retained && c->id != routes.node[parent_index].id) ||
        (c->relay && (!seek || (startup && !c->publishing))))
      continue;
    bool preferred = c->relay == prefer_relay,
         old = best && best->relay == prefer_relay;
    int score = rc_stream_score(&stream_history, c->id, c->relay, c->rssi, now);
    if (c->relay && c->publishing)
      score += 48;
    /* Startup retains its GM-first collection. Afterward usable-time history
     * outranks an audible marginal GM; role is only a score tie break. */
    if (!best || (startup && preferred && !old) ||
        ((!startup || preferred == old) &&
         (score > best_score || (score == best_score &&
          ((preferred && !old) || (preferred == old && c->id < best->id)))))) {
      best = c;
      best_score = score;
    }
  }
  return best;
}

/* Bluetooth callbacks only enqueue bounded data. PSK checks run on owner. */
struct discovery {
  bt_addr_le_t address;
  uint16_t interval;
  uint8_t sid, len;
  int8_t rssi;
  bool connectable;
  uint8_t data[RC_DYNAMIC_CAPABILITY_BYTES];
};
K_MSGQ_DEFINE(discovery_queue, sizeof(struct discovery), 4, 4);
static bool scan_data(struct bt_data *d, void *user) {
  struct discovery *p = user;
  if (d->type == BT_DATA_MANUFACTURER_DATA &&
      (d->data_len == RC_DYNAMIC_CAPABILITY_BYTES || (d->data_len == 23))) {
    p->len = d->data_len;
    memcpy(p->data, d->data, p->len);
    return false;
  }
  return true;
}
static void scan(const struct bt_le_scan_recv_info *i,
                 struct net_buf_simple *ad) {
#if defined(R2_SWITCHABLE_FIELD)
  if (!r2_switch_rc_active())
    return;
#endif
  struct discovery d = {.address = *i->addr,
                        .interval = i->interval,
                        .sid = i->sid,
                        .rssi = i->rssi,
                        .connectable = (i->adv_props & BT_GAP_ADV_PROP_CONNECTABLE) != 0};
  bt_data_parse(ad, scan_data, &d);
  if (d.len || i->interval)
    (void)k_msgq_put(&discovery_queue, &d, K_NO_WAIT);
}
static struct bt_le_scan_cb scan_cb = {.recv = scan};
struct sync_event {
  struct bt_le_per_adv_sync *sync;
  bt_addr_le_t address;
  bool ended;
  uint8_t reason;
  uint32_t at_ms;
};
K_MSGQ_DEFINE(sync_queue, sizeof(struct sync_event), 4, 4);
static void ready(struct bt_le_per_adv_sync *s,
                  struct bt_le_per_adv_sync_synced_info *i) {
#if defined(R2_SWITCHABLE_FIELD)
  if (!r2_switch_rc_active())
    return;
#endif
  struct sync_event e = {.sync = s, .address = *i->addr};
  check(!k_msgq_put(&sync_queue, &e, K_NO_WAIT), "sync queue");
}
static void ended(struct bt_le_per_adv_sync *s,
                  const struct bt_le_per_adv_sync_term_info *i) {
#if defined(R2_SWITCHABLE_FIELD)
  if (!r2_switch_rc_active())
    return;
#endif
  struct sync_event e = {.sync = s, .ended = true, .reason = i->reason,
                         .at_ms = k_uptime_get_32()};
  check(!k_msgq_put(&sync_queue, &e, K_NO_WAIT), "sync end queue");
}
static struct bt_le_per_adv_sync_cb sync_cb = {.synced = ready, .term = ended};
static void sync_events(void) {
  struct sync_event e;
  while (!k_msgq_get(&sync_queue, &e, K_NO_WAIT)) {
    if (e.ended) {
      if (sync == e.sync) {
        if (sync_terms != UINT32_MAX)
          ++sync_terms;
        sync_term_reason = e.reason;
        sync_term_ms = e.at_ms;
        r2_timing_event(R2_TIMING_SYNC_TERM,e.reason,routes.node[parent_index].id,
                        parent_synced,sync_cancels,sync_terms);
        sync = NULL;
        sync_started = 0;
        creating = parent_synced = found = false;
      }
    } else if (creating && sync == e.sync && parent_known &&
               !bt_addr_le_cmp(&e.address, &parent)) {
      parent_synced = true;
      r2_timing_event(R2_TIMING_SYNC_READY,0,routes.node[parent_index].id,sync_sid,
                     sync_started?(uint32_t)((mono_us()-sync_started)/1000):0,0);
      sync_started = 0;
      creating = false;
    }
  }
}
/* Pinned host suppresses a local pending-cancel term callback. Retain the
 * address/object until its public lookup disappears; never reuse that pool
 * object on a successful command ACK alone. The caller owns one absolute
 * deadline, including an establishment racing the cancellation. */
static bool sync_close_until(uint64_t until) {
  struct bt_le_per_adv_sync *object = sync;
  if (!object)
    return true;
  bool established = parent_synced;
  int rc = bt_le_per_adv_sync_delete(object);
  r2_timing_event(R2_TIMING_SYNC_CANCEL,rc,routes.node[parent_index].id,established,
                  sync_started?(uint32_t)((mono_us()-sync_started)/1000):0,0);
  if (sync_cancels < UINT32_MAX)
    ++sync_cancels;
  if (rc && bt_le_per_adv_sync_lookup_addr(&parent, sync_sid) == object) {
    sync_close_error = rc;
    check(false, "sync cancel command");
    return false;
  }
  while (bt_le_per_adv_sync_lookup_addr(&parent, sync_sid) == object) {
    sync_events();
    if (mono_us() >= until) {
      sync_close_error = -ETIMEDOUT;
      r2_timing_event(R2_TIMING_SYNC_CANCEL,-ETIMEDOUT,routes.node[parent_index].id,established,0,0);
      check(false, "sync cancel deadline");
      return false;
    }
    if (!established && parent_synced) {
      established = true;
      rc = bt_le_per_adv_sync_delete(object);
      if (rc && bt_le_per_adv_sync_lookup_addr(&parent, sync_sid) == object) {
        sync_close_error = rc;
        check(false, "established sync cancel");
        return false;
      }
    } else
      k_sleep(K_MSEC(1));
  }
  sync_events(); /* Drain old queued callbacks before a pool object is reused. */
  sync = NULL;
  sync_started = 0;
  creating = parent_synced = found = false;
  if (mono_us() >= until) {
    sync_close_error = -ETIMEDOUT;
    return false;
  }
  return true;
}
static void discover(uint64_t tick) {
  sync_events();
  struct discovery d;
  while (!k_msgq_get(&discovery_queue, &d, K_NO_WAIT)) {
    uint64_t now = mono_us();
    if (d.len == 23 && d.data[0] == 'G' && d.data[1] == '1' &&
        rc_admission_g1_direct(d.data[2],d.connectable) &&
        r2_group_get(d.data + 3, 8) == group.id &&
        r2_group_get(d.data + 11, 4) == group.epoch && !exclude_direct)
      candidate_seen(&d.address, r2_group_get(d.data + 15, 8), false, false, d.rssi,
                     now);
    if (d.len == RC_DYNAMIC_CAPABILITY_BYTES) {
      if (cap_received != UINT32_MAX)
        ++cap_received;
      struct rc7_control c;
      if (!rc7_capability_decode(&c, d.data, d.len, &group) &&
          c.type == RC7_CAPABILITY) {
        if (cap_authenticated != UINT32_MAX)
          ++cap_authenticated;
        if (c.publishing && c.sender != self_identity)
          rc_slots_seen(&slot_state, c.path.node[c.path.depth].slot, d.rssi,
                        now);
        /* A legacy request may inspect a real same-root contender. Stable
         * identity order makes only the higher identity yield; weak/foreign
         * observations remain occupancy hints and cannot move a live train. */
        if (c.publishing && c.sender < self_identity &&
            c.path.node[c.path.depth].slot == slot_state.old_slot &&
            d.rssi != 127 && d.rssi >= RC_SLOT_CONFLICT_RSSI &&
            c.path.source_epoch == upstream_path.source_epoch &&
            c.path.node[0].id == upstream_path.node[0].id &&
            c.path.node[0].boot == upstream_path.node[0].boot &&
            c.path.node[0].train == upstream_path.node[0].train &&
            c.path.node[0].epoch == upstream_path.node[0].epoch)
          rc_slots_contender(&slot_state, c.sender, now);
        if (parent_path_usable(&c.path)) {
          if (cap_usable != UINT32_MAX)
            ++cap_usable;
          candidate_seen(&d.address, c.sender, true, c.publishing, d.rssi, now);
          /* This capability is authenticated independently of the old timing
           * path. Changed-path RC7 packets cannot pass the old path MAC merely
           * to reach the later capture-based scope-change detector. */
          if (parent_known && parent_relay && timing.bound &&
              c.sender == routes.node[parent_index].id &&
              !rc7_path_empty(&c.path) &&
              !rc7_path_same(&upstream_path, &c.path))
            parent_rebind_request(tick);
        }
      }
    }
    if (parent_known && d.sid == 12 && d.interval == 50 &&
        !bt_addr_le_cmp(&d.address, &parent))
      found = true;
  }
  delivery_tick(mono_us(), tick);
  bool expired = timing.bound && tick >= last_context_progress &&
                 tick - last_context_progress >= 32000000;
  if (!lt_conn && !creating &&
      (!parent_known ||
       relay_fallback ||
       (!parent_synced && (!timing.bound || expired)))) {
    struct candidate *c = candidate_choose(mono_us(), expired);
    if (c && (!parent_known || c->id != routes.node[parent_index].id ||
              (c->relay != parent_relay && (!timing.bound || expired)))) {
      if (sync) {
        if (!sync_close_until(mono_us() + 500000)) {
          check(false, "selected parent sync drain");
          return;
        }
      }
      bool was_running = running;
      if (!stop())
        return;
      if (was_running) {
        if (train == UINT32_MAX) {
          check(false, "handoff train exhausted");
          return;
        }
        ++train;
      }
      parent_handoff_reset();
      parent = c->address;
      parent_known = true;
      parent_synced = creating = found = false;
      upstream_bad_since = mono_us();
      parent_selected_at = parent_stage_at = upstream_bad_since;
      startup_attempted = true;
      upstream_usable = relay_fallback = false;
      parent_admission = 0;
      parent_failed = parent_locked = false;
      stream_sample = (struct rc_stream_sample){0};
      slot_selected = false;
      node = 1;
      parent_index = 0;
      routes = (struct rc5_routes){.count = 2,
                                   .node = {{.id = c->id, .transmitter = true},
                                            {.id = self_identity,
                                             .parent = 0,
                                             .slot = transmitter ? 1 : 255,
                                             .depth = transmitter ? 1 : 0,
                                             .transmitter = transmitter}}};
      parent_relay = c->relay;
      r2_timing_event(R2_TIMING_PARENT,0,c->id,c->relay,(uint32_t)(int32_t)c->rssi,0);
      candidate_defer(c->id, mono_us());
    }
  }
}
static void publish(void) {
  struct r2_capture now;
  if (!r2_capture_now(&now) || !make_publication_path())
    return;
  holdover_poll(now.tick);
  if (!current_origin.expires)
    return;
  if (publication == UINT32_MAX) {
    check(false, "publication overflow");
    return;
  }
  published = (struct rc7_packet){.origin = current_origin,
                                  .sender = self_identity,
                                  .depth = publication_path.depth,
                                  .slot = routes.node[node].slot,
                                  .boot = local_boot,
                                  .train = train,
                                  .publication = ++publication,
                                  .epoch = capture_epoch};
  memcpy(published.proof, current_proof, 16);
  struct rc5_stamp st;
  bool permitted = rc5_clock_capture(&local_clock, &timing, now.tick, &st) &&
                   st.ns < current_origin.expires;
  for (unsigned i = 0; permitted && !holdover_expired && i < sent_count; i++)
    published.correction[published.count++] = sent[i];
  uint8_t ad[190];
  if (rc7_encode(ad, &published, &publication_path, &group)) {
    check(false, "RC7 encode");
    return;
  }
  struct bt_data d = {
      .type = BT_DATA_MANUFACTURER_DATA, .data_len = 188, .data = ad + 2};
  check(!bt_le_per_adv_set_data(pa, &d, 1), "PA data");
  struct r2_capture done;
  if (r2_capture_now(&done) && done.tick >= now.tick)
    publish_ticks = done.tick - now.tick;
}
static void create(void) {
  struct bt_le_adv_param p = {.id = 0,
                              .sid = 12,
                              .options = BT_LE_ADV_OPT_EXT_ADV |
                                         BT_LE_ADV_OPT_USE_IDENTITY |
                                         BT_LE_ADV_OPT_NO_2M,
                              .interval_min = RC_DISCOVERY_ADV_INTERVAL,
                              .interval_max = RC_DISCOVERY_ADV_INTERVAL};
  struct bt_le_per_adv_param periodic = {.interval_min = 50,
                                         .interval_max = 50};
  int rc = bt_le_ext_adv_create(&p, NULL, &pa);
  if (rc) {
    check(false, "PA create");
    return;
  }
  check(!bt_le_per_adv_set_param(pa, &periodic), "PA interval");
  publish();
}
static void start(void) {
  struct r2_capture now;
  if (!pa || !r2_capture_now(&now) || !precision_synced(now.tick))
    return;
  requested = now.tick;
  awaiting_first_tx = true;
  int rc = 0;
#if defined(R2_RC_DYNAMIC_FIXTURE)
  extern int r2_rc7_fixture_start_fault(unsigned, uint64_t, bool);
  rc = r2_rc7_fixture_start_fault(node, now.tick, slot_move_pending);
#endif
  if (!rc)
    rc = bt_le_per_adv_start(pa);
  if (rc) {
    if (slot_move_pending && (rc == -EAGAIN || rc == -EBUSY)) {
      slot_move_until = now.tick;
      return;
    }
    check(false, "PA start");
    return;
  }
  running = true;
  check(!bt_le_ext_adv_start(pa, BT_LE_EXT_ADV_START_DEFAULT),
        "extended start");
  slot_hint_update();
}
static bool stop(void) {
  if (!running)
    return true;
  int extended = bt_le_ext_adv_stop(pa), periodic = bt_le_per_adv_stop(pa);
  bool closed = !extended && !periodic;
  check(closed, "PA owners stopped");
  if (closed) {
    running = false;
    start_prepared = false;
    slot_hint_update();
  }
  return closed;
}
static bool advertising_withdraw(uint64_t tick) {
  if(!running||precision_synced(tick))return true;
  if(!stop())return false;
  sent_count=0;
  phase_admitted=start_prepared=awaiting_first_tx=false;
  lt_acquiring=false;
  restart_at=0;
  if(train==UINT32_MAX){check(false,"unsynced train exhausted");return false;}
  ++train;
  return true;
}
/* Report inherited-source permission separately from the500ms correction
 * hold. Unknown projection/capture never becomes inferred expiry or authority. */
static void delivery_source_poll(const struct r2_uc_clock *offered,uint64_t tick) {
  if(!offered->valid||tick>=offered->until_tick||holdover_expired){
    delivery_source_seen=delivery_source_lost=false;return;
  }
  uint64_t ns,elapsed;
  if(!rc5_map_project(&offered->map,tick,&ns,&elapsed))return;
  bool live=ns<offered->origin.expires;
  if(!live&&delivery_source_seen&&!delivery_source_lost){
    delivery_source_lost=true;delivery_snapshot(3,tick);
  }else if(live){
    if(delivery_source_lost)delivery_snapshot(4,tick);
    delivery_source_seen=true;delivery_source_lost=false;
  }
}
static struct r2_uc_clock clock_offer(uint64_t tick) {
  struct r2_uc_clock offered = {.origin = current_origin,
                                .capture_epoch = capture_epoch,
                                .depth = upstream_path.depth + 1};
  if (local_clock.count && timing.bound && discipline_at) {
    offered.map = local_clock.map[(local_clock.head + 3) % 4];
    offered.until_tick = offered.map.effective + RC_FIELD_HOLD_TICKS;
    if (discipline_at + RC_FIELD_HOLD_TICKS < offered.until_tick)
      offered.until_tick = discipline_at + RC_FIELD_HOLD_TICKS;
    if (timing.context_until < offered.until_tick)
      offered.until_tick = timing.context_until;
    offered.valid = !holdover_expired && tick < offered.until_tick;
  }
  /* Path hashing is change-driven; routine polls only copy a small map. */
  static struct rc7_path previous;
  static uint8_t scope[16];
  if (!rc7_path_same(&previous, &upstream_path)) {
    previous = upstream_path;
    memset(scope, 0, sizeof scope);
    if (rc7_path_valid(&upstream_path))
      (void)rc7_path_mac(scope, &upstream_path, &group);
  }
  memcpy(offered.scope, scope, sizeof scope);
  return offered;
}
static void usb_offer_now(void) {
#ifdef R2_USB_SOURCE_STREAM
  if(!transmitter)return;
  struct r2_capture current;
  if(!r2_capture_now(&current)){r2_usb_source_withdraw();return;}
  struct r2_uc_clock offered=clock_offer(current.tick);
  r2_usb_source_publish(&offered);
  struct r2_capture after;
  if(r2_capture_now(&after)&&after.epoch==offered.capture_epoch)
    delivery_source_poll(&offered,after.tick);
#endif
}
static void discipline_commit(uint64_t accepted_at) {
  /* Only the receiver's accepted original/follow-up association can
   * advance phase. Its admission, source freshness and replay fences
   * have already passed; use the original's physical RX, not the
   * follow-up arrival or a predicted parent TX. */
  phase_rx = timing.reference.rx;
  last_context_progress = timing.reference.completed_at;
  if (discipline_at && accepted_at >= discipline_at &&
      accepted_at - discipline_at > discipline_gap_max)
    discipline_gap_max = accepted_at - discipline_at;
  if (holdover_expired) {
    ++holdover_recoveries;
    r2_timing_event(R2_TIMING_HOLDOVER_RECOVER,0,routes.node[parent_index].id,
                    holdover_recoveries,(uint32_t)((accepted_at-discipline_at)/16),0);
    delivery_snapshot(2,accepted_at);
    sent_count = 0;
    usable_tx_from = accepted_at;
  }
  discipline_at = accepted_at;
  holdover_expired = false;
  /* The clock is committed. Publish its ORIGINAL bounded permission
   * before audit, snapshots, GATT/control work or owner sleep. USB
   * still chooses its actual publication lower bound, never a past
   * SOF. Delaying this until loop end created millisecond gaps even
   * when the next discipline completed before the old500ms expiry. */
  usb_offer_now();
  delivery_discipline_tuple=delivery_packet_tuple;
  delivery_discipline_rx=rx;
  r2_radio_capture_stats(&delivery_capture_baseline);
}
static void captures(struct r2_capture now) {
  struct r2_radio_record r;
  while (r2_radio_capture_pop(&r)) {
    struct r2_capture physical;
    struct rc7_packet packet;
    if (!r.owner || r.length != RC5_BYTES ||
        !r2_capture_radio(r.raw, r.epoch, &physical)) {
      ++wire_rejected;
      continue;
    }
    bool decoded =
        !r.tx && !parent_relay
            ? gm_packet(&packet, r.ad, r.length, now.tick)
            : !rc7_decode(&packet, r.ad, r.length,
                          r.tx ? &publication_path : &upstream_path, &group);
    if (!decoded) {
      if (node && parent_index == 0 && !r.tx && !gm_context.authenticated)
        lt_renew_at = 0;
      ++wire_rejected;
      continue;
    }
#if defined(R2_RC_DYNAMIC_FIXTURE)
    extern bool r2_rc7_fixture_receive(uint64_t, uint64_t);
    if (!r.tx && !r2_rc7_fixture_receive(self_identity, packet.sender))
      continue;
#endif
    if (r.reservation_us > reservation_max)
      reservation_max = r.reservation_us;
    if (r.prepare_us > prepare_max)
      prepare_max = r.prepare_us;
    if (r.receive_window_us > window_max)
      window_max = r.receive_window_us;
    if (r.tx) {
      /* A drained controller can still leave queued records from its old train.
       * They must never calibrate this start or enter its correction history.
       */
      if (packet.sender != self_identity || packet.boot != local_boot ||
          packet.train != train || packet.epoch != capture_epoch || !running ||
          physical.tick < requested) {
        ++stale_tx;
        continue;
      }
      ++tx;
      r2_ble_timing_lamps(true, packet.count != 0, mono_us());
      if (r2_radio_audit_selected(r.event)) {
        struct rc5_stamp st = {0};
        bool mapped =
            rc5_clock_capture(&local_clock, &timing, physical.tick, &st);
        struct r2_radio_audit_record q = {.kind = R2_AUDIT_TX,
                                          .peer_boot = packet.boot,
                                          .root_boot = packet.origin.boot,
                                          .epoch = r.epoch,
                                          .peer_epoch = packet.epoch,
                                          .root_epoch = packet.origin.epoch,
                                          .train = packet.train,
                                          .publication = packet.publication,
                                          .event = r.event,
                                          .channel = r.channel,
                                          .address_raw = r.address_raw,
                                          .raw = r.raw,
                                          .tick = physical.tick,
                                          .available = now.tick,
                                          .ns = mapped ? st.ns : 0,
                                          .generation =
                                              mapped ? st.generation : 0,
                                          .crc = r2_crc32(r.ad, r.length)};
        r2_radio_audit_put(&q);
      }
      if (awaiting_first_tx) {
        start_delay = physical.tick - requested;
        awaiting_first_tx = false;
      }
      if (node) {
        bool known = parent_radio_phase(physical.tick);
        int64_t clearance = 15625000 / 2 - (int64_t)r.reservation_us * 1000,
                error = slot_phase_error();
        if (clearance <= 0) {
          check(false, "reservation exceeds slot");
          return;
        }
        phase_admitted = known && error <= clearance && error >= -clearance;
        if (known && !phase_admitted) {
          lt_acquiring = true;
          lt_quiesce(true);
          if (!stop())
            return;
          sent_count = 0;
          if (train == UINT32_MAX) {
            check(false, "train exhausted");
            return;
          }
          ++train;
          ++restarts;
          uint64_t base = physical.tick - start_delay +
                          (uint64_t)((int64_t)1000000 + error * 2 / 125);
          while (base < now.tick + 32000)
            base += 1000000;
          restart_at = base;
          continue;
        } else {
          lt_acquiring = false;
          lt_quiesce(false);
        }
      }
      struct rc5_correction c = {0};
      bool usable = false;
      {
        struct rc5_stamp st;
        usable = phase_admitted && !holdover_expired &&
                 physical.tick >= usable_tx_from &&
                 rc5_clock_capture(&local_clock, &timing, physical.tick, &st) &&
                 st.ns < packet.origin.expires;
        if (usable)
          c = (struct rc5_correction){.publication = packet.publication,
                                      .event = r.event,
                                      .channel = r.channel,
                                      .generation = st.generation,
                                      .age_us = st.age_us,
                                      .tx_ns = st.ns,
                                      .bound = st.bound};
      }
      if (!usable)
        sent_count = 0;
      else {
        if (sent_count)
          sent[1] = sent[0];
        sent[0] = c;
        if (sent_count < 2)
          ++sent_count;
      }
      if (running)
        publish();
    } else if (parent_known && packet.sender == routes.node[parent_index].id) {
      if (parent_index && packet.slot != routes.node[parent_index].slot) {
        lt_renew_at = 0;
        continue;
      }
      if (timing.bound &&
          (packet.boot != timing.parent_boot ||
           packet.origin.boot != timing.root_boot ||
           packet.origin.epoch != timing.root_epoch ||
           packet.epoch != timing.parent_epoch ||
           packet.train != timing.parent_train) &&
          (packet.boot != rebind_parent_boot ||
           packet.origin.boot != rebind_root_boot ||
           packet.origin.epoch != rebind_root_epoch ||
           packet.epoch != rebind_capture_epoch ||
           packet.train != timing.parent_train)) {
        /* New authenticated scope prompts a challenge; held output stays local.
         */
        rebind_parent_boot = packet.boot;
        rebind_root_boot = packet.origin.boot;
        rebind_root_epoch = packet.origin.epoch;
        rebind_capture_epoch = packet.epoch;
        lt_renew_at = 0;
      }
      ++rx;
      delivery_packet_tuple=delivery_tuple(packet.publication,r.event,r.channel);
      delivery_packet_count=packet.count;
      delivery_origin_issued=packet.origin.issued;
      delivery_origin_expires=packet.origin.expires;
      for(unsigned i=0;i<2;i++)delivery_correction_tuple[i]=i<packet.count?
        delivery_tuple(packet.correction[i].publication,packet.correction[i].event,
                       packet.correction[i].channel):0;
      r2_ble_timing_lamps(false, packet.count != 0, mono_us());
      last_rx = physical.tick;
      struct r2_capture available;
      if (!r2_capture_now(&available))
        continue;
      if (r2_radio_audit_selected(r.event)) {
        struct r2_radio_audit_record q = {.kind = R2_AUDIT_RX,
                                          .peer_boot = packet.boot,
                                          .root_boot = packet.origin.boot,
                                          .epoch = r.epoch,
                                          .peer_epoch = packet.epoch,
                                          .root_epoch = packet.origin.epoch,
                                          .train = packet.train,
                                          .publication = packet.publication,
                                          .event = r.event,
                                          .channel = r.channel,
                                          .address_raw = r.address_raw,
                                          .raw = r.raw,
                                          .tick = physical.tick,
                                          .available = available.tick,
                                          .crc = r2_crc32(r.ad, r.length)};
        r2_radio_audit_put(&q);
      }
      r2_link_quality_rx(&delivery, packet.train, r.event, mono_us());
      if (rc7_receive(&receiver, &packet, physical.tick, r.event, r.channel,
                      available.tick)) {
        if (fit_parent_boot != timing.parent_boot ||
            fit_root_boot != timing.root_boot ||
            fit_parent_epoch != timing.parent_epoch ||
            fit_root_epoch != timing.root_epoch) {
          memset(&fit, 0, sizeof fit);
          sent_count = 0;
          fit_parent_boot = timing.parent_boot;
          fit_root_boot = timing.root_boot;
          fit_parent_epoch = timing.parent_epoch;
          fit_root_epoch = timing.root_epoch;
        }
        current_origin = timing.reference.origin;
        memcpy(current_proof, timing.reference.proof, 16);
        (void)rc5_rate_add(&fit, &timing.reference);
        fit_rms_valid = rc5_rate_rms(&fit, &fit_rms);
        /* Hardware TX/RX ADDRESS captures: no simulator bias constant imported.
         */
        struct rc5_rate rate = {.ppb = fit.ppb,
                                .rx_tx_bias_ns = direct_phase_bias,
                                .bias_bound_ns = RC5_UNKNOWN,
                                .uncertainty_ppb = RC5_UNKNOWN,
                                .capture_ns = RC5_UNKNOWN,
                                .link_bound = RC5_UNKNOWN};
        holdover_poll(available.tick);
        struct r2_capture activation;
        bool activation_known = r2_capture_now(&activation);
#ifdef R2_USB_SOURCE_STREAM
        if(activation_known)r2_usb_source_begin(activation.tick,activation.epoch);
        else r2_usb_source_withdraw();
#endif
        bool disciplined =
            rc5_clock_discipline(&local_clock, &timing, &fit, &rate);
        if (disciplined) {
          discipline_commit(timing.reference.completed_at);
          if (activation_known)
            r2_shadow_map(&local_clock, &timing, activation.tick,
                          activation.epoch);
        }
        if (r2_radio_audit_selected(timing.reference.event)) {
          struct r2_capture done = {0};
          (void)r2_capture_now(&done);
          struct r2_radio_audit_record q = {
              .kind = R2_AUDIT_MATCH,
              .peer_boot = timing.parent_boot,
              .root_boot = timing.root_boot,
              .epoch = r.epoch,
              .peer_epoch = timing.parent_epoch,
              .root_epoch = timing.root_epoch,
              .train = timing.parent_train,
              .publication = timing.reference.publication,
              .event = timing.reference.event,
              .channel = timing.reference.channel,
              .address_raw = (uint32_t)timing.reference.rx,
              .raw = (uint32_t)timing.reference.rx,
              .tick = timing.reference.rx,
              .ns = timing.reference.parent_tx_ns,
              .generation = timing.reference.generation,
              .available = timing.reference.completed_at,
              .original_available = timing.reference.original_available,
              .followup = timing.reference.followup_rx,
              .activation_lo = activation_known ? activation.tick : 0,
              .activation_hi = done.epoch == r.epoch ? done.tick : 0,
              .disciplined = disciplined};
          r2_radio_audit_put(&q);
        }
      }
    }
  }
}
static void snapshot_publish(struct r2_capture now) {
  struct rc5_stamp stamp = {0};
  bool valid = node && rc5_clock_read(&local_clock, &timing, now.tick, &stamp);
  uint64_t utc = 0;
  if (valid) {
    if (stamp.ns > INT64_MAX)
      valid = false;
    else {
      int64_t ns = (int64_t)stamp.ns, off = current_origin.utc_offset;
      if ((off > 0 && ns > INT64_MAX - off) ||
          (off < 0 && (uint64_t)ns < (uint64_t)(-(off + 1)) + 1))
        valid = false;
      else
        utc = (uint64_t)(ns + off) / 1000;
    }
  }
  uint8_t lamps[8];
  r2_ble_lamps(lamps, mono_us());
  uint32_t advertising_flags=(lt_server?1u:0u)|(lt_paused?2u:0u)|
      (lt_advertising?4u:0u)|(precision_synced(now.tick)?8u:0u)|
      (running?16u:0u)|(r2_uc_advertising()?32u:0u);
  static uint32_t previous_advertising_flags=UINT32_MAX;
  if(advertising_flags!=previous_advertising_flags){
    previous_advertising_flags=advertising_flags;
    r2_timing_event(R2_TIMING_RC_ADVERTISING,0,self_identity,
        advertising_flags,RC_DISCOVERY_ADV_INTERVAL,0);
  }
  k_mutex_lock(&state_lock, K_FOREVER);
  memcpy(status.lamps, lamps, sizeof lamps);
  const uint32_t control[13] = {lt_requests,
                                lt_responses,
                                lt_bindings,
                                lt_initiations,
                                lt_disconnects,
                                lt_rejects,
                                lt_adv_recoveries,
                                lt_phase,
                                lt_conn != NULL,
                                advertising_flags,
                                cap_received,
                                cap_authenticated,
                                cap_usable};
  memcpy(transport_snapshot, control, sizeof control);
  uint64_t selected = parent_known ? routes.node[parent_index].id : 0;
  uint64_t age = sync_started && mono_us() >= sync_started
      ? (mono_us() - sync_started) / 1000 : 0;
  struct rc_stream_row *quality = rc_stream_find(&stream_history, selected, parent_relay, mono_us());
  unsigned entries = 0;
  for (unsigned i = 0; i < RC_STREAM_CAPACITY; i++) entries += stream_history.row[i].id != 0;
  uint32_t retry = quality && rc_stream_blocked(&stream_history, selected, parent_relay, mono_us())
      ? quality->retry_at - (uint32_t)(mono_us() / 1000000) : 0;
  const uint32_t acquisition[20] = {4, (uint32_t)selected,
      (uint32_t)(selected >> 32), age > UINT32_MAX ? UINT32_MAX : (uint32_t)age,
      sync_cancels, (uint32_t)sync_close_error, creating, parent_synced,
      upstream_admitted(now.tick), sync_terms, sync_term_reason, sync_term_ms,
      entries, sizeof stream_history + sizeof stream_sample,
      quality && quality->usable_samples ? quality->usable : UINT32_MAX,
      quality && quality->delivery_samples ? quality->delivery : UINT32_MAX,
      quality ? quality->usable_samples : 0, quality ? quality->delivery_samples : 0,
      quality ? quality->failures : 0, retry};
  memcpy(acquisition_snapshot, acquisition, sizeof acquisition);
  for (unsigned i = 0; i < routes.count && i < 4; i++)
    status.identities[i] = routes.node[i].id;
  status.self = node;
  status.at = mono_us();
  status.cycles++;
  status.rx = rx;
  status.authenticated = timing.accepted;
  status.rejected = wire_rejected + timing.rejected;
  status.serving = false;
  status.follower = node && valid;
  status.follower_clock_valid = valid;
  status.follower_utc = utc;
  status.time_sampled_us = status.at;
  status.source_epoch = current_origin.epoch;
  status.root_boot = current_origin.boot;
  status.source_frame_ns = current_origin.frame;
  uint64_t remaining = valid && now.tick >= discipline_at &&
                               now.tick - discipline_at < RC_FIELD_HOLD_TICKS
                           ? RC_FIELD_HOLD_TICKS - (now.tick - discipline_at)
                           : 0;
  status.service_until = valid && status.at <= UINT64_MAX - remaining / 16
                             ? status.at + remaining / 16
                             : 0;
  status.authority_until = status.service_until;
  status.source_eligible = false;
  status.peer_delivery_confirmed = lt_responses > 0;
  status.service_receipts = lt_bindings;
  status.precision.accepted = timing.accepted;
  status.precision.map_valid = valid;
  status.precision.authorized = valid;
  status.precision.locked = valid && fit.valid && stamp.pll_tracking;
  status.precision.fit_valid = fit.valid;
  status.precision.samples = fit.count;
  status.reception = delivery.view;
  status.precision.rms_valid = valid && fit_rms_valid;
  status.precision.rms_ns = fit_rms;
  status.precision.drift_valid = fit.valid;
  status.precision.drift_ppb = fit.ppb;
  status.precision.trim_ppb =
      local_clock.count ? local_clock.map[(local_clock.head + 3) % 4].ppb : 0;
  status.precision.correction_age_valid = valid;
  status.precision.correction_age_ms = stamp.age_us / 1000;
  status.relay.selected = parent_known;
  status.relay.active = running;
  status.relay.parent = parent_index;
  status.relay.accepted = timing.accepted;
  status.relay.tx_corrections = tx;
  uint64_t update_ms =
      timing.reference.valid && now.tick >= timing.reference.completed_at
          ? (now.tick - timing.reference.completed_at) / 16000
          : UINT64_MAX;
  uint64_t source_ms = valid && stamp.ns >= current_origin.issued
                           ? (stamp.ns - current_origin.issued) / 1000000
                           : UINT64_MAX;
  status.relay.update_age_ms =
      update_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)update_ms;
  status.relay.source_age_ms =
      source_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)source_ms;
  status.relay.held_time = valid && stamp.ns >= current_origin.expires;
  status.relay.root_identity = valid ? current_origin.root : 0;
  status.relay.parent_identity =
      timing.bound ? routes.node[routes.node[node].parent].id : 0;
  status.relay.root_boot = current_origin.boot;
  status.relay.source_epoch = current_origin.epoch;
  status.relay.source_created_ns = current_origin.issued;
  status.relay.source_expires_ns = current_origin.expires;
  status.relay.source_frame_ns = current_origin.frame;
  status.relay.upstream_train = timing.parent_train;
  status.relay.train = train;
  status.relay.map_generation = stamp.generation;
  status.relay.root = 0;
  status.relay.role = routes.node[node].transmitter ? 1 : 2;
  status.relay.phase_ns = phase;
  status.relay.target_ns = slot_target_ns;
  status.relay.error_ns = slot_phase_error();
  status.relay.phase_admitted = phase_admitted;
  status.relay.upstream_recoveries = upstream_recoveries;
  status.relay.sync_creating = creating;
  status.relay.sync_active = parent_synced;
  status.relay.phase_acquiring = lt_acquiring;
  status.relay.quiet = false;
  status.relay.slot = routes.node[node].slot;
  status.relay.slot_scans = slot_state.scans;
  status.relay.slot_scan_verified = slot_state.verified;
  status.relay.slot_scan_failures = slot_state.scan_failures;
  status.relay.slot_scan_owned_us = slot_state.scan_owned_total_us;
  status.relay.slot_changes = slot_state.changes;
  status.relay.slot_rollbacks = slot_state.rollbacks;
  status.relay.slot_busy = slot_state.busy;
  status.relay.slot_requests = slot_requests;
  status.relay.client_quiet_faults = client_quiet_faults;
  status.relay.retries = restarts;
  status.relay.start_delay_ticks = start_delay;
  status.relay.publish_ticks = publish_ticks;
  status.relay.stale_tx = stale_tx;
  status.relay.discipline_gap_max_ticks = discipline_gap_max;
  status.relay.holdover_expiries = holdover_expiries;
  status.relay.holdover_recoveries = holdover_recoveries;
  status.relay.reservation_max_us = reservation_max;
  status.relay.prepare_max_us = prepare_max;
  status.relay.rx_window_max_us = window_max;
  status.relay.correction_age_ms = stamp.age_us / 1000;
  status.relay.map_valid = valid;
  status.relay.utc_us = utc;
  status.relay.bound_ns = RC5_UNKNOWN;
  k_mutex_unlock(&state_lock);
}
static void owner(void *a, void *b, void *c) {
  (void)a;
  (void)b;
  (void)c;
  static bool callbacks_registered;
  if (!callbacks_registered) {
    bt_le_scan_cb_register(&scan_cb);
    bt_le_per_adv_sync_cb_register(&sync_cb);
    callbacks_registered = true;
  }
  lt_init();
  if (transmitter)
    check(!r2_uc_start(&group, self_identity, local_boot), "UCS1 start");
  while (!atomic_get(&stop_requested) && !atomic_get(&fault)) {
    k_mutex_lock(&state_lock, K_FOREVER);
    uint64_t deadline = status.deadline;
    k_mutex_unlock(&state_lock);
    if (deadline && mono_us() >= deadline)
      break;
    struct r2_capture now;
    if (!r2_capture_now(&now)) {
      if (r2_capture_error())
        check(false, "capture owner failed");
      k_sleep(K_MSEC(1));
      continue;
    }
    if (!capture_epoch)
      capture_epoch = (uint32_t)now.epoch;
    else if (capture_epoch != now.epoch) {
      check(false, "capture epoch changed");
      continue;
    }
    /* Consume and publish precision captures before admission/discovery work.
     * Renewal is a background target, never a reason to defer this drain.
     */
    captures(now);
    discover(now.tick);
#if defined(R2_RC_DYNAMIC_FIXTURE)
    extern bool r2_rc7_fixture_drop_sync(uint64_t, uint64_t);
    if (parent_synced && r2_rc7_fixture_drop_sync(self_identity, now.tick)) {
      if (!sync_close_until(mono_us() + 500000)) {
        check(false, "fixture established sync drain");
        continue;
      }
    }
#endif
    /* Sync_Timeout is supervision AFTER establishment, not an acquisition
     * timeout. Reserve the last500ms of this2s attempt for proven cancellation.
     * No GATT initiation may compete with this bounded cold PA acquisition. */
    if (creating && sync_started && mono_us() - sync_started >= 1500000) {
      uint64_t until = sync_started + 2000000;
      if (!sync_close_until(until)) {
        check(false, "cold sync drain");
        continue;
      }
      lt_renew_at = 0;
      ++upstream_recoveries;
    }
    /* Phase 1: RC owners and diagnostic leaf listen continuously. */
    bool window = true;
    lt_guard(now.tick, window);
    if(!advertising_withdraw(now.tick))continue;
    if (running && !rc5_demand_live(&lt_demand, now.tick / 16)) {
      stop();
      sent_count = 0;
      if (train == UINT32_MAX)
        check(false, "train exhausted");
      else
        ++train;
    }
    bool admitted = upstream_admitted(now.tick);
    if (node && scanning && !lt_conn && lt_live(now.tick) && admitted && found &&
        !creating && !sync) {
      /* Public timeout units are10ms:2.5s RF supervision, independent of
       * the unchanged500ms map expiry and bounded cold acquisition. */
      struct bt_le_per_adv_sync_param q = {
          .addr = parent, .sid = 12, .skip = 0, .timeout = 250};
#if defined(R2_RC_DYNAMIC_FIXTURE)
      extern uint8_t r2_rc7_fixture_sync_sid(uint64_t, uint8_t);
      q.sid = r2_rc7_fixture_sync_sid(self_identity, q.sid);
#endif
      int rc = bt_le_per_adv_sync_create(&q, &sync);
      /* Busy retries can run at the owner cadence. Retain evidence without
       * flooding the journal or doing USB work in the radio owner. */
      static uint64_t sync_error_due;
      if(!rc||mono_us()>=sync_error_due){
        r2_timing_event(R2_TIMING_SYNC_CREATE,rc,routes.node[parent_index].id,q.sid,q.timeout,0);
        sync_error_due=mono_us()+1000000;
      }
      if (!rc) {
        sync_sid = q.sid;
        creating = true;
        sync_started = mono_us();
      } else if (rc != -EAGAIN && rc != -EBUSY)
        check(false, "sync create");
    }
    if (parent_synced && scanning && !slot_state.scanning && !relay_fallback) {
      check(!bt_le_scan_stop(), "scan stop");
      scanning = false;
    }
    if (node && window && !creating && !scanning && !lt_conn && !lt_paused &&
        (!parent_synced || slot_state.scanning || relay_fallback)) {
      struct bt_le_scan_param q = {
          .type = BT_LE_SCAN_TYPE_PASSIVE, .interval = 32, .window = 32};
      int rc = bt_le_scan_start(&q, NULL);
      if (!rc)
        scanning = true;
      else if (slot_state.scanning)
        slot_scan_failed = true;
      else if (rc != -EAGAIN && rc != -EBUSY)
        check(false, "scan start");
    }
    /* Invalid time withdraws output, not an established radio synchronization.
     * Native PA supervision owns RF loss. Reacquire the same authenticated
     * scope before requesting another JOIN; neither path extends permission. */
    if (node && holdover_expired && lt_acquiring) {
      lt_acquiring = false;
      lt_quiesce(false);
      restart_at = 0;
      start_prepared = false;
    }
    if (node && parent_known && parent_admission >= 2 &&
        now.tick >= last_context_progress &&
        now.tick - last_context_progress >= 32000000 &&
        !upstream_usable)
      parent_rebind_request(now.tick);
    rc_slots_listen(&slot_state, now.tick / 16,
                    scanning && !creating && !lt_conn && !lt_paused);
    if (node && routes.node[node].transmitter && timing.bound &&
        lt_live(now.tick) && !lt_conn) {
      if (slot_selected && !slot_startup_checked && !slot_state.scanning &&
          !slot_move_pending && downstream_ready(now.tick) &&
          rc_slots_begin_startup_check(&slot_state, now.tick / 16, (uint32_t)lt_random())) {
        slot_startup_checked = true;
        slot_scan_failed = false;
      }
      if ((!slot_selected || slot_change_requested) && !slot_state.scanning &&
          !slot_move_pending) {
        slot_state.parent_slot = routes.node[parent_index].slot;
        if (rc_slots_begin(&slot_state, now.tick / 16, slot_selected,
                           (uint32_t)lt_random())) {
          slot_change_requested = false;
          slot_scan_failed = false;
        }
      }
      if (slot_state.scanning && !scanning && !creating && !lt_paused) {
        struct bt_le_scan_param q = {
            .type = BT_LE_SCAN_TYPE_PASSIVE, .interval = 32, .window = 32};
        int rc = bt_le_scan_start(&q, NULL);
        if (!rc)
          scanning = true;
        else
          slot_scan_failed = true;
      }
      uint32_t slot_random = 0;
      if (slot_state.scanning) {
        slot_random = (uint32_t)lt_random();
#if defined(R2_RC_DYNAMIC_FIXTURE)
        extern uint32_t r2_rc7_fixture_slot_random(uint64_t, uint32_t);
        slot_random = r2_rc7_fixture_slot_random(self_identity, slot_random);
#endif
      }
      if (slot_state.scanning &&
          rc_slots_finish(&slot_state, now.tick / 16, slot_random)) {
        if (scanning) {
          int rc = bt_le_scan_stop();
          if (!rc)
            scanning = false;
          else {
            slot_scan_failed = true;
            check(false, "slot scan owner drain");
          }
        }
        if (slot_scan_failed) {
          if (!slot_state.restoring)
            rc_slots_rollback(&slot_state, now.tick / 16);
          slot_scan_failed = false;
        }
        if (slot_state.moving) {
          if (stop()) {
            sent_count = 0;
            if (train == UINT32_MAX) {
              check(false, "train exhausted");
              break;
            }
            ++train;
            ++restarts;
            phase_admitted = false;
            restart_at = 0;
            if (!set_local_slot(slot_state.target)) {
              rc_slots_rollback(&slot_state, now.tick / 16);
              (void)set_local_slot(slot_state.old_slot);
              slot_selected = true;
              continue;
            }
            slot_move_pending = true;
            slot_move_until = now.tick + 32000000;
            slot_hint_update();
          }
        } else {
          slot_selected = slot_selected || slot_state.available;
          if (slot_state.available)
            rc_slots_commit(&slot_state, now.tick / 16);
          if (slot_selected)
            retained_slot = routes.node[node].slot;
          slot_state.rescan = false;
        }
      }
      if (slot_move_pending && downstream_ready(now.tick)) {
        rc_slots_commit(&slot_state, now.tick / 16);
        slot_move_pending = false;
        slot_selected = true;
        retained_slot = routes.node[node].slot;
        slot_hint_update();
      }
      if (slot_move_pending && now.tick >= slot_move_until) {
        if (stop()) {
          rc_slots_rollback(&slot_state, now.tick / 16);
          check(set_local_slot(slot_state.old_slot), "restore old slot");
          sent_count = 0;
          if (train == UINT32_MAX) {
            check(false, "train exhausted");
            break;
          }
          ++train;
          ++restarts;
          phase_admitted = false;
          restart_at = 0;
          slot_move_pending = false;
          slot_selected = true;
          slot_hint_update();
        }
      }
    }
    struct rc5_stamp st;
    bool ready_clock =
        node && rc5_clock_capture(&local_clock, &timing, now.tick, &st);
#if defined(R2_RC_DYNAMIC_FIXTURE)
    /* Fixture-only controller disturbance, executed by the normal PA owner.
     * Return a physical-counter pause; no timestamp or source is synthesized.
     */
    extern uint64_t r2_rc7_fixture_pause(unsigned, uint64_t);
    uint64_t pause =
        running && ready_clock ? r2_rc7_fixture_pause(node, now.tick) : 0;
    if (pause && stop()) {
      sent_count = 0;
      ++train;
      restart_at = requested;
      while (restart_at <= now.tick)
        restart_at += 1000000;
      restart_at += pause;
    }
#endif
    if (node && routes.node[node].transmitter && !running && !lt_conn &&
        ready_clock && precision_synced(now.tick) && phase_rx && now.tick >= phase_rx &&
        now.tick - phase_rx < RC_FIELD_HOLD_TICKS &&
        rc5_demand_live(&lt_demand, now.tick / 16) &&
        (slot_selected || slot_move_pending) && !slot_state.scanning) {
      if (!pa)
        create();
      /* First request uses slot target with no assumed device start latency.
       * Subsequent placement uses the actual captured request-to-TX duration.
       */
      if (!restart_at) {
        restart_at = phase_rx + (uint64_t)parent_slot_delta_ns * 2 / 125;
        while (restart_at < now.tick + 32000)
          restart_at += 1000000;
      }
      /* Encoding and synchronous HCI publication are preparation, not part of
       * the calibrated start request. Finish them before waiting for its tick.
       */
      if (!start_prepared) {
        publish();
        start_prepared = true;
      }
      lt_acquiring = true;
      lt_quiesce(true);
#if defined(R2_RC_DYNAMIC_FIXTURE)
      extern uint32_t r2_rc7_fixture_work(void);
      uint32_t work_us = r2_rc7_fixture_work();
      if (work_us)
        k_busy_wait(work_us);
#endif
      /* Publication and quiescence can block. Select the next phase only after
       * they finish, then wait once instead of polling a 200 us aperture. */
      struct r2_capture due;
      if (!r2_capture_now(&due))
        continue;
      uint32_t wait_us;
      if (!rc5_field_start_wait(&restart_at, due.tick, &wait_us)) {
        check(false, "start tick overflow");
        continue;
      }
      if (wait_us)
        k_busy_wait(wait_us);
      if (atomic_get(&stop_requested) || atomic_get(&fault) ||
          (deadline && mono_us() >= deadline))
        continue;
      start();
      restart_at = 0;
      start_prepared = false;
    }
    /* Read-only PPS witness evaluates frozen BLE maps; never estimator input.
     */
    r2_shadow_poll();
    if (r2_capture_now(&now)) {
      holdover_poll(now.tick);
      if (running && holdover_expired && published.count) {
        publish();
        (void)r2_capture_now(&now);
      }
      snapshot_publish(now);
    }
    lt_guard(now.tick, window);
    lt_poll(now.tick, window);
    if (transmitter) {
      struct r2_uc_clock offered=clock_offer(now.tick);
#ifdef R2_USB_SOURCE_STREAM
      usb_offer_now();
#endif
      check(!r2_uc_poll(&offered,precision_synced(now.tick)),"UCS1 advertising gate");
    }
    k_sleep(K_USEC(1000));
  }
  r2_service_stop();
  r2_shadow_normal_withdraw();
#ifdef R2_USB_SOURCE_STREAM
    r2_usb_source_withdraw();
#endif
  bool closed = stop();
  if (pa) {
    int rc = bt_le_ext_adv_delete(pa);
    if (!rc)
      pa = NULL;
    else
      closed = false;
  }
  if (sync) {
    uint64_t until = mono_us() + 500000;
    k_mutex_lock(&state_lock, K_FOREVER);
    if (status.close_deadline && status.close_deadline < until)
      until = status.close_deadline;
    k_mutex_unlock(&state_lock);
    if (!sync_close_until(until))
      closed = false;
  }
  if (scanning) {
    int rc = bt_le_scan_stop();
    if (!rc)
      scanning = false;
    else
      closed = false;
  }
  if (!lt_close())
    closed = false;
  if (transmitter && !r2_uc_close())
    closed = false;
  k_sleep(K_MSEC(75));
  struct r2_radio_capture_stats capture;
  r2_radio_capture_stats(&capture);
  if (capture.armed)
    closed = false;
  check(closed, "native owners drained");
  k_mutex_lock(&state_lock, K_FOREVER);
  status.error = atomic_get(&fault);
  status.active = !closed;
  status.serving = status.follower = status.follower_clock_valid = false;
  status.closed = closed;
  status.closed_at = closed ? mono_us() : 0;
  k_mutex_unlock(&state_lock);
}
int r2_service_configure(const struct r2_identity *id,
                         const struct r2_enrollment *e) {
  if (!id || id->result || !id->boot || !e || !e->rc_dynamic ||
      !r2_group_valid(&e->group))
    return -EINVAL;
  if (atomic_get(&started)) {
    if (!status.closed || status.active ||
        k_thread_join(&owner_thread, K_NO_WAIT))
      return -EBUSY;
  }
  if (!lt_reset())
    return -EIO;
  identity = *id;
  group = e->group;
  local_boot = id->boot;
  self_identity = strtoull(id->silicon, NULL, 16);
  if (!self_identity)
    return -EACCES;
  transmitter = !e->rc7_leaf;
  exclude_direct = e->rc7_exclude_direct_gm;
  forced_parent = 0;
  node = 1;
  parent_index = 0;
  receiver = (struct rc7_receiver){0};
  upstream_path = (struct rc7_path){0};
  publication_path = (struct rc7_path){0};
  fit = (struct rc5_rate_fit){0};
  local_clock = (struct rc5_clock){.hold_ticks = RC_FIELD_HOLD_TICKS};
  delivery = (struct r2_link_quality){0};
  routes = (struct rc5_routes){.count = 2,
                               .node = {{0},
                                        {.id = self_identity,
                                         .slot = transmitter ? 1 : 255,
                                         .depth = transmitter ? 1 : 0,
                                         .transmitter = transmitter}}};
  current_origin = (struct rc5_origin){0};
  memset(current_proof, 0, 16);
  sent_count = publication = rx = tx = restarts = wire_rejected = stale_tx =
      capture_epoch = 0;
  train = 1;
  running = scanning = creating = parent_synced = found = parent_known =
      phase_admitted = start_prepared = awaiting_first_tx = slot_selected =
          slot_change_requested = slot_scan_failed = slot_move_pending =
              holdover_expired = false;
  publish_ticks = discipline_at = discipline_gap_max = usable_tx_from =
      last_rx = phase_rx = start_delay = requested = restart_at =
          slot_move_until = upstream_retry_at = 0;
  delivery_packet_tuple=delivery_correction_tuple[0]=delivery_correction_tuple[1]=0;
  delivery_origin_issued=delivery_origin_expires=delivery_discipline_tuple=0;
  delivery_packet_count=delivery_discipline_rx=0;
  delivery_source_seen=delivery_source_lost=false;
  memset(&delivery_capture_baseline,0,sizeof delivery_capture_baseline);
  reservation_max = prepare_max = window_max = holdover_expiries =
      holdover_recoveries = slot_requests = upstream_recoveries =
          client_quiet_faults = 0;
  rebind_parent_boot = rebind_root_boot = fit_parent_boot = fit_root_boot = 0;
  rebind_root_epoch = rebind_capture_epoch = fit_parent_epoch = fit_root_epoch =
      0;
  fit_rms = RC5_UNKNOWN;
  fit_rms_valid = false;
  pa = NULL;
  sync = NULL;
  sync_sid = 12;
  sync_started = 0;
  sync_cancels = 0;
  sync_terms = sync_term_ms = sync_term_reason = 0;
  sync_close_error = 0;
  memset(acquisition_snapshot, 0, sizeof acquisition_snapshot);
  memset(candidates, 0, sizeof candidates);
  candidate_since = upstream_bad_since = 0;
  parent_selected_at = parent_stage_at = mono_us();
  upstream_usable = relay_fallback = false;
  parent_admission = 0;
  parent_failed = parent_locked = parent_ineligible = false;
  stream_sample = (struct rc_stream_sample){0};
  slot_target_ns = transmitter ? 15625000 : 0;
  parent_slot_delta_ns = slot_target_ns;
  phase = 0;
  direct_phase_bias = RC_DIRECT_PPS_CORRECTION_NS;
  rc_slots_init(&slot_state, 1, 0);
  atomic_clear(&fault);
  atomic_clear(&stop_requested);
  atomic_clear(&started);
  k_msgq_purge(&discovery_queue);
  k_msgq_purge(&sync_queue);
  memset(transport_snapshot, 0, sizeof transport_snapshot);
  cap_received = cap_authenticated = cap_usable = 0;
  status = (struct r2_service_status){.configured = true,
                                      .closed = true,
                                      .normal = true,
                                      .profile_rc = true,
                                      .self = 1,
                                      .root = 0,
                                      .coordinator = 255,
                                      .standby = 255,
                                      .group_id = group.id,
                                      .key_epoch = group.epoch};
  snprintf(status.name, sizeof status.name, "%s", id->unit);
  return 0;
}
/* Called only between configure and start by the dispatcher. Admission still
 * requires the full authenticated path and current root evidence. */
int r2_dynamic_service_parent_target(uint64_t parent) {
  if (atomic_get(&started) || parent == self_identity)
    return -EINVAL;
  forced_parent = parent;
  return 0;
}
void r2_service_clock(const struct r2_clock *clock) {
  (void)clock;
  r2_service_supervise();
}
int r2_service_preflight(uint32_t seconds, uint64_t arrival) {
  (void)seconds;
  (void)arrival;
  return status.configured && !atomic_get(&started) ? 0 : -EACCES;
}
static int service_launch(uint64_t arrival, uint64_t deadline, bool normal) {
  int rc = r2_service_preflight(1, arrival);
  if (rc)
    return rc;
  atomic_set(&started, 1);
  k_mutex_lock(&state_lock, K_FOREVER);
  status.active = true;
  /* Every new owner gets a fresh heartbeat before it can be observed. */
  status.at = mono_us();
  status.closed = false;
  status.normal = normal;
  status.deadline = deadline;
  k_mutex_unlock(&state_lock);
  k_thread_create(&owner_thread, owner_stack,
                  K_THREAD_STACK_SIZEOF(owner_stack), owner, NULL, NULL, NULL,
                  8, 0, K_NO_WAIT);
  k_thread_name_set(&owner_thread, "r2_boundary");
  return 0;
}
int r2_service_run(uint64_t arrival) {
  return service_launch(arrival, 0, true);
}
int r2_service_start(uint32_t seconds, uint64_t arrival) {
  if (!seconds) {
    r2_service_stop();
    return 0;
  }
  uint64_t deadline;
  if (!rc5_field_deadline(arrival, seconds, mono_us(), &deadline))
    return -ETIMEDOUT;
  return service_launch(arrival, deadline, false);
}
void r2_service_stop(void) {
  if (!atomic_get(&started))
    return;
  k_mutex_lock(&state_lock, K_FOREVER);
  if (!status.closing) {
    status.closing = true;
    status.close_deadline = mono_us() + 2000000;
  }
  k_mutex_unlock(&state_lock);
  atomic_set(&stop_requested, 1);
}
void r2_service_supervise(void) {
  if (status.closing && !status.closed && mono_us() >= status.close_deadline)
    atomic_cas(&fault, 0, -ETIMEDOUT);
}
int r2_service_close_result(void) {
  struct r2_service_status s;
  r2_service_snapshot(&s);
/* The lifetime fault stays visible. Successful proved owner closure is a
 * separate result, so a contained fault can use supported maintenance. */
#if defined(R2_SWITCHABLE_FIELD)
  if (s.closed && !s.active && atomic_get(&started) &&
      k_thread_join(&owner_thread, K_NO_WAIT))
    return -EAGAIN;
#endif
  return s.closed && !s.active ? 0 : s.error ? s.error : -EAGAIN;
}
void r2_service_snapshot(struct r2_service_status *out) {
  k_mutex_lock(&state_lock, K_FOREVER);
  *out = status;
  k_mutex_unlock(&state_lock);
  if (atomic_get(&fault))
    out->error = atomic_get(&fault);
}
void r2_dynamic_service_transport_snapshot(uint32_t out[13]) {
  k_mutex_lock(&state_lock, K_FOREVER);
  memcpy(out, transport_snapshot, sizeof transport_snapshot);
  k_mutex_unlock(&state_lock);
}
void r2_dynamic_service_gm_admission_snapshot(uint32_t out[7]) {
  k_mutex_lock(&state_lock, K_FOREVER);
  memcpy(out, gm_admission_diagnostic, sizeof gm_admission_diagnostic);
  k_mutex_unlock(&state_lock);
}
void r2_dynamic_service_connection_snapshot(uint32_t out[20]) {
  k_mutex_lock(&state_lock, K_FOREVER);
  memcpy(out, lt_diagnostic, sizeof lt_diagnostic);
  k_mutex_unlock(&state_lock);
}
void r2_dynamic_service_acquisition_snapshot(uint32_t out[20]) {
  k_mutex_lock(&state_lock, K_FOREVER);
  memcpy(out, acquisition_snapshot, sizeof acquisition_snapshot);
  k_mutex_unlock(&state_lock);
}
void r2_service_ui(struct r2_ui_model *m) {
  struct r2_service_status s;
  r2_service_snapshot(&s);
  m->profile_supported = true;
  m->profile_rc = true;
  m->profile_switching = false;
  m->ble_active = s.active && !s.closing;
  m->root = 0;
  m->coordinator = m->standby = 255;
  m->precision = s.precision;
  memcpy(m->lamps, s.lamps, sizeof m->lamps);
  r2_ui_reception(m, &s.reception, mono_us());
  m->precision_beat_hz =
      s.active && !s.closing && !s.error && s.follower_clock_valid
          ? R2_PRECISION_HZ
          : 0;
  m->rx = s.rx;
  m->authenticated = s.authenticated;
  m->gm_capable = false;
  m->source_eligible = s.source_eligible;
  m->relay_selected = parent_known;
  m->time_relay_hops = parent_known ? routes.node[parent_index].depth : 0;
  m->relay_upstream = s.relay.parent;
  m->boundary_clock = true;
  m->rc_source_held = s.relay.held_time;
  m->rc_update_age_ms = s.relay.update_age_ms;
  m->rc_source_age_ms = s.relay.source_age_ms;
  bool live = s.active && !s.error && !s.closing;
  m->rc_serving_clients = live && s.follower_clock_valid &&
      ((s.relay.active && s.relay.phase_admitted && s.relay.tx_corrections) ||
       r2_uc_serving_clients());
  m->ble_active = live;
  m->sync_attempt = live && !s.follower_clock_valid;
  m->scope_active = live && (s.serving || s.follower);
  m->role =
      m->scope_active ? (s.serving ? R2_UI_GM : R2_UI_FOLLOWER) : R2_UI_NO_ROLE;
  m->authenticated_gm = live && s.follower;
  m->gm_source_fresh = live && s.serving && m->clock.source_available;
  r2_ui_identity_name(m->root_name, routes.node[0].id, NULL);
  r2_ui_identity_name(
      m->upstream_name,
      node && s.relay.selected ? routes.node[s.relay.parent].id : 0, NULL);
  r2_time_precision_snapshot(&m->pps_precision);
  if (node) { /* Local GNSS remains a witness, never selected fallback. */
    m->clock.valid = false;
    m->coarse_valid = false;
    r2_ui_select_ble_time(m, s.follower_utc, s.time_sampled_us, mono_us(),
                          s.service_until, live && s.follower_clock_valid);
  }
}
int r2_service_relay(uint64_t session, uint32_t seconds, uint64_t arrival) {
  (void)session;
  (void)seconds;
  (void)arrival;
  return -ENOTSUP;
}
int r2_service_relay_cancel(uint64_t session) {
  (void)session;
  return -ENOTSUP;
}
int r2_service_name(const char *name) {
  if (!name || strlen(name) >= sizeof status.name)
    return -EINVAL;
  k_mutex_lock(&state_lock, K_FOREVER);
  snprintf(status.name, sizeof status.name, "%s", name);
  k_mutex_unlock(&state_lock);
  return 0;
}
uint32_t r2_service_stack_unused(void) {
#ifdef CONFIG_INIT_STACKS
  size_t unused;
  if (atomic_get(&started) && !k_thread_stack_space_get(&owner_thread, &unused))
    return unused;
#endif
  return UINT32_MAX;
}
uint32_t r2_radio_service_stack_unused(void) {
  return r2_service_stack_unused();
}
