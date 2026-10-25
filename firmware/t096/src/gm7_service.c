/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Normal GM owner. RC7 remains in its separate unchanged backend. */
#include "gm7_service.h"
#include "../protocol/gm7_bridge.h"
#include "../protocol/gm7_phase.h"
#include "ble_service.h"
#include "ble_transport.h"
#include "capture.h"
#include "cleanup_budget.h"
#include "gm7_clock.h"
#include "gm7_controller.h"
#include "gm7_source.h"
#include "gm7_timing.h"
#include "gm_diagnostics.h"
#include "gnss_diagnostics.h"
#include "group_admission.h"
#include "lora_service.h"
#include "owner_budget.h"
#include "radio_service.h"
#include "rc_shadow.h"
#include "role_announcement.h"
#include "runtime_profile.h"
#include "time_service.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#define R2_LOCK_UNIT 2u
#include "health_lock.h"
static struct gm7_owner owner;
static struct gm7_scheduler scheduler;
/* Descriptive publication only; until is conservatively converted kernel time.
 * Original sender sample/durations are never incremented by a receiver. */
static struct {
  struct gm7_reported_role report;
  uint64_t identity, boot;
  bool valid, current;
} peer_display[GM7_PEERS];
static struct {
  struct r2_clock_view view;
  int64_t translated_utc_us, translated_at_us, last_pps, last_rmc, last_rmc_utc,
      last_pair_edge, last_pair_utc;
  uint32_t translated_epoch;
  bool stopped, fatal;
} input;
/* One retained delivered coordinate; raw GNSS remains in the time owner and
 * input metadata. Promotion never becomes evidence of GNSS eligibility. */
static struct r2_source_clock functional_source;
static bool promoted_valid, takeover_preparing;
static uint64_t takeover_bound, promoted_pps, phase_step_at;
static struct {
  uint64_t identity[2];
  uint8_t address[2][6];
  uint32_t map_bound_us;
  unsigned count;
} roster;
/* GM publication only. The public compatibility snapshot is constructed on
 * read; unused RC/retired coordinator fields consume no persistent GM RAM. */
static struct {
  struct r2_supervision supervision;
  struct r2_transport_fault transport_fault;
  struct r2_radio_precision_view precision;
  struct r2_link_quality_view reception;
  struct r2_clock_quality quality;
  uint64_t at, profile_incarnation, group_id, root_boot, follower_utc,
      time_sampled_us, source_frame_ns, service_until, authority_until,
      display_coarse_utc, display_coarse_at, close_deadline, closed_at,
      deadline;
  uint64_t identities[2];
  uint32_t key_epoch, admission_ms, hot_refusal, source_epoch,
      local_source_epoch, rx, authenticated, rejected, drops, errors, backlog,
      cycles, worker_max_us, profile_changes;
  unsigned root, self, coordinator, standby;
  int error;
  char name[25];
  uint8_t lamps[8];
  bool serving, hot_ready, gm_cold, gm_capable, source_eligible, follower,
      follower_clock_valid, display_coarse_valid, closed, active, closing,
      configured, normal, profile_pending;
} status;
static struct r2_group_config group;
static struct {
  uint64_t birth, founder, term, active, active_boot, at;
  uint32_t hot_ms, active_ms, pps_ms, reason;
  uint16_t blockers;
  uint8_t role, known;
  bool pending;
  uint8_t event_queued, event_completed, event_failed, event_unsent;
  uint8_t reservation_stage;
  int16_t reservation_rc;
  uint32_t reservation_failures;
} diagnostic;
_Static_assert(sizeof(diagnostic) == 80, "Use diagnostic padding only");
enum {
  RESERVE_ADMITTED = 1,
  RESERVE_CONTROL,
  RESERVE_SCHEDULER,
  RESERVE_NATIVE,
  RESERVE_COMPLETE,
  RESERVE_EXPIRED
};
/* CPU construction lead only. Actual RF windows and native>=3ms admission
 * remain unchanged; packet sample/horizon are never moved after construction.
 */
#define PRIMARY_PREPARE_US 25000u
static struct {
  uint64_t sequence;
  /* Bounded negative-only phase lease; occupies existing alignment padding. */
  uint32_t phase_sample_ms;
  uint8_t queued, completed, failed;
} event_evidence;
_Static_assert(sizeof(event_evidence) == 16, "Retain event evidence storage");

static uint64_t identity, boot, close_until, primary_frame = UINT64_MAX,
                                             status_cycle = UINT64_MAX,
                                             diag_frame = UINT64_MAX;
static atomic_t management_rx, management_mac, management_accepted;
_Static_assert(sizeof(atomic_t) == sizeof(uint32_t),
               "Management counters retain storage");
/* Single owner updates; console readers use atomic loads, not a reader-only
 * mutex. Saturation preserves evidence across runtime role transitions. */
static void management_count(atomic_t *counter) {
  uint32_t value = (uint32_t)atomic_get(counter);
  if (value != UINT32_MAX)
    atomic_set(counter, (int32_t)(value + 1));
}
static uint32_t clock_epoch, event_generation, overflow_seen, reset_generation,
    last_phase, last_enclosure;
static bool phase_known, stop_begun;
static uint8_t own_address[6];
static char name[25];
static bool configured, started, used, transport_stopped, scan_aligned,
    resource_fault;
static atomic_t stopping, busy;
static bool resuming;
static uint64_t resume_boot;
static int start(uint64_t, uint32_t);
K_SEM_DEFINE(supervise_requested, 0, 1);
K_THREAD_STACK_DEFINE(supervisor_stack, 2048);
static struct k_thread supervisor_thread;
static void supervisor_owner(void *a, void *b, void *c) {
  (void)a;
  (void)b;
  (void)c;
  for (;;) {
    (void)k_sem_take(&supervise_requested, K_FOREVER);
    r2_service_supervise();
  }
}
K_MUTEX_DEFINE(lock);
K_THREAD_STACK_DEFINE(owner_stack, 8192);
static struct k_work_q owner_queue;
static uint64_t kernel_now(void) {
  return k_ticks_to_us_floor64(k_uptime_ticks());
}
static bool bridge(struct gm7_bridge *b) {
  /* Single-core, no zero-latency IRQs: pair the software clocks without a
   * scheduler/IRQ gap or a competing CC2 capture. The HF reader is bounded,
   * read-only and never waits/requests a clock. Restore even a nested key
   * before validating the unchanged100us enclosure; real domain faults still
   * fail. */
  unsigned int key = irq_lock();
  b->kernel_lo = kernel_now();
  bool valid = r2_gm7_clock_now(&b->hf, &b->epoch);
  if (valid)
    b->kernel_hi = kernel_now();
  irq_unlock(key);
  return valid && gm7_bridge_valid(*b);
}
static uint64_t hf_read(void *context) {
  (void)context;
  uint64_t now;
  uint32_t epoch;
  return r2_gm7_clock_now(&now, &epoch) && epoch == clock_epoch ? now
                                                                : UINT64_MAX;
}
static int entropy(void *context, uint8_t *out, size_t length) {
  (void)context;
  return sys_csrand_get(out, length);
}
static bool reservation_fence(void *context) {
  (void)context;
  r2_gm7_controller_cancel(0);
  const struct gm7_job *j = &scheduler.job[GM7_PRIMARY];
  return j->state != GM7_JOB_EMPTY &&
         r2_gm7_controller_exposed(GM7_PRIMARY, j->ticket);
}
static void fence(unsigned reason, uint64_t now) {
  gm7_owner_withdraw(&owner, reason, now);
  gm7_scheduler_cancel_positive(&scheduler, now);
  r2_gm7_controller_cancel(now);
  r2_radio_service_withdraw();
}
static __attribute__((noinline)) bool source_read(struct gm7_source *s,
                                                  struct r2_source_clock *map,
                                                  struct r2_clock *clock,
                                                  struct gm7_bridge b) {
  (void)k_mutex_lock(&lock, K_FOREVER);
  memset(clock, 0, sizeof *clock);
  clock->view = input.view;
  clock->translated_utc_us = input.translated_utc_us;
  clock->translated_at_us = input.translated_at_us;
  clock->translated_epoch = input.translated_epoch;
  clock->last_pps = input.last_pps;
  clock->last_rmc = input.last_rmc;
  clock->last_rmc_utc = input.last_rmc_utc;
  clock->last_pair_edge = input.last_pair_edge;
  clock->last_pair_utc = input.last_pair_utc;
  clock->stopped = input.stopped;
  clock->fatal = input.fatal;
  *map = functional_source;
  (void)k_mutex_unlock(&lock);

  if (clock->view.mono_us >= 0 &&
      b.kernel_hi >= (uint64_t)clock->view.mono_us) {
    uint64_t elapsed = b.kernel_hi - (uint64_t)clock->view.mono_us;
    int64_t *ages[4] = {&clock->view.gps_age_us, &clock->view.pps_age_us,
                        &clock->view.gga_age_us, &clock->view.rx_age_us};
    for (unsigned i = 0; i < 4; i++)
      if (*ages[i] >= 0 && elapsed <= (uint64_t)(INT64_MAX - *ages[i]))
        *ages[i] += (int64_t)elapsed;
    clock->view.mono_us = (int64_t)b.kernel_hi;
  }
  struct r2_capture current = {0};
  bool captured = r2_capture_now(&current) && current.epoch == b.epoch;
  if (gm7_source_snapshot(s, clock, map, current, b.kernel_hi))
    return false;
  uint64_t until = 0, pps = 0;
  bool live = gm7_bridge_to_hf(b, s->until_us, &until);
  if (s->pps_healthy && !gm7_bridge_past(b, s->pps_at, &pps))
    s->pps_healthy = false;
  s->until_us = live ? until : 0;
  s->pps_at = pps;
  s->physical_ready &= captured && live;
  /* Physical UTC was evaluated at this capture, not at the earlier software
   * bridge. A coarse fallback instead belongs to the original kernel sample. */
  s->sampled_us = s->physical_ready ? current.tick / 16 : b.hf;
  s->gnss_healthy &= live;
  return true;
}
static bool source_phase(const struct gm7_source *s, struct gm7_bridge b,
                         uint64_t *utc) {
  if (!s->utc_available || s->sampled_us > b.hf ||
      s->utc_us > UINT64_MAX - (b.hf - s->sampled_us))
    return false;
  *utc = s->utc_us + b.hf - s->sampled_us;
  return gm7_utc_valid(*utc);
}
/* Root UTC and the local HF coordinate meet only through an actual selected
 * G1 delivered map. Nonce offset intervals never become phase evidence. */
static bool phase_at(const struct r2_gm7_timing *t, struct gm7_bridge b,
                     uint64_t *utc, uint32_t *width) {
  int64_t relative;
  if (!t->retained || t->counter_epoch != b.epoch || !t->map.valid ||
      t->utc_offset_ns < 0 || !r2_servo_mapped(&t->map, b.hf * 16, &relative) ||
      relative < 0 || relative > INT64_MAX - t->utc_offset_ns ||
      t->phase_enclosure_us > 8000)
    return false;
  *utc = (uint64_t)(relative + t->utc_offset_ns) / 1000;
  *width = t->phase_enclosure_us;
  return true;
}
/* An original physical phase may place already immutable negative copies
 * after eligibility disappears. It never supplies UTC, positive authority or
 * source permission. The owner last-processing64 proves a bounded polling gap;
 * modulo32 ms is used only within this <=1s lease, never as a wrap extender.
 * Expiry retires the cache even when no negative event is pending. */
