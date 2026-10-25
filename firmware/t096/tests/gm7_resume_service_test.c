/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual dispatcher/resume/source/owner composition; only platform and the
 * unchanged RC backend are fake. No radio or hardware acceptance claim. */
#define main existing_service_cases
#include "gm7_service_test.c"
#undef main
#include "rc_dynamic_service.h"
#include "service_backends.h"
#include "watchdog.h"
static bool rc_closed, uc_idle = true;
static unsigned rc_starts;
int r2_dynamic_service_configure(const struct r2_identity *i,
                                 const struct r2_enrollment *e) {
  (void)i;
  (void)e;
  rc_closed = true;
  return 0;
}
int r2_dynamic_service_parent_target(uint64_t parent) {
  (void)parent;
  return 0;
}
int r2_dynamic_service_run(uint64_t at) {
  (void)at;
  ++rc_starts;
  rc_closed = false;
  return 0;
}
void r2_dynamic_service_stop(void) { rc_closed = true; }
int r2_dynamic_service_close_result(void) { return rc_closed ? 0 : -EAGAIN; }
void r2_dynamic_service_supervise(void) {}
bool r2_uc_quiescent(void) { return uc_idle; }
void r2_shadow_normal_withdraw(void) {}
void r2_normal_service_stop(void) { r2_service_stop(); }
void r2_normal_service_supervise(void) { r2_service_supervise(); }
int r2_normal_service_close_result(void) { return r2_service_close_result(); }
void r2_normal_service_snapshot(struct r2_service_status *s) {
  r2_service_snapshot(s);
}
int r2_normal_service_configure(const struct r2_identity *i,
                                const struct r2_enrollment *e) {
  return r2_service_configure(i, e);
}
int r2_normal_service_run(uint64_t at) { return r2_service_run(at); }
/* Exact source bodies extracted by the driver, with only colliding TU names
 * renamed. The ordinary RC dispatcher itself is not changed by this fix. */
#include "gm7_dispatch_under_test.h"

