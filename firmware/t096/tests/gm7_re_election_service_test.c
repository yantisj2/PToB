/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual source/owner/cycle/reservation; fake public HAL/kernel/RF only. */
#define main historical_service_cases
#include "gm7_service_test.c"
#undef main

static void re_election_setup(bool retained) {
  setup(false);
  memset(&fake_timing, 0, sizeof fake_timing);
  primary_frame = diag_frame = status_cycle = UINT64_MAX;
  phase_known = false;
  last_phase = last_enclosure = 0;
  if (retained) {
    owner.scope = (struct gm7_scope){.birth = UINT64_C(1792800000000000),
                                     .founder = 99, .term = 4,
                                     .active = 99, .boot = 77,
                                     .timing_epoch = 8, .source_gen = 8,
                                     .capture_gen = 4, .train = 8};
    owner.selected = (struct gm7_control){.birth_us = owner.scope.birth,
                                           .founder = 99, .term = 4,
                                           .active = 99, .active_boot = 77};
    owner.selected_original = 50000000;
    owner.selected_until = owner.selected_source_until = 0;
  }
  tick_to(66020000);
  healthy_input();
}
static void current_peer(bool incumbent) {
  struct gm7_peer *p = &owner.peer[0];
  p->used = true;
  p->identity = incumbent ? 99 : 1;
  p->boot = 77;
  p->kind = incumbent ? GM7_ANNOUNCE : GM7_STATUS;
  p->flags = GM7_ELIGIBLE | GM7_CAPABLE;
  p->original = fake_hf - 10;
  p->received = (struct gm7_peer_control){.last_sequence=1,
      .source_until=fake_hf+1000000,.control_until=fake_hf+2000000,
      .control=owner.selected};
  struct gm7_control *c=&p->received.control;
  c->timing_epoch=8;
  c->sender = p->identity;
  c->boot = p->boot;
  c->source_gen = 8;
  c->capture_gen = 4;
  c->train_gen = 8;
  c->sent_mono_us = fake_hf - 10;
  c->sent_utc_us = UINT64_C(1792800000000000) + fake_hf - 10;
  c->priority1 = c->priority2 = 128;
  c->clock_class = 2;
  c->accuracy = 255;
  c->variance = 65535;
}
int main(void) {
  re_election_setup(false);
  cycle(&service_work);
  assert(owner.reserved && fake_native_live[GM7_PRIMARY]);
  finish(GM7_PRIMARY, true, true);
  assert(owner.serving && owner.scope.active == identity);
  puts("PASS initial start without retained root");

  re_election_setup(true);
  cycle(&service_work);
  assert(owner.eligible && owner.initial_due && owner.reserved &&
         fake_native_live[GM7_PRIMARY] && !owner.serving &&
         diagnostic.reservation_stage == RESERVE_NATIVE);
  assert(phase_known && last_enclosure == 100);
  finish(GM7_PRIMARY, true, true);
  assert(owner.serving && !owner.reserved && owner.scope.active == identity &&
         owner.scope.birth == UINT64_C(1792800000000000) &&
         owner.scope.founder == 99 && owner.scope.term == 5);
  puts("PASS expired remote root/no delivered map: actual first ANN completes "
       "local re-election and inherits original lineage/next term");

  re_election_setup(true);
  current_peer(true);
  cycle(&service_work);
  assert(owner.eligible && !owner.initial_due && !owner.reserved &&
         !owner.serving && !fake_native_live[GM7_PRIMARY]);
  puts("PASS live incumbent prevents local initial election");

  re_election_setup(true);
  current_peer(false);
  cycle(&service_work);
  assert(owner.eligible && !owner.initial_due && !owner.reserved &&
         !fake_native_live[GM7_PRIMARY]);
  puts("PASS better current eligible candidate retains election rank");

  re_election_setup(true);
  owner.quiet = false;
  owner.selected_original = fake_hf - 1000000;
  cycle(&service_work);
  assert(!owner.initial_due && !owner.reserved && !fake_native_live[GM7_PRIMARY]);
  puts("PASS original quiet/backoff must elapse");

  re_election_setup(true);
  input.view.fix_active = false;
  cycle(&service_work);
  assert(!owner.eligible && !owner.initial_due && !owner.reserved &&
         !fake_native_live[GM7_PRIMARY]);
  puts("PASS missing independent local GNSS never starts");

  re_election_setup(true);
  r2_source_clock_invalidate(&functional_source);
  input.translated_epoch = 3;
  input.translated_utc_us = INT64_C(1792800000000000) + fake_hf;
  input.translated_at_us = (int64_t)fake_kernel;
  cycle(&service_work);
  assert(owner.eligible && !owner.source.physical_ready && !owner.initial_due &&
         !owner.reserved && !fake_native_live[GM7_PRIMARY]);
  puts("PASS coarse UTC without physical source map cannot elect");

  re_election_setup(true);
  fake_poll_advance = 1600000;
  cycle(&service_work);
  assert(fake_hf > owner.source.until_us && !owner.reserved &&
         !owner.serving && !fake_native_live[GM7_PRIMARY]);
  puts("PASS blocking elapsed work cannot extend original source horizon");

  /* Existing actual production RF/HOT and negative-copy cases retain their
   * original scopes, native admission/end windows and fixed deadlines. */
  prospective_blackout();
  negative_phase_cases();
  puts("PASS existing HOT blackout and bounded negative-phase scheduling");
  return 0;
}