static bool negative_phase(struct gm7_bridge b, uint64_t *base,
                           uint32_t *enclosure) {
  if (!phase_known)
    return false;
  uint32_t elapsed_ms =
      (uint32_t)(b.hf / 1000) - event_evidence.phase_sample_ms;
  if (b.epoch != clock_epoch || b.hf < owner.now ||
      b.hf - owner.now > 1000000 || elapsed_ms >= 1000) {
    phase_known = false;
    return false;
  }
  /* Cached floor(ms) and current floor(ms) give outward age <(delta+1)ms.
   * Include CPU preparation lead plus the4ms complete-event endpoint. */
  uint32_t end_age = (elapsed_ms + 1) * 1000 + PRIMARY_PREPARE_US + 4000;
  if (end_age > 1000000)
    return false;
  uint32_t growth = (end_age + 4999) / 5000;
  if (last_enclosure > 2000 || growth > 2000 - last_enclosure) {
    phase_known = false;
    return false;
  }
  *enclosure = last_enclosure + growth;
  *base = (b.hf / GM7_FRAME_US) * GM7_FRAME_US + last_phase;
  if (*base > b.hf && *base >= GM7_FRAME_US)
    *base -= GM7_FRAME_US;
  return true;
}
static void preparation(struct gm7_preparation *p, struct gm7_bridge b,
                        const struct r2_gm7_timing *t) {
  *p = (struct gm7_preparation){.correction_age_us = UINT32_MAX,
                                .phase_enclosure_us = UINT32_MAX,
                                .retained_enclosure_us = UINT32_MAX};
  if (!owner.scope.active || owner.scope.active == identity ||
      t->scope.root_identity != owner.scope.active ||
      t->scope.boot != owner.scope.boot ||
      t->scope.source_epoch != owner.scope.timing_epoch ||
      t->scope.publication_epoch != owner.scope.train ||
      t->capture_gen != owner.scope.capture_gen || t->counter_epoch != b.epoch)
    return;
  p->scope = owner.scope;
  p->shadow_gen = t->shadow_gen;
  p->map_valid = t->view.map_valid;
  p->fit_valid = t->view.fit_valid;
  p->locked = t->view.locked;
  if (t->last_correction && b.hf * 16 >= t->last_correction)
    p->correction_age_us = (uint32_t)((b.hf * 16 - t->last_correction) / 16);
  uint64_t utc;
  uint32_t width;
  if (phase_at(t, b, &utc, &width)) {
    p->phase_available = width <= 2000;
    p->phase_enclosure_us = width;
    p->retained_prediction = true;
    p->retained_enclosure_us = width;
    if (owner.selected.yield_utc_us && owner.selected.yield_utc_us >= utc &&
        owner.selected.yield_utc_us - utc < 1000000) {
      uint64_t delta = owner.selected.yield_utc_us - utc;
      p->yield_sequence = owner.selected.sequence;
      p->yield_before_us = b.hf + delta;
      if (p->yield_before_us >= width)
        p->yield_before_us -= width;
      else
        return;
      p->yield_after_us = b.hf + delta + width;
      p->yield_phase_valid = width <= 2000;
    }
  }
}
static struct gm7_window window(uint64_t nominal, uint32_t enclosure) {
  return (struct gm7_window){.nominal = nominal,
                             .latest_start = nominal + 2000,
                             .latest_end = nominal + 4000,
                             .enclosure_us = enclosure,
                             .channel = 37,
                             .physical = true};
}
static void reservation_note(unsigned stage, int rc, bool failed) {
  (void)k_mutex_lock(&lock, K_FOREVER);
  diagnostic.reservation_stage = (uint8_t)stage;
  diagnostic.reservation_rc = rc < INT16_MIN   ? INT16_MIN
                              : rc > INT16_MAX ? INT16_MAX
                                               : (int16_t)rc;
  if (failed && diagnostic.reservation_failures != UINT32_MAX)
    ++diagnostic.reservation_failures;
  (void)k_mutex_unlock(&lock);
}
static void reservation_fail(uint64_t nominal, uint64_t sequence,
                             unsigned stage, int rc, bool exposed,
                             uint64_t now) {
  if (!owner.reserved || owner.reserved_nominal != nominal ||
      owner.reserved_sequence != sequence)
    return;
  reservation_note(stage, rc, true);
  /* Cancellation fences native work before checking possible AUX exposure. */
  r2_gm7_controller_cancel(now);
  gm7_owner_reservation_failed(&owner, exposed, now);
  gm7_scheduler_cancel_positive(&scheduler, now);
}
static int queue_at(unsigned slot, const struct gm7_packet *p,
                    struct gm7_window w, uint64_t deadline,
                    uint64_t request_utc, struct gm7_bridge b) {
  if (p->kind == GM7_REQUEST && !gm7_utc_valid(request_utc))
    return -EINVAL;
  bool first = slot == GM7_PRIMARY && owner.reserved &&
               owner.reserved_nominal == w.nominal && p->kind == GM7_ANNOUNCE &&
               owner.reserved_sequence == p->body.control.sequence;
  uint64_t ticket;
  int rc =
      gm7_scheduler_submit(&scheduler, slot, p, &group, w,
                           owner.source.until_us, deadline, 0, b.hf, &ticket);
  if (rc) {
    if (first)
      reservation_fail(w.nominal, p->body.control.sequence, RESERVE_SCHEDULER,
                       rc, false, b.hf);
    return rc;
  }
  if (p->kind == GM7_REQUEST)
    scheduler.job[slot].request_utc = request_utc;
  if (owner.event.valid &&
      p->body.control.sequence == owner.event.packet.body.control.sequence &&
      (p->kind == GM7_WITHDRAW ||
       (p->kind == GM7_ANNOUNCE && (p->flags & GM7_YIELD)))) {
    if (event_evidence.sequence != p->body.control.sequence) {
      event_evidence.queued = event_evidence.completed = event_evidence.failed =
          0;
      event_evidence.sequence = p->body.control.sequence;
    }
    ++event_evidence.queued;
  }
  rc = r2_gm7_controller_submit(slot, &scheduler, own_address, b.hf);
  if (rc) {
    if (first)
      reservation_fail(w.nominal, p->body.control.sequence, RESERVE_NATIVE, rc,
                       false, b.hf);
    gm7_scheduler_abort(&scheduler, slot, ticket, b.hf);
    if (!r2_gm7_controller_drain(slot, b.hf)) {
      if (event_evidence.sequence == scheduler.job[slot].sequence)
        ++event_evidence.failed;
      gm7_scheduler_release(&scheduler, slot, ticket, true, b.hf);
    }
  } else if (first)
    reservation_note(RESERVE_NATIVE, 0, false);
  return rc;
}
static int queue(unsigned slot, const struct gm7_packet *p, struct gm7_window w,
                 uint64_t deadline, struct gm7_bridge b) {
  return queue_at(slot, p, w, deadline, 0, b);
}
static void results(struct gm7_bridge b) {
  if (!bridge(&b) || b.epoch != clock_epoch)
    return;
  struct r2_gm7_tx result;
  for (unsigned count = 0;
       count < GM7_JOB_SLOTS && r2_gm7_controller_result(&result); count++) {
    if (result.slot >= GM7_JOB_SLOTS)
      continue;
    struct gm7_job *job = &scheduler.job[result.slot];
    if (job->ticket != result.ticket || job->generation != result.generation ||
        job->buffer_version != result.buffer_version)
      continue;
    uint64_t start_lo, start_hi, aux_lo, aux_hi, end_lo, end_hi;
    bool valid = result.complete &&
                 r2_gm7_clock_bounds(result.primary_address, result.epoch, 40,
                                     1, &start_lo, &start_hi) &&
                 r2_gm7_clock_bounds(result.aux_address, result.epoch, 24, 1,
                                     &aux_lo, &aux_hi) &&
                 r2_gm7_clock_bounds(result.aux_end, result.epoch, 0, 1,
                                     &end_lo, &end_hi) &&
                 aux_lo >= start_hi && aux_lo - start_hi >= 510 * 16 &&
                 aux_hi >= start_lo && aux_hi - start_lo <= 540 * 16 &&
                 end_lo > aux_hi;
    uint64_t first = valid ? start_lo / 16 : 0,
             end = valid ? (end_hi + 15) / 16 : 0;
    if (valid)
      valid = !gm7_scheduler_started(&scheduler, result.slot, result.ticket,
                                     result.generation, result.buffer_version,
                                     first) &&
              gm7_scheduler_aux_allowed(
                  &scheduler, result.slot, result.ticket, result.generation,
                  result.buffer_version, first, 1, true) &&
              !gm7_scheduler_complete(&scheduler, result.slot, result.ticket,
                                      result.generation, result.buffer_version,
                                      end, true);
    struct gm7_packet packet;
    if (valid && !gm7_ad_decode(&packet, job->ad, job->length, &group)) {
      if (packet.kind == GM7_REQUEST &&
          !gm7_request_sent(&owner.requester, &packet.body.request, first,
                            b.hf)) {
        uint64_t utc, next;
        for (unsigned i = 0; i < GM7_PEERS; i++) {
          struct gm7_peer *p = &owner.peer[i];
          if (!p->used || p->identity != packet.body.request.target ||
              p->boot != packet.body.request.target_boot)
            continue;
          if (p->retry_attempt == UINT64_MAX || b.hf < first ||
              first < job->window.nominal ||
              !gm7_utc_valid(job->request_utc) ||
              job->request_utc >
                  UINT64_MAX - (first - job->window.nominal)) {
            resource_fault = true;
            break;
          }
          /* The completing ticket owns this guess even if a later locator,
           * source snapshot or selected root changes before worker delivery. */
          utc = job->request_utc + first - job->window.nominal;
          uint32_t offset;
          if (utc > UINT64_MAX - 2000000 ||
              gm7_status_offset(&offset, identity, boot,
                                (utc + 2000000) / 2000000,
                                gm7_status_jitter(identity, boot,
                                                  (utc + 2000000) / 2000000)) ||
              gm7_retry_after(&next, utc, ++p->retry_attempt, offset) ||
              next < utc || first > UINT64_MAX - (next - utc)) {
            resource_fault = true;
            break;
          }
          p->retry_offset = (uint16_t)(offset - 89750);
          p->retry_at = first + next - utc;
          break;
        }
      }
      if (owner.reserved && result.slot == GM7_PRIMARY &&
          job->sequence == owner.reserved_sequence) {
        int rc = gm7_owner_reservation_commit(&owner, first, end, b.hf);
        if (rc)
          reservation_fail(job->window.nominal, job->sequence, RESERVE_COMPLETE,
                           rc, true, b.hf);
        else
          reservation_note(RESERVE_COMPLETE, 0, false);
      }
      r2_ble_timing_lamps(true, false, b.kernel_hi);
    } else {
      gm7_scheduler_abort(&scheduler, result.slot, result.ticket, b.hf);
      if (result.slot == GM7_PRIMARY)
        reservation_fail(job->window.nominal, job->sequence, RESERVE_COMPLETE,
                         -EIO, result.exposed, b.hf);
    }
  }
  for (unsigned slot = 0; slot < GM7_JOB_SLOTS; slot++) {
    struct gm7_job *j = &scheduler.job[slot];
    if (j->state == GM7_JOB_EMPTY)
      continue;
    /* Queued/preempted/aborted jobs have an original finite retirement point.
     */
    if (j->state == GM7_JOB_COMPLETE || j->state == GM7_JOB_ABORTED ||
        b.hf > j->window.latest_end) {
      if (!r2_gm7_controller_drain(slot, b.hf)) {
        if (j->state != GM7_JOB_COMPLETE && j->state != GM7_JOB_ABORTED)
          gm7_scheduler_abort(&scheduler, slot, j->ticket, b.hf);
        if (slot == GM7_PRIMARY)
          reservation_fail(j->window.nominal, j->sequence, RESERVE_EXPIRED,
                           -ETIME, true, b.hf);
        if (event_evidence.sequence == j->sequence &&
            (j->kind == GM7_WITHDRAW || j->negative ||
             (owner.event.valid && (owner.event.packet.flags & GM7_YIELD)))) {
          if (j->state == GM7_JOB_COMPLETE)
            ++event_evidence.completed;
          else
            ++event_evidence.failed;
        }
        (void)gm7_scheduler_release(&scheduler, slot, j->ticket, true, b.hf);
      }
    }
  }
  /* Process measured results before original-end retirement: worker latency
   * cannot turn a valid original RF completion into a missed event. Unknown
   * exposure remains conservative, including an orphan without a ticket. */
  if (owner.reserved && b.hf > owner.reserved_end)
    reservation_fail(owner.reserved_nominal, owner.reserved_sequence,
                     RESERVE_EXPIRED, -ETIME, true, b.hf);
}
static struct r2_gm7_admission_source admission(struct gm7_bridge b) {
  uint64_t until = 0, utc = owner.source.utc_us;
  bool source = gm7_owner_output(&owner, b.hf) &&
                gm7_bridge_to_kernel(b, owner.source.until_us, &until);
  if (b.hf >= owner.source.sampled_us &&
      utc <= UINT64_MAX - (b.hf - owner.source.sampled_us))
    utc += b.hf - owner.source.sampled_us;
  else
    source = false;
  return (struct r2_gm7_admission_source){.group = &group,
                                          .identity = identity,
                                          .boot = boot,
                                          .utc_us = utc,
                                          .sampled_us = b.kernel_hi,
                                          .until_us = until,
                                          .timing_epoch =
                                              owner.scope.timing_epoch,
                                          .train = owner.scope.train,
                                          .map_bound_us = roster.map_bound_us,
                                          .name = name,
                                          .serving = source};
}
static __attribute__((noinline)) void
timing(struct gm7_bridge b, const struct gm7_source *s,
       const struct r2_source_clock *map, const struct r2_clock_view *clock) {
  struct r2_radio_scope scope = {0};
  struct r2_group_client anchor = {0};
  uint64_t until = 0;
  int64_t utc = -1;
  if (gm7_owner_output(&owner, b.hf) &&
      gm7_bridge_to_kernel(b, s->until_us, &until)) {
    scope = (struct r2_radio_scope){.root = 0,
                                    .root_identity = identity,
                                    .boot = boot,
                                    .source_epoch = owner.scope.timing_epoch,
                                    .local_source_epoch = s->source_gen,
                                    .publication_epoch = owner.scope.train,
                                    .until_us = until,
                                    .gm = true};
    utc = (int64_t)s->utc_us;
  } else if (owner.scope.active && owner.scope.active != identity) {
    for (unsigned i = 0; i < GM7_PEERS; i++) {
      const struct gm7_peer *p = &owner.peer[i];
      if (!p->used || p->identity != owner.scope.active ||
          p->boot != owner.scope.boot || !gm7_session_live(&p->session, b.hf))
        continue;
      const struct gm7_bound_anchor *a = &p->session.descriptor;
      const struct gm7_control *sample = &p->session.control;
      uint64_t lo, hi;
      if (!(p->session.anchor_flags & GM7_A_SERVING) || !sample->sent_utc_us ||
          !p->session.control_until ||
          gm7_session_map(&p->session, sample->sent_mono_us, b.hf, &lo, &hi) ||
          b.hf < lo)
        continue;
      uint64_t remaining = owner.selected_source_until;
      if (!gm7_bridge_to_kernel(b, remaining, &until))
        until = b.kernel_hi;
      uint64_t projected = sample->sent_utc_us + (b.hf - lo), width = hi - lo;
      anchor = (struct r2_group_client){
          .passive = true,
          .source = true,
          .root = p->identity,
          .boot = p->boot,
          .peer = p->identity,
          .peer_boot = p->boot,
          .source_epoch = a->timing_epoch,
          .publication = a->train_gen,
          .utc_us = projected,
          .at = b.kernel_hi,
          .until = until,
          .map_until = until,
          .uncertainty_us = width > UINT32_MAX ? UINT32_MAX : (uint32_t)width};
      /* Bounded local roster representation, independent of enrolled4
       * identities. */
      if (roster.identity[1] != p->identity) {
        roster.identity[1] = p->identity;
        uint8_t derived[16];
        r2_group_pair_key(derived, &group, p->identity, p->identity);
        memcpy(roster.address[1], derived, 6);
        roster.address[1][5] |= 0xc0;
      }
      scope = (struct r2_radio_scope){.root = 1,
                                      .root_identity = p->identity,
                                      .boot = p->boot,
                                      .source_epoch = a->timing_epoch,
                                      .publication_epoch = a->train_gen,
                                      .until_us = until,
                                      .follower = true};
      utc = (int64_t)projected;
      break;
    }
  }
  struct r2_enrollment path = {.group = group,
                               .count = 2,
                               .map_bound_us = roster.map_bound_us,
                               .local_capabilities = 3,
                               .gm_mask = 1,
                               .gnss_mask = 1};
  memcpy(path.identity, roster.identity, sizeof roster.identity);
  memcpy(path.address, roster.address, sizeof roster.address);
  r2_radio_service_client(&anchor, &path, b.kernel_hi);
  struct r2_role_record role = {.sender = identity,
                                .sender_boot = boot,
                                .root = scope.root_identity,
                                .root_boot = scope.boot,
                                .source_epoch = scope.source_epoch,
                                .created_us = b.kernel_hi,
                                .valid_us = 1500000,
                                .flags = scope.gm ? R2_ROLE_SERVING : 0,
                                .members = 1};
  r2_radio_service_role(&role, &group);
  struct r2_gnss_record reference = {0};
  struct gm7_packet diagnostic_packet;
  if (scope.gm && !gm7_gnss_snapshot(&owner, &diagnostic_packet, clock, b.hf)) {
    const struct gm7_gnss *g = &diagnostic_packet.body.gnss;
    gm7_legacy_gnss(&reference, g, b.kernel_hi);
  }
  r2_radio_service_tick_reference(
      scope, map, utc >= 0 && utc <= INT64_MAX / 1000 ? utc * 1000 : -1,
      b.kernel_hi, scope.gm ? &reference : NULL);
  r2_ble_serving(scope.gm);
  r2_lora_origin(scope.gm, boot, scope.source_epoch);
}
/* A MAC-valid locator is only a bounded guess at a REQUEST opportunity. An
 * ordinary ANNOUNCE sample precedes the38ms primary in the same250ms UTC
 * frame. Its complete LE2M AUX END is nominally525us plus its full packet
 * airtime after primary START. A5ms
 * discovery copy, controller lateness, propagation or a replay can make this
 * guess wrong; none of it establishes physical phase or source authority.
 * Use it immediately, not as a renewed locator/map cache. */