static void cold_rc(void) {
  setup(false);
  memset(&owner, 0, sizeof owner);
  memset(&status, 0, sizeof status);
  memset(&scheduler, 0, sizeof scheduler);
  configured = started = used = false;
  clock_epoch = 0;
  dispatch_configured = dispatch_pending = dispatch_resuming =
      dispatch_running = false;
  dispatch_changes = dispatch_failure = 0;
  atomic_clear(&dispatch_rc_active);
  fake_ble_closed = fake_pt_closed = fake_relay_closed = true;
  rc_starts = 0;
  uc_idle = true;
  struct r2_identity id = {.silicon = "0000000000000011", .boot = 33};
  struct r2_enrollment e = {.group = group,
                            .map_bound_us = 40000,
                            .rc_dynamic = true,
                            .rc7_boot_rc = true};
  assert(!dispatcher_configure(&id, &e));
  assert(!dispatcher_run(fake_kernel / 1000));
  assert(r2_switch_rc_active() && rc_starts == 1 && !started && !clock_epoch);
}
static void request_gm(void) {
  assert(!dispatcher_profile(R2_PROFILE_GM, fake_kernel / 1000));
  dispatcher_supervise();
}
static void resumed(void) {
  assert(resuming && status.profile_pending && !status.active &&
         !status.closed);
  assert(clock_epoch == fake_clock_epoch);
  cycle(&service_work);
  dispatcher_supervise();
  assert(!dispatch_pending && !dispatch_failure && !resuming);
  assert(status.active && !status.closed && !status.error && used);
  assert(!r2_switch_rc_active() &&
         owner.requester.boot == dispatch_incarnation);
  assert(!owner.serving && !owner.armed && !fake_scope.gm);
}
static void resume_cases(void) {
  cold_rc();
  request_gm();
  resumed();
  uint64_t first_boot = boot;
  assert(!dispatcher_profile(R2_PROFILE_RC, fake_kernel / 1000));
  cycle(&service_work);
  dispatcher_supervise();
  assert(r2_switch_rc_active() && !dispatch_pending && rc_starts == 2);
  request_gm();
  resumed();
  assert(boot != first_boot && dispatch_changes == 3);

  cold_rc();
  request_gm();
  uint64_t deadline = dispatch_until;
  fake_reset_result = -EAGAIN;
  cycle(&service_work);
  dispatcher_supervise();
  assert(resuming && dispatch_pending && dispatch_until == deadline && !used);
  tick_to(fake_hf + 2000);
  fake_reset_result = 0;
  resumed();

  for (unsigned mode = 0; mode < 6; ++mode) {
    cold_rc();
    request_gm();
    if (mode == 0)
      fake_reset_result = -EIO;
    if (mode == 1)
      fake_relay_reset_result = -EIO;
    if (mode == 2)
      fake_group_start_result = -EIO;
    if (mode == 3)
      fake_hf_valid = false;
    if (mode == 4)
      ++fake_clock_epoch;
    if (mode == 5) {
      fake_reset_result = -EAGAIN;
      tick_to(fake_hf + dispatch_until - fake_kernel);
    }
    cycle(&service_work);
    dispatcher_supervise();
    assert(dispatch_failure < 0 && !dispatch_pending && !resuming &&
           !status.active);
    assert(!owner.serving && !fake_scope.gm);
    if (mode < 3) {
      int error = status.error;
      cycle(&service_work);
      assert(status.closed && !status.active && status.error == error &&
             r2_ble_closed() && r2_radio_service_closed() &&
             r2_gm7_controller_idle());
    }
  }

  for (unsigned during_reset = 0; during_reset < 2; ++during_reset) {
    cold_rc();
    request_gm();
    if (during_reset)
      fake_stop_on_reset = true;
    else
      r2_service_stop();
    cycle(&service_work);
    assert(!resuming && atomic_get(&stopping) && !used && !status.active);
    cycle(&service_work);
    assert(status.closed && !status.profile_pending && !used && !fake_scope.gm);
  }
  cold_rc();
  fake_hf_valid = false;
  request_gm();
  assert(dispatch_failure == -ESTALE && !resuming && !started);
  puts("PASS actual cold RC configure/run->GM, warm GM->RC->GM, reset pending, "
       "hard errors, exact deadline, clock/epoch fences and stop during reset");
}
static void source_cases(void) {
  setup(false);
  assert(!gm7_owner_init(&owner, &group, identity, boot, true, fake_hf));
  uint64_t beginning = fake_hf, observing = 0, last = 0;
  for (unsigned i = 0; fake_hf - beginning < 61000000; ++i) {
    healthy_input();
    fake_capture_advance = i % 31 == 0 ? 20000 : i % 7 == 0 ? 731 : 0;
    cycle(&service_work);
    assert(!status.error && owner.source.physical_ready &&
           owner.source_observing);
    if (!i)
      observing = owner.source_since;
    assert(owner.source_since == observing && owner.last_utc >= last);
    assert(last_phase <= 1 || last_phase >= GM7_FRAME_US - 1);
    assert(event_evidence.phase_sample_ms == (uint32_t)(fake_hf / 1000));
    last = owner.last_utc;
    tick_to(fake_hf + 2000);
  }
  assert(owner.eligible && fake_hf - observing >= GM7_DWELL_US);

  healthy_input();
  struct gm7_bridge b;
  assert(bridge(&b));
  struct gm7_source s;
  struct r2_source_clock map;
  struct r2_clock c;
  uint64_t original_until = (uint64_t)input.last_pps + R2_PPS_FRESH_US;
  fake_capture_advance = 20000;
  assert(source_read(&s, &map, &c, b));
  assert(s.physical_ready && s.sampled_us == fake_hf && s.sampled_us > b.hf);
  uint64_t converted;
  assert(gm7_bridge_to_hf(b, original_until, &converted));
  assert(s.until_us == converted); /* No capture-delay horizon renewal. */
  uint64_t utc;
  assert(!source_phase(&s, b, &utc)); /* Future sample is still refused. */
  assert(bridge(&b) && source_phase(&s, b, &utc));

  healthy_input();
  r2_source_clock_invalidate(&functional_source);
  input.translated_epoch = input.view.source_epoch;
  input.translated_at_us = (int64_t)fake_kernel;
  input.translated_utc_us = INT64_C(1792800000000000) + (int64_t)fake_hf;
  assert(bridge(&b));
  fake_capture_advance = 20000;
  assert(source_read(&s, &map, &c, b));
  assert(!s.physical_ready && s.utc_available && s.sampled_us == b.hf);
  assert(bridge(&b) && source_phase(&s, b, &utc));
  assert(utc == UINT64_C(1792800000000000) + fake_hf);

  healthy_input();
  functional_source.utc_offset_ns -= 1000000000;
  cycle(&service_work);
  assert(!owner.source_observing && !owner.eligible); /* Real backstep. */
  healthy_input();
  tick_to(fake_hf + R2_PPS_FRESH_US + 1);
  cycle(&service_work);
  assert(!owner.eligible && !owner.source.gnss_healthy);
  ++fake_clock_epoch;
  cycle(&service_work);
  assert(status.error == -ESTALE && atomic_get(&stopping) && !owner.serving &&
         !fake_scope.gm);
  puts("PASS actual source_read->fresh phase->owner:61s continuous dwell with "
       "variable capture delay, original horizon, coarse fallback, future, "
       "real backstep, expiry and live epoch fences");
}
int main(int argc, char **argv) {
  if (argc < 2 || !strcmp(argv[1], "resume"))
    resume_cases();
  if (argc < 2 || !strcmp(argv[1], "source"))
    source_cases();
  return 0;
}
