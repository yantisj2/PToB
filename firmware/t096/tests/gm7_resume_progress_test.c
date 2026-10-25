/* MIT. Copyright (c) 2026 Jonathan Yantis. Actual GM/dispatcher snapshots.
 * The driver includes existing fake platform/RC boundaries and original bodies.
 */
#include "gm7_resume_fixture.inc"
#include "gm7_dispatch_snapshot.inc"
#include "gm7_resume_progress.h"
void r2_dynamic_service_snapshot(struct r2_service_status *s) {
  memset(s, 0, sizeof *s);
}
static struct gm7_progress_record records[GM7_PROGRESS_RECORDS];
static unsigned count;
static void unavailable(const struct r2_service_status *s) {
  assert(!s->serving && !s->follower && !s->follower_clock_valid &&
         !s->hot_ready && !s->precision.map_valid &&
         !s->precision.authorized && !s->precision.locked &&
         !s->precision.rms_valid && !s->service_until && !s->authority_until);
}
static void record(uint32_t expected) {
  assert(count < GM7_PROGRESS_RECORDS);
  struct gm7_progress_record *r = &records[count++];
  r->now = fake_kernel;
  dispatcher_snapshot(&r->status);
  r->expected_stall = expected;
  unavailable(&r->status);
}
static void begin_late_rc(void) {
  cold_rc();
  fake_kernel = 73000000;
  fake_hf = 1000000;
}
int main(int argc, char **argv) {
  assert(argc == 2);
  begin_late_rc();
  request_gm();
  uint64_t accepted = fake_kernel, deadline = dispatch_until;
  assert(status.at == accepted && !status.closing && !status.close_deadline);
  record(0); /* Before any GM worker, beyond the original30s WDT age. */
  for (unsigned i = 0; i < 3; i++) {
    tick_to(fake_hf + 100);
    struct r2_service_status s;
    dispatcher_snapshot(&s);
    assert(s.at == accepted && dispatch_until == deadline);
  }
  cycle(&service_work);
  dispatcher_supervise();
  assert(status.active && !dispatch_pending && status.at == accepted);
  record(0); /* Real start completed, before first normal publication. */
  tick_to(fake_hf + 2000);
  cycle(&service_work);
  assert(status.at == fake_kernel);
  record(0);

  /* A completed old close is not a current close deadline after a warm return. */
  assert(!dispatcher_profile(R2_PROFILE_RC, fake_kernel / 1000));
  cycle(&service_work);
  dispatcher_supervise();
  assert(r2_switch_rc_active() && status.closed && status.closing);
  tick_to(fake_hf + 60000000);
  assert(fake_kernel >= status.close_deadline);
  status.hot_ready = status.precision.map_valid = status.precision.authorized =
      status.precision.locked = status.precision.rms_valid = true;
  status.service_until = status.authority_until = UINT64_MAX;
  request_gm();
  assert(status.at == fake_kernel && !status.closing && !status.close_deadline);
  record(0);
  cycle(&service_work);
  dispatcher_supervise();
  record(0);
  tick_to(fake_hf + 2000);
  cycle(&service_work);
  record(0);

  /* Pending reset work and queries never renew the accepted handoff sample. */
  begin_late_rc();
  request_gm();
  accepted = status.at;
  deadline = dispatch_until;
  fake_reset_result = -EAGAIN;
  cycle(&service_work);
  tick_to(fake_hf + 29999999);
  cycle(&service_work);
  assert(status.at == accepted && dispatch_until == deadline && resuming);
  record(0);
  tick_to(fake_hf + 1);
  record(1); /* Exact unchanged30s worker progress limit. */

  begin_late_rc();
  request_gm();
  fake_group_start_result = -EIO;
  cycle(&service_work);
  dispatcher_supervise();
  assert(status.error == -EIO && !owner.serving);
  record(2u | ((uint32_t)(uint16_t)-EIO << 16));
  cycle(&service_work);
  assert(status.closed && !status.active && status.error == -EIO);

  begin_late_rc();
  request_gm();
  r2_service_stop();
  cycle(&service_work);
  assert(!resuming && !status.active && status.closed && !owner.serving);
  record(0);

  FILE *f = fopen(argv[1], "wb");
  assert(f && fwrite(&count, sizeof count, 1, f) == 1 &&
         fwrite(records, sizeof records[0], count, f) == count && !fclose(f));
  printf("PASS %u actual GM/dispatcher snapshots: late cold/warm return, one "
         "handoff sample, original30s limit, error/cancel and no authority\n", count);
  return 0;
}