static void tentative_request(const struct gm7_packet *locator, uint64_t rx,
                               struct gm7_bridge b) {
  const uint32_t end_phase =
      GM7_PRIMARY_US + GM7_AUX_TARGET_US +
      (11 + 10 + GM7_CONTROL_BYTES + 4) * 4;
  if (locator->kind != GM7_ANNOUNCE || (locator->flags & GM7_YIELD) ||
      resource_fault || owner.failed || owner.stopped ||
      scheduler.job[GM7_STANDBY].state != GM7_JOB_EMPTY ||
      rx > b.hf || b.hf - rx >= 250000 ||
      rx < end_phase)
    return;
  const struct gm7_control *c = &locator->body.control;
  struct gm7_peer *peer = NULL;
  for (unsigned i = 0; i < GM7_PEERS; i++)
    if (owner.peer[i].used && !owner.peer[i].quarantined &&
        owner.peer[i].identity == c->sender && owner.peer[i].boot == c->boot) {
      peer = &owner.peer[i];
      break;
    }
  if (!peer || !peer->locator_seen || b.hf < peer->locator_at ||
      b.hf - peer->locator_at >= 4500000 ||
      !gm7_session_due(&peer->session, b.hf))
    return;
  uint64_t base = rx - end_phase;
  uint64_t frame_utc = c->sent_utc_us - c->sent_utc_us % GM7_FRAME_US;
  uint64_t cycle = frame_utc / 2000000, tx;
  uint32_t offset;
  if (!gm7_utc_valid(c->sent_utc_us) ||
      gm7_status_offset(&offset, identity, boot, cycle,
                        gm7_status_jitter(identity, boot, cycle)))
    return;
  if (peer->retry_attempt) {
    tx = peer->retry_at;
    if (tx < b.hf + 3000) {
      uint64_t frames = (b.hf + 3000 - tx + 999999) / 1000000;
      if (frames > (UINT64_MAX - tx) / 1000000)
        return;
      tx += frames * 1000000;
      peer->retry_at = tx; /* Same A1 residue/frozen slot, no RF attempt. */
      if (owner.late_windows != UINT32_MAX)
        ++owner.late_windows;
    }
  } else {
    tx = base + offset;
    if (tx < b.hf + 3000)
      tx += GM7_FRAME_US;
  }
  /* A fresh primary/copy normally leaves about45..70ms CPU preparation.
   * Misses wait another locator. Native admission still samples actual HF and
   * requires>=3ms; this does not enlarge any RF window or nonce deadline. */
  if (tx < b.hf + 3000 || tx > b.hf + 70000 || tx < peer->retry_at ||
      tx < base || frame_utc > UINT64_MAX - (tx - base))
    return;
  struct gm7_packet request;
  if (!gm7_owner_request_peer(&owner, &request, peer->identity, peer->boot,
                              b.hf, tx, entropy, NULL))
    (void)queue_at(GM7_STANDBY, &request, window(tx, 2000), tx + 250000,
                    frame_utc + tx - base, b);
}
static __attribute__((noinline)) void receive(struct gm7_bridge b, uint64_t utc,
                                              uint64_t frame_start,
                                              uint64_t frame,
                                              uint32_t enclosure,
                                              bool physical_aligned) {
  struct r2_gm7_rx r;
  for (unsigned count = 0; count < 2 && r2_gm7_capture_pop(&r); count++) {
    management_count(&management_rx);
    uint64_t lo, hi;
    if (!r2_gm7_clock_bounds(r.raw, r.epoch, 0, 5, &lo, &hi) ||
        r.epoch != b.epoch)
      continue;
    uint64_t rx = (hi + 15) / 16;
    struct gm7_packet packet;
    if (gm7_ad_decode(&packet, r.ad, r.length, &group))
      continue;
    management_count(&management_mac);
    if (!bridge(&b) || b.epoch != clock_epoch)
      return;
    if (packet.kind == GM7_REQUEST) {
      struct gm7_window w;
      if (!gm7_response_window(&w, lo / 16, frame_start, frame, enclosure) &&
          utc &&
          !gm7_owner_anchor(
              &owner, &packet,
              &(struct gm7_packet){.group = group.id,
                                   .key_epoch = group.epoch,
                                   .kind = GM7_REQUEST,
                                   .body.request = packet.body.request},
              rx, b.hf)) {
        management_count(&management_accepted);
        (void)queue(GM7_SECONDARY, &packet, w, lo / 16 + 250000, b);
      }
    } else {
      if (!bridge(&b) || b.epoch != clock_epoch)
        return;
      uint32_t generation = owner.event_generation;
      if (!gm7_owner_receive(&owner, r.ad, r.length, r.address, rx, b.hf))
        management_count(&management_accepted);
      /* Owner receive re-samples processing time after decode. Its next API
       * must use a contemporary bridge; captured RX and leases stay original. */
      if (!bridge(&b) || b.epoch != clock_epoch)
        return;
      if (generation != owner.event_generation) {
        if (!owner.serving)
          r2_radio_service_withdraw();
        gm7_scheduler_cancel_positive(&scheduler, b.hf);
        r2_gm7_controller_cancel(b.hf);
      }
      if (!physical_aligned)
        tentative_request(&packet, rx, b);
    }
  }
}
static __attribute__((noinline)) void publish(struct gm7_bridge b) {
  struct r2_radio_precision_view v;
  struct r2_group_client t, c;
  uint64_t frame;
  r2_radio_service_snapshot(&v, &t, &c, &frame, b.kernel_hi);
  struct r2_link_quality_view quality;
  r2_radio_service_link_quality(&quality);
  (void)k_mutex_lock(&lock, K_FOREVER);
  diagnostic.birth = owner.scope.birth;
  diagnostic.founder = owner.scope.founder;
  diagnostic.term = owner.scope.term;
  diagnostic.active = owner.scope.active;
  diagnostic.active_boot = owner.scope.boot;
  diagnostic.at = b.kernel_hi;
  diagnostic.role =
      owner.armed     ? GM7_W_HOT
      : owner.serving ? GM7_W_ACTIVE
      : (owner.selected.hot == identity && owner.selected.hot_boot == boot)
          ? GM7_W_PREPARING
          : GM7_W_STANDBY;
  diagnostic.known = 0;
  diagnostic.reason = owner.reason;
  diagnostic.blockers = owner.blockers;
  diagnostic.pending = owner.reserved;
  bool event_match =
      owner.event.valid &&
      event_evidence.sequence == owner.event.packet.body.control.sequence;
  diagnostic.event_queued = event_match ? event_evidence.queued : 0;
  diagnostic.event_completed = event_match ? event_evidence.completed : 0;
  diagnostic.event_failed = event_match ? event_evidence.failed : 0;
  diagnostic.event_unsent = owner.event.valid && diagnostic.event_queued < 3
                                ? 3 - diagnostic.event_queued
                                : 0;
  diagnostic.hot_ms = diagnostic.active_ms = diagnostic.pps_ms = UINT32_MAX;
  if (owner.armed && b.hf >= owner.armed_since) {
    diagnostic.known |= 1;
    diagnostic.hot_ms = gm7_duration_ms(b.hf, owner.armed_since);
  }
  if (gm7_owner_output(&owner, b.hf)) {
    diagnostic.known |= 2;
    diagnostic.active_ms = gm7_duration_ms(b.hf, owner.active_since);
  }
  if (owner.pps_observing && owner.source.pps_healthy &&
      b.hf >= owner.pps_since && b.hf >= owner.source.pps_at &&
      b.hf - owner.source.pps_at < R2_PPS_FRESH_US) {
    diagnostic.known |= 4;
    diagnostic.pps_ms = gm7_duration_ms(b.hf, owner.pps_since);
  }
  for (unsigned i = 0; i < GM7_PEERS; i++) {
    peer_display[i].valid = false;
    peer_display[i].current = false;
    struct gm7_reported_role report;
    bool current;
    uint64_t expires;
    if (gm7_peer_role(&owner, i, b.hf, &report, &current) &&
        gm7_bridge_to_kernel(b, report.until, &expires)) {
      report.until = expires;
      peer_display[i].report = report;
      peer_display[i].identity = owner.peer[i].identity;
      peer_display[i].boot = owner.peer[i].boot;
      peer_display[i].valid = true;
      peer_display[i].current = current;
    }
  }
  status.at = b.kernel_hi;
  status.profile_incarnation = boot;
  status.group_id = group.id;
  status.key_epoch = group.epoch;
  status.serving =
      gm7_owner_output(&owner, b.hf) && r2_radio_service_serving(b.kernel_hi);
  status.hot_ready = owner.armed;
  status.gm_cold = !owner.serving && !owner.armed && !owner.prepared;
  status.gm_capable = true;
  status.source_eligible = owner.eligible;
  status.admission_ms =
      owner.source_observing && b.hf >= owner.source_since &&
              b.hf - owner.source_since < GM7_DWELL_US
          ? (uint32_t)((GM7_DWELL_US - (b.hf - owner.source_since) + 999) /
                       1000)
          : 0;
  status.quality = (struct r2_clock_quality){
      .identity = identity,
      .priority1 = owner.source.quality.priority1,
      .clock_class = owner.eligible ? owner.source.quality.clock_class : 255,
      .accuracy = 255,
      .variance = 65535,
      .priority2 = owner.source.quality.priority2};
  status.hot_refusal = owner.blockers;
  status.precision = v;
  status.reception = quality;
  status.identities[0] = identity;
  status.identities[1] =
      owner.scope.active == identity ? 0 : owner.scope.active;
  status.root = owner.scope.active == identity ? 0
                : owner.scope.active           ? 1
                                               : 255;
  status.self = 0;
  status.coordinator = 255;
  status.standby = owner.armed ? 0 : 255;
  status.root_boot = owner.scope.boot;
  status.source_epoch = owner.scope.timing_epoch;
  status.local_source_epoch = owner.source.source_gen;
  status.follower = t.source;
  status.follower_clock_valid = t.source;
  status.follower_utc = t.utc_us;
  status.time_sampled_us = t.at;
  status.source_frame_ns = frame;
  status.service_until = t.map_until;
  status.authority_until = t.until;
  status.display_coarse_valid = c.source;
  status.display_coarse_utc = c.utc_us;
  status.display_coarse_at = c.at;
  uint32_t join_rx = r2_ble_receipts();
  uint32_t received = (uint32_t)atomic_get(&management_rx);
  status.rx = received > UINT32_MAX - join_rx ? UINT32_MAX : received + join_rx;
  status.authenticated = (uint32_t)atomic_get(
      &management_mac); /* Verified GM7 MACs, never local TX sequences. */
  status.rejected = owner.drops;
  status.drops = r2_ble_drops();
  status.errors = r2_ble_errors();
  status.backlog = r2_ble_backlog();
  memcpy(status.name, name, 25);
  r2_ble_lamps(status.lamps, b.kernel_hi);
  r2_ble_fault_snapshot(&status.transport_fault);
  if (status.cycles != UINT32_MAX)
    ++status.cycles;
  (void)k_mutex_unlock(&lock);
}
static void cycle(struct k_work *work) {
  (void)work;
  uint64_t cycle_started = kernel_now();
  (void)k_mutex_lock(&lock, K_FOREVER);
  status.supervision.started = cycle_started;
  status.supervision.stage_at = cycle_started;
  status.supervision.state = 2;
  status.supervision.stage = 1;
  uint64_t queued = cycle_started >= status.supervision.submitted
                        ? cycle_started - status.supervision.submitted
                        : 0;
  if (queued > status.supervision.queue_max_us)
    status.supervision.queue_max_us =
        queued > UINT32_MAX ? UINT32_MAX : (uint32_t)queued;
  (void)k_mutex_unlock(&lock);
  struct gm7_bridge b;
  if (!bridge(&b) || b.epoch != clock_epoch) {
    phase_known = false;
    r2_radio_service_withdraw();
    r2_gm7_controller_cancel(0);
    r2_gm7_controller_precision(false, 0, 0, 0);
    gm7_owner_stop(&owner, UINT64_MAX);
    resource_fault = true;
    status.error = -ESTALE;
    r2_service_stop();
    r2_gm7_controller_scan(false, false, 0);
    r2_ble_stop();
    r2_ble_poll(kernel_now());
    r2_radio_service_stop(kernel_now());
    r2_relay_service_stop(kernel_now());
    for (unsigned slot = 0; slot < GM7_JOB_SLOTS; slot++)
      (void)r2_gm7_controller_drain(slot, 0);
    if (r2_ble_closed() && r2_radio_service_closed() &&
        r2_relay_service_closed() && r2_gm7_controller_idle()) {
      status.closed = true;
      status.active = false;
      status.closed_at = kernel_now();
      r2_ble_gm7_server(false);
    }
    goto finished;
  }
  uint64_t negative_base = 0;
  uint32_t negative_enclosure = 0;
  bool negative_aligned =
      negative_phase(b, &negative_base, &negative_enclosure);
  r2_ble_owner_deadline(cycle_started + 100000);
  if (resuming) {
    int rc = r2_radio_service_reset(reset_generation);
    if (rc == -EAGAIN)
      goto finished;
    if (!rc)
      rc = r2_relay_service_profile_reset(resume_boot);
    (void)k_mutex_lock(&lock, K_FOREVER);
    /* A stop may arrive during either asynchronous reset. It wins before
     * committing the next incarnation; the worker never clears that stop. */
    if (!resuming || atomic_get(&stopping)) {
      (void)k_mutex_unlock(&lock);
      goto finished;
    }
    if (rc) {
      status.error = rc;
      resuming = status.profile_pending = false;
      (void)k_mutex_unlock(&lock);
      r2_service_stop();
      goto finished;
    }
    r2_group_admission_reset();
    boot = resume_boot;
    used = started = transport_stopped = stop_begun = false;
    resuming = false;
    status.profile_pending = false;
    status.error = 0;
    rc = start(kernel_now() / 1000, 0);
    if (rc) {
      status.error = rc;
      /* A failed start may have acquired transport resources. Retain the
       * worker's cleanup ownership until the ordinary drain completes. */
      started = true;
    }
    (void)k_mutex_unlock(&lock);
    if (rc)
      r2_service_stop();
    goto finished;
  }
  if (atomic_get(&stopping)) {
    if (!stop_begun) {
      fence(GM7_R_STOP, b.hf);
      r2_gm7_controller_scan(false, false, 0);
      r2_gm7_controller_precision(false, 0, 0, 0);
      r2_ble_stop();
      stop_begun = true;
    }
    r2_ble_poll(kernel_now());
    r2_radio_service_stop(kernel_now());
    r2_relay_service_stop(kernel_now());
    results(b);
    if (owner.event.valid && owner.event.negative && owner.event.sent < 3 &&
        b.hf < owner.event.deadline && negative_aligned) {
      uint64_t nominal = negative_base + GM7_PRIMARY_US;
      if (nominal < b.hf + 3000)
        nominal += GM7_FRAME_US;
      if (owner.event.first == UINT64_MAX)
        (void)gm7_owner_event_align(&owner, nominal, b.hf);
      if (nominal >= owner.event.first &&
          (nominal - owner.event.first) % GM7_FRAME_US == 0) {
        uint64_t copy = (nominal - owner.event.first) / GM7_FRAME_US;
        if (copy < 3 && copy >= owner.event.sent &&
            nominal <= b.hf + PRIMARY_PREPARE_US &&
            scheduler.job[GM7_PRIMARY].state == GM7_JOB_EMPTY) {
          owner.event.sent = (uint8_t)(copy + 1);
          (void)queue(GM7_PRIMARY, &owner.event.packet,
                      window(nominal, negative_enclosure), owner.event.deadline,
                      b);
        }
      }
    }
    bool copies_done = !owner.event.valid || !owner.event.negative ||
                       owner.event.sent >= 3 || b.hf >= owner.event.deadline ||
                       !negative_aligned;
    if (copies_done) {
      gm7_owner_stop(&owner, b.hf);
      transport_stopped = true;
    }
    bool idle = transport_stopped && r2_ble_closed() &&
                r2_radio_service_closed() && r2_relay_service_closed() &&
                r2_gm7_controller_idle();
    if (idle) {
      r2_ble_gm7_server(false);
      (void)k_mutex_lock(&lock, K_FOREVER);
      status.closed = true;
      status.active = false;
      status.closed_at = kernel_now();
      (void)k_mutex_unlock(&lock);
    }
    goto finished;
  }
  struct r2_clock clock;
  struct r2_source_clock map;
  struct gm7_source source;
  if (!source_read(&source, &map, &clock, b)) {
    fence(GM7_R_SOURCE, b.hf);
    goto finished;
  }
  /* Capture may have been delayed by other work. Use a contemporary same-domain
   * coordinate for phase, while retaining the source's actual sample and its
   * original expiry for owner freshness/dwell. */
  if (!bridge(&b) || b.epoch != clock_epoch) {
    resource_fault = true;
    goto finished;
  }
  if (r2_gm7_capture_overflows() != overflow_seen) {
    overflow_seen = r2_gm7_capture_overflows();
    resource_fault = true;
    fence(GM7_R_RESOURCE, b.hf);
  }
  if (resource_fault) {
    source.physical_ready = source.gnss_healthy = false;
  }
  struct r2_gm7_timing t;
  r2_radio_service_gm7_timing(&t, b.kernel_hi);
  struct gm7_preparation prep;
  preparation(&prep, b, &t);
  uint64_t utc = 0;
  uint32_t enclosure = 100;
  bool projected = source_phase(&source, b, &utc);
  bool aligned = projected && source.physical_ready && b.hf < source.until_us;
  if (owner.scope.active && owner.scope.active != identity)
    aligned = phase_at(&t, b, &utc, &enclosure) && enclosure <= 2000;
  uint64_t frame = utc / GM7_FRAME_US, phase = utc % GM7_FRAME_US;
  uint64_t frame_start = b.hf >= phase ? b.hf - phase : 0;
  if (aligned) {
    phase_known = true;
    last_phase = (uint32_t)(frame_start % GM7_FRAME_US);
    last_enclosure = enclosure;
    event_evidence.phase_sample_ms = (uint32_t)(b.hf / 1000);
  }
  receive(b, utc, frame_start, frame, enclosure, aligned);
  if (!bridge(&b) || b.epoch != clock_epoch) {
    resource_fault = true;
    goto finished;
  }
  gm7_owner_update(&owner, &source, &prep, b.hf);
  if (owner.event_generation != event_generation) {
    event_generation = owner.event_generation;
    gm7_scheduler_cancel_positive(&scheduler, b.hf);
    r2_gm7_controller_cancel(b.hf);
  }
  r2_gm7_controller_source(owner.source.until_us, owner.source.physical_ready &&
                                                      owner.eligible &&
                                                      !resource_fault);
  if (scan_aligned != aligned) {
    int rc = r2_ble_gm7_scan(aligned);
    if (!rc)
      scan_aligned = aligned;
    else if (rc != -EAGAIN) {
      resource_fault = true;
      fence(GM7_R_RESOURCE, b.hf);
    }
  }
  r2_ble_poll(b.kernel_hi);
  int transport_error = r2_ble_stop_error();
  int precision_error = r2_radio_service_error();
  if (!precision_error)
    precision_error = r2_radio_service_supervise(b.kernel_hi);
  if (transport_error || precision_error) {
    status.error = transport_error ? transport_error : precision_error;
    r2_service_stop();
    goto finished;
  }
  struct r2_gm7_admission_source join = admission(b);
  r2_group_admission_gm7_tick(&join, b.kernel_hi);
  struct r2_ble_event event;
  for (unsigned count = 0;
       count < R2_OWNER_CONTROL_RX && r2_ble_receive(&event); count++)
    if (event.peer == r2_ble_admission_peer())
      r2_group_admission_gm7_receive(&join, &event, kernel_now());
  results(b);
  if (owner.armed && b.hf >= owner.selected_original &&
      b.hf - owner.selected_original >= 2400000 && !owner.serving) {
    if (!takeover_preparing) {
      r2_radio_service_handoff_request(owner.scope.active, owner.scope.boot,
                                       owner.scope.timing_epoch);
      takeover_preparing = true;
    }
    r2_radio_service_stop(b.kernel_hi);
  } else if (takeover_preparing && !owner.reserved && !owner.serving) {
    takeover_preparing = false;
    r2_radio_service_activate();
  }
  if (!bridge(&b) || b.epoch != clock_epoch) {
    resource_fault = true;
    goto finished;
  }
  r2_gm7_controller_precision(
      owner.serving && aligned, (uint32_t)(frame_start % 62500),
      takeover_bound ? takeover_bound : owner.source.until_us,
      owner.scope.train);
  if (!takeover_preparing || owner.serving)
    timing(b, &source, &map, &clock.view);
  if (owner.serving && takeover_bound) {
    struct r2_gm7_timing onair;
    r2_radio_service_gm7_timing(&onair, b.kernel_hi);
    if ((onair.first_tx_observed_complete &&
         (onair.first_tx_derived_end_hi + 15) / 16 > takeover_bound) ||
        (!onair.first_tx_observed_complete && b.hf >= takeover_bound)) {
      ++owner.missed_bounds;
      fence(GM7_R_MISSED_BOUND, b.hf);
      takeover_bound = 0;
    } else if (onair.first_tx_observed_complete)
      takeover_bound = 0;
  }
  if (owner.serving) {
    struct r2_gm7_timing onair;
    r2_radio_service_gm7_timing(&onair, b.kernel_hi);
    bool recent = onair.last_tx && b.hf * 16 >= onair.last_tx &&
                  b.hf * 16 - onair.last_tx <= 4000000;
    if (recent && (onair.tx_phase_us > 1500 || onair.tx_phase_us < -1500))
      fence(GM7_R_MISSED_BOUND, b.hf);
    else if (recent) {
      int step = gm7_phase_nudge(onair.tx_phase_us, onair.last_tx, b.hf * 16,
                                 phase_step_at);
      if (step) {
        int rc = r2_radio_service_gm7_phase_step(step);
        phase_step_at = onair.last_tx;
        if (rc && rc != -EAGAIN)
          fence(GM7_R_MISSED_BOUND, b.hf);
      }
    }
  }
  /* Receive/drain work can advance processing-now. Recheck the original
   * phase lease at the contemporary bridge before constructing a negative. */
  if (!aligned)
    negative_aligned = negative_phase(b, &negative_base, &negative_enclosure);
  bool negative_only =
      !aligned && negative_aligned && owner.event.valid && owner.event.negative;
  if (negative_only) {
    frame_start = negative_base;
    frame = b.hf / GM7_FRAME_US;
    enclosure = negative_enclosure;
  }
  if ((aligned || negative_only) && frame_start) {
    uint64_t nominal = frame_start + GM7_PRIMARY_US;
    if (nominal >= b.hf + 3000 && nominal <= b.hf + PRIMARY_PREPARE_US &&
        primary_frame != frame &&
        scheduler.job[GM7_PRIMARY].state == GM7_JOB_EMPTY) {
      if (!owner.serving &&
          (owner.initial_due ||
           gm7_owner_takeover_at(&owner, nominal, nominal + 4000, b.hf))) {
        bool ready = true;
        if (owner.armed) {
          ready = takeover_preparing && r2_radio_service_closed() &&
                  r2_radio_service_handoff_ready(owner.scope.active,
                                                 owner.scope.boot,
                                                 owner.scope.timing_epoch);
          uint64_t bound = owner.selected_original + GM7_BLACKOUT_BOUND_US;
          /* Leave a finite100ms native G1 acquisition/start/first-event budget;
           * actual first START is checked independently, never HCI acceptance.
           */
          if (nominal + 104000 > bound) {
            ready = false;
            ++owner.missed_bounds;
            owner.reason = GM7_R_MISSED_BOUND;
          }
          if (ready && !promoted_valid) {
            uint64_t raw_pps = map.last_edge;
            struct r2_capture current;
            ready = r2_capture_now(&current) &&
                    !r2_radio_service_gm7_promote(
                        &map, owner.scope.active, owner.scope.boot,
                        owner.scope.timing_epoch, owner.scope.train,
                        source.source_gen, map.utc_association, current);
            if (ready) {
              (void)k_mutex_lock(&lock, K_FOREVER);
              ready = !input.stopped && !input.fatal &&
                      input.view.source_epoch == source.source_gen &&
                      functional_source.epoch == current.epoch &&
                      functional_source.last_edge == raw_pps;
              if (ready) {
                functional_source = map;
                promoted_valid = true;
                promoted_pps = raw_pps;
              }
              (void)k_mutex_unlock(&lock);
              takeover_bound = bound;
            }
          }
        }
        if (ready) {
          primary_frame = frame; /* Consume this original attempt on refusal. */
          int rc = gm7_owner_reserve(&owner, nominal, nominal + 4000, b.hf);
          reservation_note(RESERVE_ADMITTED, rc, rc != 0);
        }
      }
      struct gm7_packet p;
      int rc = owner.reserved ? gm7_owner_planned_control(&owner, &p, b.hf)
                              : gm7_owner_control(&owner, &p, b.hf);
      if (rc && owner.reserved)
        reservation_fail(nominal, owner.reserved_sequence, RESERVE_CONTROL, rc,
                         false, b.hf);
      if (!rc && (p.kind == GM7_ANNOUNCE || p.kind == GM7_WITHDRAW)) {
        primary_frame = frame;
        bool aligned_event = true;
        if (owner.event.valid && !owner.event.sent &&
            (owner.event.first == UINT64_MAX ||
             owner.event.first == owner.event.packet.body.control.sent_mono_us))
          aligned_event = !gm7_owner_event_align(&owner, nominal, b.hf);
        uint64_t copy = owner.event.valid && nominal >= owner.event.first
                            ? (nominal - owner.event.first) / GM7_FRAME_US
                            : 0;
        if (aligned_event &&
            (!owner.event.valid ||
             (copy < 3 && copy >= owner.event.sent &&
              nominal == owner.event.first + copy * GM7_FRAME_US))) {
          /* One original opportunity, including queue refusal. Retrying it at
           * another poll cannot create an extra fourth event/counter. */
          if (owner.event.valid)
            owner.event.sent = (uint8_t)(copy + 1);
          if (!queue(GM7_PRIMARY, &p, window(nominal, enclosure), 0, b)) {
            primary_frame = frame;
            if (gm7_discovery_policy(
                    identity, frame, true, owner.serving || owner.reserved,
                    owner.event.valid, owner.withdrawn) == GM7_DISCOVERY_COPY &&
                p.kind == GM7_ANNOUNCE && !(p.flags & GM7_YIELD)) {
              if (!queue(GM7_SECONDARY, &p, window(nominal + 5000, enclosure),
                         0, b))
                diag_frame = frame;
            }
          }
        }
      }
    }
    /* A retained negative phase cannot schedule STATUS/bootstrap/diagnostics.
     */
    if (negative_only)
      goto published;
    uint32_t offset;
    uint64_t cycle = utc / 2000000;
    bool ordinary = !owner.serving && cycle != status_cycle;
    if (!gm7_status_offset(&offset, identity, boot, cycle,
                           gm7_status_jitter(identity, boot, cycle))) {
      uint64_t initial_tx = frame_start + offset, tx = initial_tx;
      struct gm7_peer *request_peer = NULL;
      if (!ordinary) {
        for (unsigned i = 0; i < GM7_PEERS; i++) {
          struct gm7_peer *p = &owner.peer[i];
          if (!p->used || !p->locator_seen || b.hf < p->locator_at ||
              b.hf - p->locator_at >= 4500000 ||
              !gm7_session_due(&p->session, b.hf))
            continue;
          uint64_t candidate = initial_tx;
          if (p->retry_attempt) {
            candidate = p->retry_at;
            if (candidate < b.hf + 3000) {
              uint64_t frames = (b.hf + 3000 - candidate + 999999) / 1000000;
              if (frames > (UINT64_MAX - candidate) / 1000000) {
                resource_fault = true;
                continue;
              }
              candidate += frames * 1000000;
              p->retry_at =
                  candidate; /* Same residue/frozen slot; no RF attempt. */
              if (owner.late_windows < UINT32_MAX)
                ++owner.late_windows;
            }
          }
          if (candidate < b.hf + 3000 || candidate > b.hf + 7000 ||
              candidate < p->retry_at)
            continue;
          if (!request_peer || p->retry_at < request_peer->retry_at ||
              (p->retry_at == request_peer->retry_at &&
               p->identity < request_peer->identity)) {
            request_peer = p;
            tx = candidate;
          }
        }
      }
      if (!owner.reserved && (ordinary || request_peer) && tx >= b.hf + 3000 &&
          tx <= b.hf + 7000 &&
          scheduler.job[GM7_STANDBY].state == GM7_JOB_EMPTY) {
        struct gm7_packet p;
        int rc = ordinary ? gm7_owner_control(&owner, &p, b.hf)
                          : gm7_owner_request_peer(
                                &owner, &p, request_peer->identity,
                                request_peer->boot, b.hf, tx, entropy, NULL);
        if (!rc &&
            !queue_at(GM7_STANDBY, &p, window(tx, enclosure),
                      p.kind == GM7_REQUEST ? tx + 250000 : 0,
                      p.kind == GM7_REQUEST
                          ? frame * GM7_FRAME_US + tx - frame_start
                          : 0,
                      b) &&
            ordinary)
          status_cycle = cycle;
      }
    }
    uint64_t secondary = frame_start + GM7_SECONDARY_US;
    if (secondary >= b.hf + 3000 && secondary <= b.hf + 7000 &&
        diag_frame != frame &&
        scheduler.job[GM7_SECONDARY].state == GM7_JOB_EMPTY) {
      struct gm7_packet p;
      enum gm7_discovery policy = gm7_discovery_policy(
          identity, frame, true, owner.serving && !resource_fault,
          owner.event.valid, owner.withdrawn);
      if (policy == GM7_DISCOVERY_COPY &&
          scheduler.job[GM7_PRIMARY].state == GM7_JOB_COMPLETE &&
          !gm7_ad_decode(&p, scheduler.job[GM7_PRIMARY].ad,
                         scheduler.job[GM7_PRIMARY].length, &group)) {
        if (!queue(GM7_SECONDARY, &p, window(secondary, enclosure), 0, b))
          diag_frame = frame;
      } else if (policy == GM7_DISCOVERY_ORDINARY &&
                 frame % 8 ==
                     1 + gm7_status_slot(identity, boot, cycle) % 3 +
                         4 * ((gm7_status_slot(identity, boot, cycle) / 3) &
                              1) &&
                 !gm7_role_snapshot(&owner, &p, b.hf)) {
        if (!queue(GM7_SECONDARY, &p, window(secondary, enclosure), 0, b))
          diag_frame = frame;
      }
    }
  }
published:
  publish(b);
finished:
  (void)k_mutex_lock(&lock, K_FOREVER);
  uint64_t end = kernel_now();
  if (end >= cycle_started && end - cycle_started > status.worker_max_us)
    status.worker_max_us = end - cycle_started > UINT32_MAX
                               ? UINT32_MAX
                               : (uint32_t)(end - cycle_started);
  uint64_t total = end >= status.supervision.submitted
                       ? end - status.supervision.submitted
                       : UINT64_MAX;
  if (total > status.supervision.owner_max_us)
    status.supervision.owner_max_us =
        total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
  status.supervision.stage = 0;
  status.supervision.state = 0;
  atomic_clear(&busy);
  (void)k_mutex_unlock(&lock);
}
K_WORK_DEFINE(service_work, cycle);
static void timer(struct k_timer *timer) {
  (void)timer;
  k_sem_give(&supervise_requested);
}
K_TIMER_DEFINE(service_timer, timer, NULL);
int r2_service_configure(const struct r2_identity *id,
                         const struct r2_enrollment *e) {
  if (configured || !id || id->result || !id->boot || !e ||
      !r2_group_valid(&e->group) || !e->map_bound_us || e->map_bound_us > 50000)
    return -EINVAL;
  identity = strtoull(id->silicon, NULL, 16);
  if (!identity)
    return -EINVAL;
  boot = id->boot;
  group = e->group;
  roster.map_bound_us = e->map_bound_us;
  roster.count = 2;
  memset(roster.identity, 0, sizeof roster.identity);
  memset(roster.address, 0, sizeof roster.address);
  roster.identity[0] = identity;
  uint8_t derived[16];
  r2_group_pair_key(derived, &group, identity, identity);
  memcpy(own_address, derived, 6);
  own_address[5] |= 0xc0;
  memcpy(roster.address[0], own_address, 6);
  bt_addr_le_t address = {.type = BT_ADDR_LE_RANDOM};
  memcpy(address.a.val, own_address, 6);
  int rc = bt_id_create(&address, NULL);
  if (rc < 0)
    return rc;
  snprintf(name, sizeof name, "Clock-%016llx", (unsigned long long)identity);
  struct r2_enrollment path = {.group = group,
                               .count = 2,
                               .map_bound_us = roster.map_bound_us,
                               .local_capabilities = 3,
                               .gm_mask = 1,
                               .gnss_mask = 1};
  memcpy(path.identity, roster.identity, sizeof roster.identity);
  memcpy(path.address, roster.address, sizeof roster.address);
  rc = r2_radio_service_init(0, &path);
  if (rc)
    return rc;
  rc = r2_relay_service_init(0, &path, boot, NULL);
  if (rc)
    return rc;
  r2_radio_service_continuous(true);
  k_work_queue_start(&owner_queue, owner_stack,
                     K_THREAD_STACK_SIZEOF(owner_stack), 9, NULL);
  k_thread_name_set(&owner_queue.thread, "gm7_owner");
  rc = r2_ble_owner_bind(k_work_queue_thread_get(&owner_queue));
  if (rc)
    return rc;
  k_thread_create(&supervisor_thread, supervisor_stack,
                  K_THREAD_STACK_SIZEOF(supervisor_stack), supervisor_owner,
                  NULL, NULL, NULL, 5, 0, K_NO_WAIT);
  k_thread_name_set(&supervisor_thread, "gm7_supervisor");
  configured = status.configured = true;
  status.closed = true;
  status.self = 0;
  status.root = status.standby = status.coordinator = 255;
  return 0;
}
void r2_service_clock(const struct r2_clock *clock) {
  struct r2_source_clock raw;
  r2_time_source_snapshot(&raw);
  struct r2_capture current = {0};
  uint64_t relative, generation;
  bool captured = r2_capture_now(&current);
  bool raw_live =
      captured && r2_source_clock_read(&raw, current, &relative, &generation);
  (void)k_mutex_lock(&lock, K_FOREVER);
  input.view = clock->view;
  input.translated_utc_us = clock->translated_utc_us;
  input.translated_at_us = clock->translated_at_us;
  input.translated_epoch = clock->translated_epoch;
  input.last_pps = clock->last_pps;
  input.last_rmc = clock->last_rmc;
  input.last_rmc_utc = clock->last_rmc_utc;
  input.last_pair_edge = clock->last_pair_edge;
  input.last_pair_utc = clock->last_pair_utc;
  input.stopped = clock->stopped;
  input.fatal = clock->fatal;
  bool healthy = raw_live && !clock->stopped && !clock->fatal &&
                 (clock->view.source_available ||
                  (clock->view.valid && clock->view.fix_active &&
                   clock->view.pps_present));
  if (promoted_valid) {
    /* Serialize main refresh and promotion. Only a NEW physical PPS may
     * discipline the retained delivered affine coordinate. A duplicate raw
     * fit cannot renew its original horizon or overwrite its UTC/phase. */
    bool same =
        healthy && functional_source.epoch == current.epoch &&
        raw.epoch == current.epoch && raw.utc_bound &&
        raw.utc_source_epoch == clock->view.source_epoch &&
        functional_source.utc_source_epoch == clock->view.source_epoch &&
        raw.utc_association == functional_source.utc_association;
    if (!same || raw.last_edge < promoted_pps) {
      promoted_valid = false;
      healthy = false;
      r2_source_clock_invalidate(&functional_source);
    } else if (raw.last_edge != promoted_pps) {
      const struct r2_source_map *latest =
          &raw.maps[(raw.head + R2_SOURCE_MAP_HISTORY - 1) %
                    R2_SOURCE_MAP_HISTORY];
      struct r2_pps_precision pps = {.epoch = raw.epoch,
                                     .last = raw.last_edge,
                                     .rate_q16 = latest->rate_q16,
                                     .seen = true,
                                     .rate_valid = true};
      if (!r2_source_clock_publish(&functional_source, &pps, current)) {
        promoted_valid = false;
        healthy = false;
      } else {
        promoted_pps = raw.last_edge;
      }
    }
  } else {
    functional_source = raw;
  }
  r2_radio_service_source(&functional_source, healthy,
                          clock->view.source_epoch);
  if (!healthy) {
    status.serving = false;
    r2_gm7_controller_source(0, false);
    r2_gm7_controller_cancel(0);
  }
  (void)k_mutex_unlock(&lock);
}
int r2_gm7_start_ready(void) {
  (void)k_mutex_lock(&lock, K_FOREVER);
  int rc = status.error          ? status.error
           : !configured || used ? -EACCES
           : input.fatal         ? -EIO
           : input.stopped       ? -ECANCELED
                                 : 0;
  (void)k_mutex_unlock(&lock);
  if (!rc)
    rc = r2_capture_error();
  if (!rc)
    rc = r2_gnss_input_error();
  if (rc)
    return rc;
  struct gm7_bridge b;
  return bridge(&b) ? 0 : -EAGAIN;
}
void r2_gm7_start_failed(int error) {
  (void)k_mutex_lock(&lock, K_FOREVER);
  if (!started && error < 0) {
    status.error = error;
    status.active = status.serving = status.follower = false;
    status.normal = status.closed = true;
  }
  (void)k_mutex_unlock(&lock);
}
int r2_service_preflight(uint32_t seconds, uint64_t arrival) {
  uint64_t now = kernel_now();
  return !configured || used || !seconds || seconds > 120 ||
                 arrival > now / 1000 || now / 1000 - arrival >= R2_REPLY_MS
             ? -EACCES
             : 0;
}
static int start(uint64_t arrival, uint32_t seconds) {
  int rc = r2_service_preflight(seconds ? seconds : 1, arrival);
  if (rc)
    return rc;
  struct gm7_bridge b;
  if (!bridge(&b))
    return -ESTALE;
  rc = gm7_owner_init(&owner, &group, identity, boot, true, b.hf);
  if (rc)
    return rc;
  clock_epoch = b.epoch;
  phase_known = false;
  gm7_owner_clock(&owner, hf_read, NULL);
  gm7_owner_reservation_guard(&owner, reservation_fence, NULL);
  gm7_scheduler_init(&scheduler, b.hf);
  rc = r2_ble_group_start(identity, group.id, group.epoch, own_address);
  if (rc)
    return rc;
  r2_ble_gm7_server(true);
  r2_gm7_controller_scan(true, false, 0);
  r2_radio_service_activate();
  used = started = true;
  status.active = true;
  status.closed = status.closing = false;
  status.normal = !seconds;
  status.deadline = seconds ? arrival * 1000 + (uint64_t)seconds * 1000000 : 0;
  k_timer_start(&service_timer, K_MSEC(2), K_MSEC(2));
  return 0;
}
int r2_service_start(uint32_t seconds, uint64_t at) {
  if (!seconds) {
    r2_service_stop();
    return 0;
  }
  return start(at, seconds);
}
int r2_service_run(uint64_t at) { return start(at, 0); }
void r2_service_stop(void) {
  if (!started)
    return;
  r2_radio_service_withdraw();
  r2_gm7_controller_cancel(0);
  (void)k_mutex_lock(&lock, K_FOREVER);
  resuming = status.profile_pending = false;
  if (!atomic_get(&stopping)) {
    close_until = kernel_now() + R2_CLEANUP_US;
    status.close_deadline = close_until;
    status.closing = true;
    status.serving = status.follower = false;
    atomic_set(&stopping, 1);
  }
  (void)k_mutex_unlock(&lock);
}
void r2_service_supervise(void) {
  if (!started)
    return;
  (void)k_mutex_lock(&lock, K_FOREVER);
  uint64_t now = kernel_now();
  bool closed = status.closed;
  if (atomic_get(&stopping) && !closed && now >= close_until && !status.error)
    status.error = -ETIMEDOUT;
  bool expired = status.deadline && now >= status.deadline;
  if (atomic_get(&busy) && now >= status.supervision.submitted &&
      now - status.supervision.submitted >= 100000) {
    if (!status.supervision.fault_site) {
      status.supervision.fault_site = 1;
      status.supervision.fault_at = now;
      status.supervision.fault_state = status.supervision.state;
    }
    if (!status.error)
      status.error = -ETIMEDOUT;
    expired = true;
  }
  bool submit = (!closed || resuming) && atomic_cas(&busy, 0, 1);
  if (submit) {
    status.supervision.submitted = now;
    status.supervision.state = 1;
  }
  (void)k_mutex_unlock(&lock);
  if (expired)
    r2_service_stop();
  if (submit) {
    int rc = k_work_submit_to_queue(&owner_queue, &service_work);
    if (rc < 0) {
      atomic_clear(&busy);
      status.error = rc;
      r2_service_stop();
    }
  }
}
int r2_service_close_result(void) {
  struct r2_service_status s;
  r2_service_snapshot(&s);
  return s.error ? s.error : s.closed || !started ? 0 : -EAGAIN;
}
void r2_service_snapshot(struct r2_service_status *out) {
  (void)k_mutex_lock(&lock, K_FOREVER);
  memset(out, 0, sizeof *out);
#define P(field) out->field = status.field
  P(supervision);
  P(transport_fault);
  P(precision);
  P(reception);
  P(quality);
  P(at);
  P(profile_incarnation);
  P(group_id);
  P(root_boot);
  P(follower_utc);
  P(time_sampled_us);
  P(source_frame_ns);
  P(service_until);
  P(authority_until);
  P(display_coarse_utc);
  P(display_coarse_at);
  P(close_deadline);
  P(closed_at);
  P(deadline);
  P(key_epoch);
  P(admission_ms);
  P(hot_refusal);
  P(source_epoch);
  P(local_source_epoch);
  P(rx);
  P(authenticated);
  P(rejected);
  P(drops);
  P(errors);
  P(backlog);
  P(cycles);
  P(worker_max_us);
  P(profile_changes);
  P(root);
  P(self);
  P(coordinator);
  P(standby);
  P(error);
  P(serving);
  P(hot_ready);
  P(gm_cold);
  P(gm_capable);
  P(source_eligible);
  P(follower);
  P(follower_clock_valid);
  P(display_coarse_valid);
  P(closed);
  P(active);
  P(closing);
  P(configured);
  P(normal);
  P(profile_pending);
#undef P
  memcpy(out->identities, status.identities, sizeof status.identities);
  memcpy(out->name, status.name, sizeof status.name);
  memcpy(out->lamps, status.lamps, sizeof status.lamps);
  (void)k_mutex_unlock(&lock);
  if (out->closing || atomic_get(&stopping)) {
    out->serving = out->follower = out->follower_clock_valid = out->hot_ready =
        false;
    out->precision.authorized = out->precision.map_valid =
        out->precision.locked = false;
  }
}
int r2_service_name(const char *next) {
  size_t n = next ? strlen(next) : 0;
  if (!r2_group_name(next, n))
    return -EINVAL;
  (void)k_mutex_lock(&lock, K_FOREVER);
  memset(name, 0, 25);
  memcpy(name, next, n);
  (void)k_mutex_unlock(&lock);
  return 0;
}
uint32_t r2_service_stack_unused(void) {
#ifdef CONFIG_INIT_STACKS
  size_t unused;
  if (configured && !k_thread_stack_space_get(&owner_queue.thread, &unused) &&
      unused <= UINT32_MAX)
    return (uint32_t)unused;
#endif
  return UINT32_MAX;
}
int r2_service_relay(uint64_t session, uint32_t seconds, uint64_t at) {
  (void)session;
  (void)seconds;
  (void)at;
  return -ENOTSUP;
}
int r2_service_relay_cancel(uint64_t session) {
  (void)session;
  return -ENOTSUP;
}
int r2_service_profile(unsigned mode, uint64_t arrival) {
  (void)mode;
  (void)arrival;
  return -ENOTSUP;
}
void r2_service_profile_status(uint32_t out[5]) {
  out[0] = out[1] = out[2] = out[3] = out[4] = 0;
}

