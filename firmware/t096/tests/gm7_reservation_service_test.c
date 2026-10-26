/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual service/owner/codec/scheduler; physical completion and elapsed CPU
 * time are explicit fake boundaries, not measured controller performance. */
#define main existing_service_cases
#include "gm7_service_test.c"
#undef main
#include <limits.h>
static void preparing(uint64_t phase) {
  setup(false);
  primary_frame = diag_frame = status_cycle = UINT64_MAX;
  tick_to(fake_hf + phase);
  healthy_input();
}
static void refused_then_serving(void) {
  preparing(20000);
  fake_submit_refusal = true;
  for (unsigned i = 0; i < 3; ++i) {
    cycle(&service_work);
    assert(!owner.reserved && !owner.serving && !owner.scope.birth);
    assert(scheduler.job[GM7_PRIMARY].state == GM7_JOB_EMPTY);
    assert(diagnostic.reservation_stage == RESERVE_NATIVE &&
           diagnostic.reservation_rc == -EBUSY &&
           diagnostic.reservation_failures == i + 1);
    uint64_t consumed = primary_frame;
    cycle(&service_work);
    assert(primary_frame == consumed && diagnostic.reservation_failures == i+1); /* Primary opportunity never retries; ordinary STATUS may advance. */
    tick_to(fake_hf + GM7_FRAME_US);
    healthy_input();
  }
  fake_submit_refusal = false;
  cycle(&service_work);
  assert(owner.reserved && fake_native_live[GM7_PRIMARY] && !owner.serving);
  finish(GM7_PRIMARY, true, true);
  assert(owner.serving && owner.scope.birth && !owner.reserved &&
         diagnostic.reservation_stage == RESERVE_COMPLETE &&
         !diagnostic.reservation_rc && diagnostic.reservation_failures == 3);
  puts("PASS actual cycle repeated preRF refusals consume originals, discard "
       "unexposed scope, then fresh opportunity completes first ANN/ACTIVE");
}
static void original_end_cases(void) {
  preparing(20000);
  cycle(&service_work);
  struct gm7_job old = scheduler.job[GM7_PRIMARY];
  /* Completion occurred inside the original window; its processing is late. */
  fake_native_live[GM7_PRIMARY] = false;
  fake_native_exposed[GM7_PRIMARY] = true;
  fake_result[fake_result_count++] = (struct r2_gm7_tx){
      .ticket = old.ticket,
      .generation = old.generation,
      .buffer_version = old.buffer_version,
      .epoch = 4,
      .primary_address = (uint32_t)((old.window.nominal + 42) * 16),
      .aux_address = (uint32_t)((old.window.nominal + 551) * 16),
      .aux_end = (uint32_t)((old.window.nominal + 1400) * 16),
      .slot = GM7_PRIMARY,
      .complete = true,
      .exposed = true};
  tick_to(old.window.latest_end + 10000);
  struct gm7_bridge b;
  assert(bridge(&b));
  results(b);
  assert(owner.serving && !owner.reserved && owner.active_since == fake_hf);

  for (unsigned orphan = 0; orphan < 2; ++orphan) {
    preparing(20000);
    cycle(&service_work);
    uint64_t birth = owner.reserved_scope.birth;
    uint64_t end = owner.reserved_end;
    if (orphan) {
      /* Unknown lost bookkeeping is conservative, not proof of no exposure. */
      memset(&scheduler.job[GM7_PRIMARY], 0, sizeof scheduler.job[GM7_PRIMARY]);
      fake_native_live[GM7_PRIMARY] = false;
    } else
      fake_native_exposed[GM7_PRIMARY] = true;
    tick_to(end + 1);
    assert(bridge(&b));
    results(b);
    assert(!owner.reserved && !owner.serving && owner.scope.birth == birth &&
           owner.event.valid && owner.event.negative &&
           diagnostic.reservation_stage == RESERVE_EXPIRED);
  }
  puts("PASS original-end retirement after measured results; delayed valid "
       "completion commits, missing/exposed and no-job orphan retain lineage");
}
static void stages_and_stale(void) {
  preparing(20000);
  struct gm7_bridge b;
  assert(bridge(&b));
  assert(!gm7_owner_reserve(&owner, fake_hf + 18000, fake_hf + 22000, fake_hf));
  struct gm7_packet p;
  assert(!gm7_owner_planned_control(&owner, &p, fake_hf));
  /* The real scheduler rejects a source horizon that does not cover END. */
  owner.source.until_us = owner.reserved_end;
  assert(queue(GM7_PRIMARY, &p, window(owner.reserved_nominal, 100), 0, b) ==
         -ESTALE);
  assert(!owner.reserved && diagnostic.reservation_stage == RESERVE_SCHEDULER);
  preparing(20000);
  owner.sequence = UINT64_MAX;
  cycle(&service_work);
  assert(!owner.reserved && !owner.serving && owner.failed &&
         diagnostic.reservation_stage == RESERVE_CONTROL);

  preparing(20000);
  cycle(&service_work);
  struct gm7_job old = scheduler.job[GM7_PRIMARY];
  uint64_t old_sequence = owner.reserved_sequence;
  gm7_owner_reservation_cancel(&owner, fake_hf);
  assert(bridge(&b));
  results(b);
  tick_to(fake_hf + GM7_FRAME_US);
  healthy_input();
  cycle(&service_work);
  assert(owner.reserved && owner.reserved_sequence > old_sequence);
  uint64_t current = owner.reserved_sequence;
  fake_result[fake_result_count++] =
      (struct r2_gm7_tx){.ticket = old.ticket,
                         .generation = old.generation,
                         .buffer_version = old.buffer_version,
                         .slot = GM7_PRIMARY,
                         .complete = false,
                         .exposed = true};
  assert(bridge(&b));
  results(b);
  assert(owner.reserved && owner.reserved_sequence == current);
  finish(GM7_PRIMARY, true, true);
  assert(owner.serving);
  reservation_note(RESERVE_NATIVE, INT_MIN, true);
  assert(diagnostic.reservation_rc == INT16_MIN);
  diagnostic.reservation_failures = UINT32_MAX;
  reservation_note(RESERVE_NATIVE, INT_MAX, true);
  assert(diagnostic.reservation_rc == INT16_MAX &&
         diagnostic.reservation_failures == UINT32_MAX);
  puts("PASS control/scheduler failure rollback, stale ticket isolation and "
       "bounded diagnostic error/count encoding");
}
static void preparation_cost(void) {
  /* Advance HF after the actual scheduler codec/HMAC, at the native boundary.
   * Its existing3ms rule is also exercised by the native controller suite. */
  preparing(14000);
  fake_submit_current_lead = true;
  fake_submit_advance = 6000;
  cycle(&service_work);
  assert(owner.reserved && fake_native_live[GM7_PRIMARY]);
  uint64_t sample = owner.source.sampled_us;
  struct gm7_packet packet;
  assert(!gm7_ad_decode(&packet, scheduler.job[GM7_PRIMARY].ad,
                        scheduler.job[GM7_PRIMARY].length, &group));
  assert(packet.body.control.sent_mono_us == sample &&
         fake_hf == sample + 6000);
  finish(GM7_PRIMARY, true, true);
  assert(owner.serving);

  preparing(34000);
  fake_submit_current_lead = true;
  fake_submit_advance = 2000;
  cycle(&service_work);
  assert(!owner.reserved && !owner.serving &&
         diagnostic.reservation_stage == RESERVE_NATIVE &&
         diagnostic.reservation_rc == -ETIMEDOUT);
  uint64_t old = diagnostic.reservation_failures;
  cycle(&service_work);
  assert(diagnostic.reservation_failures == old);
  tick_to(fake_hf + GM7_FRAME_US - 20000);
  healthy_input();
  cycle(&service_work);
  assert(owner.reserved);
  finish(GM7_PRIMARY, true, true);
  assert(owner.serving);
  preparing(1000);
  fake_submit_current_lead = true;
  unsigned polls = 0;
  while (!owner.reserved && polls++ < 45) {
    fake_submit_advance = 6000;
    cycle(&service_work);
    if (!owner.reserved)
      tick_to(fake_hf + 5700);
  }
  assert(owner.reserved && polls < 45 && fake_native_live[GM7_PRIMARY]);
  finish(GM7_PRIMARY, true, true);
  assert(owner.serving);
  puts("PASS bounded100ms CPU preparation with6ms injected elapsed encode/work; "
       "unchanged packet sample/RF windows, currentHF refusal and next "
       "recovery");
}
int main(void) {
  refused_then_serving();
  original_end_cases();
  stages_and_stale();
  preparation_cost();
  preparing(20000);
  cycle(&service_work);
  assert(owner.reserved && owner.reserved_nominal - fake_hf > 7000);
  tick_to(fake_hf + 5000);
  ++fake_clock_epoch;
  cycle(&service_work);
  assert(!owner.reserved && !owner.serving && !fake_native_live[GM7_PRIMARY]);
  return 0;
}
