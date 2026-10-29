/* MIT. Copyright (c) 2026 Jonathan Yantis. Unchanged actual watchdog consumer. */
#include "gm7_watchdog_fixture.inc"
#include "gm7_resume_progress.h"
int main(int argc, char **argv) {
  assert(argc == 2);
  unsigned count;
  struct gm7_progress_record records[GM7_PROGRESS_RECORDS];
  FILE *f = fopen(argv[1], "rb");
  assert(f && fread(&count, sizeof count, 1, f) == 1 &&
         count <= GM7_PROGRESS_RECORDS &&
         fread(records, sizeof records[0], count, f) == count && !fclose(f));
  for (unsigned i = 0; i < count; i++) {
    struct gm7_progress_record *r = &records[i];
    memset((void *)r2_watchdog_capsule, 0, sizeof r2_watchdog_capsule);
    clear_runtime();
    id.reset_cause = 0;
    assert(!r2_watchdog_boot(&id));
    advance((uint32_t)(r->now / 1000));
    r2_watchdog_enable(true);
    r2_watchdog_observe(&r->status, true);
    assert((uint32_t)report_worker_stalled == r->expected_stall);
    bool feeding = watchdog_step();
    if (r->expected_stall) {
      assert(!feeding && reset_reason == WD_RC_WORKER_STALL &&
             r2_watchdog_capsule[32] == r->expected_stall);
      struct r2_service_status healthy = records[2].status;
      advance((uint32_t)(r->now / 1000) + 1);
      healthy.at = fake_ms * 1000;
      r2_watchdog_observe(&healthy, true);
      assert(!report_worker_stalled && !watchdog_step() &&
             reset_reason == WD_RC_WORKER_STALL); /* Terminal stays terminal. */
    } else {
      assert(feeding && !reset_reason);
    }
  }
  /* Existing actual observer checks retain current error/drain/core decisions. */
  owner_failure_cases();
  puts("PASS actual unchanged watchdog consumes GM resume snapshots: no "
       "false late-handoff reset; exact30s/error/drain/terminal guards retained");
  return 0;
}