int r2_normal_service_resume_gm(uint64_t incarnation, uint32_t changes) {
  if (!configured || !incarnation || resuming || !r2_ble_closed() ||
      !r2_radio_service_closed() || !r2_relay_service_closed() ||
      !r2_gm7_controller_idle())
    return -EBUSY;
  if (reset_generation == UINT32_MAX)
    return -EOVERFLOW;
  struct gm7_bridge b;
  if (!bridge(&b))
    return -ESTALE;
  (void)k_mutex_lock(&lock, K_FOREVER);
  reset_generation++;
  resume_boot = incarnation;
  /* Only this explicit, fully drained lifecycle may bind a new capture domain.
   * The ordinary worker comparison remains strict, including before reset. */
  clock_epoch = b.epoch;
  atomic_clear(&stopping);
  resuming = true;
  /* A real accepted, fully drained handoff is CONTROL progress. The dormant
   * GM publication may belong to boot or an earlier incarnation. Publish this
   * sample once; reset EAGAIN, queries and supervision cannot renew it. Retire
   * completed close state and old authority before making that sample fresh. */
  status.at = b.kernel_hi;
  status.closing = false;
  status.close_deadline = 0;
  status.serving = status.follower = status.follower_clock_valid =
      status.hot_ready = false;
  status.precision.map_valid = status.precision.authorized =
      status.precision.locked = status.precision.rms_valid = false;
  status.service_until = status.authority_until = 0;
  status.closed = false;
  status.profile_pending = true;
  status.profile_changes = changes + 1;
  status.profile_incarnation = incarnation;
  started = true;
  (void)k_mutex_unlock(&lock);
  k_timer_start(&service_timer, K_MSEC(2), K_MSEC(2));
  return 0;
}

