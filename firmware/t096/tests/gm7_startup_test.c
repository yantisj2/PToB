/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Compose actual service, main startup, time-start and capture owners. Only
 * public kernel/BLE/GNSS-entry/HAL boundaries are synthetic; never hardware. */
#define main gm7_existing_service_cases
#include "gm7_service_test.c"
#undef main
#include "core.h"
static bool platform_ready, source_started, normal_started, normal_start_failed,
    poweroff_requested, fake_rc, fake_ui_off;
static struct r2_maint maintenance;
static uint64_t boot_deadline, diag_delay;
static unsigned diag_arms, start_calls, start_events, diag_cancels;
static int last_start, diag_result;
extern bool probe_async_pending(void);
extern int probe_async_error(void);
extern int probe_async_stop(void);
extern void probe_async_poll(int64_t now);
extern void probe_async_failure(unsigned failure);
extern unsigned probe_async_requests(void);
void r2_ui_start(void) {}
bool r2_ui_poweroff_requested(void) { return fake_ui_off; }
bool r2_switch_rc_active(void) { return fake_rc; }
int r2_diag_arm(uint64_t arrival, uint32_t seconds) {
  (void)arrival;
  assert(!seconds);
  ++diag_arms;
  tick_to(fake_hf + diag_delay);
  return diag_result;
}
void r2_diag_cancel_arm(int result) {
  assert(result < 0);
  ++diag_cancels;
}
static void event(uint32_t kind, int result) {
  if (kind == 12) {
    last_start = result;
    ++start_events;
  }
}
void probe_hf_published(bool valid) { fake_hf_valid = valid; }
int64_t r2_time_now_us(void) { return (int64_t)fake_kernel; }
static int dispatch_run(uint64_t at) {
  ++start_calls;
  return fake_rc ? 0 : r2_service_run(at);
}
static void actual_main_start_block(void) {
  uint64_t now = fake_kernel / 1000;
#define r2_service_run dispatch_run
#include "gm7_startup_main_under_test.h"
#undef r2_service_run
}
static void startup_diagnostic(bool pending, int error, bool active) {
  char text[2048];
  assert(r2_gm7_diagnostic_format(text, sizeof text, 0, "fake-source", 33,
                                  true) > 0);
  assert(strstr(text, pending ? "\"startup_pending\":true"
                              : "\"startup_pending\":false"));
  assert(strstr(text, active ? "\"available\":true" : "\"available\":false"));
  char field[64];
  snprintf(field, sizeof field, "\"startup_error\":%d", error);
  assert(strstr(text, field));
}
int main(int argc, char **argv) {
  assert(argc == 2);
  unsigned mode = (unsigned)strtoul(argv[1], NULL, 10);
  assert(mode <= 14);
  setup(false);
  used = started = false;
  status.active = false;
  status.closed = true;
  memset(&input, 0, sizeof input); /* No GNSS fix or dwell qualifies startup. */
  fake_hf_valid = false;
  platform_ready = true;
  maintenance.phase = R2_IDLE;
  fake_rc = mode == 8;
  boot_deadline = fake_kernel / 1000 + ((mode == 2 || mode == 3) ? 1 : 2000);
  uint64_t original_deadline = boot_deadline;
  actual_main_start_block();
  assert(source_started && probe_async_pending() &&
         probe_async_requests() == 1);
  if (fake_rc) {
    assert(normal_started && !normal_start_failed && start_calls == 1 &&
           diag_arms == 1 && start_events == 1 && !last_start);
    actual_main_start_block();
    assert(start_calls == 1 && diag_arms == 1);
    puts("PASS actual RC startup branch remains immediate before HF "
         "publication");
    return 0;
  }
  assert(!normal_started && !normal_start_failed && !start_calls &&
         !diag_arms && !start_events && !status.active);
  startup_diagnostic(true, 0, false);
  actual_main_start_block();
  assert(!start_calls && !diag_arms && probe_async_requests() == 1 &&
         boot_deadline == original_deadline);
  if (mode == 1)
    probe_async_failure(1); /* Real capture owner original20ms timeout. */
  if (mode == 4)
    probe_async_failure(2); /* Public nrfx init fails. */
  if (mode == 5)
    fake_group_start_result = -EIO;
  if (mode == 6)
    maintenance.phase = R2_CLOSING;
  if (mode == 7)
    poweroff_requested = true;
  if (mode == 9)
    diag_result = -EBUSY;
  if (mode == 10) {
    assert(!probe_async_stop());
    input.stopped = true;
  }
  if (mode == 11)
    diag_delay = 2000000; /* Deadline cannot renew during diagnostic arm. */
  if (mode == 12)
    fake_ui_off = true;
  if (mode == 13)
    fake_gnss_error = -EIO;
  if (mode == 14)
    input.fatal = true;
  tick_to(fake_hf + (mode == 1 ? 20000 : mode == 3 ? 2000 : 1000));
  probe_async_poll((int64_t)fake_kernel);
  fake_capture_error = probe_async_error();
  actual_main_start_block();
  assert(boot_deadline == original_deadline && probe_async_requests() == 1 &&
         start_events == 1);
  if (!mode) {
    assert(normal_started && !normal_start_failed && !last_start &&
           diag_arms == 1 && start_calls == 1 && !diag_cancels &&
           status.active);
    startup_diagnostic(false, 0, true);
  } else {
    int expected =
        mode == 1 || mode == 2 || mode == 3 || mode == 11    ? -ETIMEDOUT
        : mode == 6 || mode == 7 || mode == 10 || mode == 12 ? -ECANCELED
        : mode == 9                                          ? -EBUSY
                                                             : -EIO;
    assert(!normal_started && normal_start_failed && last_start == expected &&
           status.error == expected && !status.active && !status.serving);
    assert(diag_arms == (mode == 5 || mode == 9 || mode == 11));
    assert(start_calls == (mode == 5));
    assert(diag_cancels == (mode == 5 || mode == 11));
    startup_diagnostic(false, expected, false);
  }
  /* Neither later publication, cleared fault, maintenance exit nor poweroff
   * withdrawal can renew/revive a terminal initial start. */
  unsigned calls = start_calls, arms = diag_arms;
  fake_capture_error = fake_gnss_error = fake_group_start_result = 0;
  fake_hf_valid = true;
  maintenance.phase = R2_IDLE;
  poweroff_requested = fake_ui_off = false;
  actual_main_start_block();
  assert(start_calls == calls && diag_arms == arms && start_events == 1 &&
         probe_async_requests() == 1 && boot_deadline == original_deadline);
  printf("PASS actual async main/time/capture/GM7 startup mode%u: bounded "
         "pending, one arm/start, terminal latch and truthful "
         "availability/error\n",
         mode);
  return 0;
}