void r2_service_ui(struct r2_ui_model *m) {
  struct r2_service_status s;
  r2_service_snapshot(&s);
  uint64_t now = kernel_now();
  (void)k_mutex_lock(&lock, K_FOREVER);
  m->gm7_owner = true;
  m->gm7_known = diagnostic.known;
  m->gm7_at_ms = diagnostic.at / 1000;
  m->gm7_hot_ms = diagnostic.hot_ms;
  m->gm7_active_ms = diagnostic.active_ms;
  m->gm7_pps_ms = diagnostic.pps_ms;
  unsigned selected = (unsigned)((now / 4000000) % GM7_PEERS);
  for (unsigned scan = 0; scan < GM7_PEERS; scan++) {
    unsigned i = (selected + scan) % GM7_PEERS;
    if (peer_display[i].valid && now < peer_display[i].report.until) {
      m->gm7_peer_id = peer_display[i].identity;
      m->gm7_peer_role = peer_display[i].report.role;
      m->gm7_peer_known = peer_display[i].report.known;
      m->gm7_peer_hot_ms = peer_display[i].report.hot_ms;
      m->gm7_peer_active_ms = peer_display[i].report.active_ms;
      m->gm7_peer_pps_ms = peer_display[i].report.pps_ms;
      m->gm7_peer_valid = true;
      m->gm7_peer_current = peer_display[i].current;
      break;
    }
  }
  (void)k_mutex_unlock(&lock);

  m->ble_active = s.active && !s.closing;
  m->root = s.root;
  m->rx = s.rx;
  m->authenticated = s.authenticated;
  m->precision = s.precision;
  m->quality = s.quality;
  m->coordinator = s.coordinator;
  m->standby = s.standby;
  m->profile_supported = true;
  m->profile_rc = s.profile_rc;
  m->profile_switching = s.profile_pending;
  m->relay_selected = s.relay.selected;
  m->relay_upstream = s.relay.parent;
  m->time_relay_hops = s.relay.selected ? s.relay.time_relay_hops : 0;
  m->boundary_clock = s.relay.selected;
  m->rc_source_held = s.relay.held_time;
  m->rc_update_age_ms = s.relay.correction_age_ms;
  m->rc_source_age_ms = s.relay.source_age_ms;
  /* Presentation-only settling state. Progress refreshes it; stale permission
   * never grants time or analog eligibility. Keep identity for at most5s after
   * the last accepted correction, and clear immediately on stop/root change. */
  static uint64_t ui_root_boot, ui_root_id, ui_progress_at;
  static uint32_t ui_source_epoch, ui_accepted;
  uint64_t candidate = s.root < 4 ? s.identities[s.root] : 0;
  bool candidate_live = s.active && !s.error && !s.closing &&
                        !atomic_get(&stopping) && !s.serving && s.root_boot &&
                        s.source_epoch && candidate && s.root != s.self &&
                        (s.self >= 4 || candidate != s.identities[s.self]);
  if (!candidate_live || ui_root_boot != s.root_boot ||
      ui_root_id != candidate || ui_source_epoch != s.source_epoch) {
    ui_root_boot = s.root_boot;
    ui_root_id = candidate;
    ui_source_epoch = s.source_epoch;
    ui_accepted = s.precision.accepted;
    ui_progress_at = 0;
  }
  if (candidate_live && s.precision.accepted != ui_accepted) {
    ui_accepted = s.precision.accepted;
    ui_progress_at = now;
  }
  m->sync_attempt =
      candidate_live &&
      (now < s.authority_until || (ui_progress_at && now >= ui_progress_at &&
                                   now - ui_progress_at < 5000000));
  unsigned labels[3] = {s.root, s.root, s.coordinator};
  char *names[3] = {m->root_name, m->upstream_name, m->coordinator_name};
  for (unsigned j = 0; j < 3; j++) {
    unsigned p = labels[j];
    uint64_t id = p < 4 ? s.identities[p] : 0;
    bool known = p == s.self ||
                 ((s.follower_clock_valid || m->sync_attempt) && p == s.root) ||
                 (p < 4 && (s.authenticated_mask & (1u << p)));
    r2_ui_identity_name(names[j], known ? id : 0,
                        known ? (p == s.self ? s.name
                                 : p < 4     ? s.peer_names[p]
                                             : NULL)
                              : NULL);
  }
  if (s.profile_rc) {
    m->clock.valid = false;
  }
  if (s.relay.selected)
    r2_ui_identity_name(m->upstream_name, s.relay.parent_identity, NULL);
  m->source_eligible = s.source_eligible;
  m->gm_capable = s.gm_capable;
  m->gm_cold = s.gm_cold;
  m->admission_ms = s.admission_ms;
  m->hot_ready = s.hot_ready;
  m->hot_refusal = s.hot_refusal;
#if defined(CONFIG_BT_PER_ADV)
  r2_time_precision_snapshot(&m->pps_precision);
#endif
  m->precision_beat_hz = s.active && !s.closing && !s.error &&
                                 (s.serving || s.follower_clock_valid)
                             ? R2_PRECISION_HZ
                             : 0;
  r2_ui_reception(m, &s.reception, now);
  memcpy(m->lamps, s.lamps, 8);
  /* UI may project the frozen delivered map only through its independently
   * computed deadline. Worker cadence is not source evidence, and does not
   * shorten an otherwise valid map to an arbitrary50ms presentation lease. */
  bool fresh = !s.error && !s.closing && !atomic_get(&stopping);
  /* Authenticated source role and valid delivered time are independent.
   * Loss of a precision map need not erase an unchanged live root identity. */
  uint64_t self_id = s.self < 4 ? s.identities[s.self] : 0;
  uint64_t root_id = s.root < 4 ? s.identities[s.root] : 0;
  m->scope_active =
      fresh && (s.serving || (root_id && self_id && root_id != self_id &&
                              now < s.authority_until));
  m->role =
      m->scope_active ? (s.serving ? R2_UI_GM : R2_UI_FOLLOWER) : R2_UI_NO_ROLE;
  m->authenticated_gm = m->scope_active && !s.serving;
  m->local_hot_standby = fresh && s.hot_ready;
  m->gm_source_fresh = m->scope_active && s.serving &&
                       s.local_source_epoch == m->clock.source_epoch &&
                       m->clock.fix_active && m->clock.pps_present;
  /* A nonbootstrap authenticated GR3 origin comes from the root physical
   * PPS-derived source map. Captured
   * corrections plus the locked receiver map establish synchronization to
   * that GM counter, independently of local GNSS. This is no absolute UTC or
   * output accuracy qualification, and public coarse JOIN alone cannot pass. */
  m->follower_pps_valid = m->authenticated_gm && s.follower_clock_valid &&
                          s.source_frame_ns && s.root_boot && s.source_epoch &&
                          s.precision.authorized && s.precision.locked &&
                          s.precision.fit_valid && s.precision.map_valid &&
                          now < s.service_until;
  m->sync_age_ms = UINT32_MAX;
  r2_ui_select_ble_time(m, s.follower_utc, s.time_sampled_us, now,
                        s.service_until, fresh && s.follower_clock_valid);
  /* One UI producer owns this boot-local display anchor. Permission expiry,
   * root changes and reacquisition withdraw authority above, not elapsed time.
   * Only validated local, delivered BLE or authenticated JOIN time seeds it. */
  static uint64_t display_utc, display_at;
  static bool display_valid;
  static uint32_t display_profile;
  if (s.profile_pending || display_profile != s.profile_changes) {
    display_valid = false;
    display_profile = s.profile_changes;
  }
  if (display_valid && now < display_at)
    display_valid = false;
  if (m->clock.valid && m->clock.utc_us >= 0) {
    display_utc = (uint64_t)m->clock.utc_us;
    display_at = now;
    display_valid = true;
  } else if (!display_valid && fresh && s.display_coarse_valid &&
             now >= s.display_coarse_at &&
             now - s.display_coarse_at <= INT64_MAX &&
             s.display_coarse_utc <=
                 (uint64_t)INT64_MAX - (now - s.display_coarse_at)) {
    display_utc = s.display_coarse_utc;
    display_at = s.display_coarse_at;
    display_valid = true;
  }
  m->coarse_valid = display_valid;
  m->coarse_utc_us = display_utc;
  m->coarse_at_us = display_at;
  /* Operator UI policy: <=200ns delivered RMS, with <=500ms visual loss
   * tolerance. This never modifies servo lock, map expiry or output authority.
   */
  static uint64_t face_root, face_boot, face_at, face_until;
  static uint32_t face_epoch, face_rms;
  bool changed = (candidate && face_root && candidate != face_root) ||
                 (s.root_boot && face_boot && s.root_boot != face_boot) ||
                 (s.source_epoch && face_epoch && s.source_epoch != face_epoch);
  bool hard_loss = !fresh || !s.active || s.serving || now < face_at || changed;
  if (hard_loss || (s.precision.rms_valid && s.precision.rms_ns > 200))
    face_until = 0;
  bool measured = fresh && candidate_live && m->authenticated_gm &&
                  s.follower_clock_valid && s.source_frame_ns &&
                  s.precision.authorized && s.precision.fit_valid &&
                  s.precision.map_valid && s.precision.rms_valid &&
                  s.precision.rms_ns <= 200 && now < s.service_until;
  if (measured && now <= UINT64_MAX - 500000) {
    face_root = candidate;
    face_boot = s.root_boot;
    face_epoch = s.source_epoch;
    face_at = now;
    face_until = now + 500000;
    face_rms = s.precision.rms_ns;
  }
  m->face_lock_until_us = face_until;
  m->face_rms_ns = face_rms;
}

int r2_gm7_diagnostic_format(char *out, size_t capacity, unsigned page,
                             const char *source, uint64_t physical_boot,
                             bool runtime_gm) {
  if (page > GM7_PEERS)
    return -EINVAL;
  (void)k_mutex_lock(&lock, K_FOREVER);
  bool startup_pending = runtime_gm && configured && !started && !status.error;
  int startup_error = runtime_gm && !started ? status.error : 0;
  runtime_gm = runtime_gm && started;
  if (page >= 1 && page <= GM7_PEERS) {
    unsigned i = page - 1;
    bool valid = runtime_gm && peer_display[i].valid &&
                 kernel_now() < peer_display[i].report.until;
    const struct gm7_reported_role *r = &peer_display[i].report;
    int n = snprintf(
        out, capacity,
        "{\"schema\":7,\"protocol\":\"GM7v2+A1+C1\",\"source\":\"%s\",\"boot\":"
        "\"%016llx\",\"page\":%u,\"available\":%"
        "s,\"peer\":\"%016llx\",\"peer_boot\":\"%016llx\",\"sender_sampled_"
        "mono_us\":%llu,\"expires_kernel_us\":%llu,\"mesh_current\":%s,"
        "\"role\":%u,\"known\":%u,\"hot_ms\":%u,\"active_ms\":%u,\"pps_ms\":%u,"
        "\"timer_basis\":\"SENDER_REPORTED_AT_SAMPLE\",\"authority\":false}",
        source, (unsigned long long)physical_boot, page,
        valid ? "true" : "false",
        (unsigned long long)(valid ? peer_display[i].identity : 0),
        (unsigned long long)(valid ? peer_display[i].boot : 0),
        (unsigned long long)(valid ? r->sampled_mono_us : 0),
        (unsigned long long)(valid ? r->until : 0),
        valid && peer_display[i].current ? "true" : "false",
        valid ? r->role : 0, valid ? r->known : 0,
        (unsigned)(valid ? r->hot_ms : UINT32_MAX),
        (unsigned)(valid ? r->active_ms : UINT32_MAX),
        (unsigned)(valid ? r->pps_ms : UINT32_MAX));
    (void)k_mutex_unlock(&lock);
    return n >= 0 && (size_t)n < capacity ? n : -ENOSPC;
  }
  int n = snprintf(
      out, capacity,
      "{\"schema\":7,\"protocol\":\"GM7v2+A1+C1\",\"source\":\"%s\",\"boot\":"
      "\"%016llx\",\"page\":0,\"available\":%s,"
      "\"startup_pending\":%s,\"startup_error\":%d,\"birth_utc_us\":%llu,"
      "\"founder\":\"%"
      "016llx\",\"term\":%llu,\"active\":\"%016llx\",\"active_boot\":\"%"
      "016llx\",\"role\":%u,\"known\":%u,\"hot_ms\":%u,\"active_ms\":%u,\"pps_"
      "ms\":%u,\"blockers_internal\":%u,\"reason\":%u,\"reservation_pending\":%"
      "s,\"reservation_stage\":%u,\"reservation_rc\":%d,"
      "\"reservation_failures\":%u,\"management_rx\":%u,"
      "\"management_mac_valid\":%u,\"management_"
      "accepted\":%u,\"event_queued\":%u,\"event_completed\":%u,\"event_"
      "failed\":%u,"
      "\"event_unsent\":%u,\"absolute_utc_error_us\":null,\"rf_timing_"
      "qualified\":false}",
      source, (unsigned long long)physical_boot,
      configured && runtime_gm ? "true" : "false",
      startup_pending ? "true" : "false", startup_error,
      (unsigned long long)(runtime_gm ? diagnostic.birth : 0),
      (unsigned long long)(runtime_gm ? diagnostic.founder : 0),
      (unsigned long long)(runtime_gm ? diagnostic.term : 0),
      (unsigned long long)(runtime_gm ? diagnostic.active : 0),
      (unsigned long long)(runtime_gm ? diagnostic.active_boot : 0),
      runtime_gm ? diagnostic.role : GM7_W_RC,
      runtime_gm ? diagnostic.known : 0,
      (unsigned)(runtime_gm ? diagnostic.hot_ms : UINT32_MAX),
      (unsigned)(runtime_gm ? diagnostic.active_ms : UINT32_MAX),
      (unsigned)(runtime_gm ? diagnostic.pps_ms : UINT32_MAX),
      diagnostic.blockers, diagnostic.reason,
      runtime_gm && diagnostic.pending ? "true" : "false",
      runtime_gm ? diagnostic.reservation_stage : 0,
      runtime_gm ? (int)diagnostic.reservation_rc : 0,
      runtime_gm ? diagnostic.reservation_failures : 0,
      runtime_gm ? (uint32_t)atomic_get(&management_rx) : 0,
      runtime_gm ? (uint32_t)atomic_get(&management_mac) : 0,
      runtime_gm ? (uint32_t)atomic_get(&management_accepted) : 0,
      runtime_gm ? diagnostic.event_queued : 0,
      runtime_gm ? diagnostic.event_completed : 0,
      runtime_gm ? diagnostic.event_failed : 0,
      runtime_gm ? diagnostic.event_unsent : 0);
  (void)k_mutex_unlock(&lock);
  return n >= 0 && (size_t)n < capacity ? n : -ENOSPC;
}
