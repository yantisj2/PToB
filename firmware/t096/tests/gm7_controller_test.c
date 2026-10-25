/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_controller_fake.h"
#include "gm7_controller_under_test.h"
#include <stdio.h>
#include "gm7_vectors.h"
static unsigned clock_reads, second_read_advance;
static bool second_read_invalid;
bool r2_gm7_clock_now(uint64_t *now, uint32_t *epoch) {
  if (++clock_reads == 2) {
    fake_now += second_read_advance;
    if (second_read_invalid)
      fake_clock_valid = false;
  }
  *now = fake_now;
  *epoch = fake_epoch;
  return fake_clock_valid;
}
bool r2_gm7_clock_bounds(uint32_t raw, uint32_t epoch, unsigned prefix,
                         unsigned chain, uint64_t *lo, uint64_t *hi) {
  if (!fake_clock_valid || epoch != fake_epoch ||
      raw < (prefix + chain) * 16 + 1)
    return false;
  *lo = raw - prefix * 16 - chain * 16 - 1;
  *hi = raw - prefix * 16 + chain * 16 + 1;
  return true;
}
bool r2_gm7_capture_arm(void *o, bool t, bool a) {
  (void)o;
  (void)t;
  (void)a;
  if (fake_capture_busy)
    return false;
  fake_capture_armed = fake_capture_busy = true;
  ++fake_arms;
  return true;
}
void r2_gm7_capture_abort(void *o) {
  (void)o;
  fake_capture_armed = fake_capture_busy = false;
}
bool r2_gm7_capture_busy(void) { return fake_capture_busy; }
void r2_gm7_capture_done(void *o, bool done, bool crc, const void *p) {
  (void)crc;
  (void)p;
  if (!fake_capture_armed)
    return;
  fake_capture_armed = fake_capture_busy = false;
  bool auxiliary = fake_phy == 2;
  uint32_t address = (uint32_t)((fake_now + (auxiliary ? 24 : 40)) * 16),
           end = (uint32_t)(fake_now * 16);
  if (!auxiliary) {
    address = (uint32_t)((fake_now - 184 + 42) * 16);
    end = (uint32_t)(fake_now * 16);
  }
  r2_gm7_controller_capture(o, auxiliary, address, end, fake_epoch, done);
}

static struct gm7_scheduler jobs;
static struct gm7_packet announce(void) {
  struct r2_group_config g = {.id = UINT64_C(0x0102030405060708), .epoch = 1};
  for (unsigned i = 0; i < 32; i++)
    g.psk[i] = (uint8_t)i;
  struct gm7_packet p;
  for (unsigned i = 0; i < sizeof fixtures / sizeof fixtures[0]; i++)
    if (!strcmp(fixtures[i].name, "announce")) {
      assert(!gm7_decode(&p, fixtures[i].wire, fixtures[i].size, &g));
      return p;
    }
  assert(false);
  return p;
}
static void reset(void) {
  memset(native, 0, sizeof native);
  fake_now = fake_ticker_now = 1000000;
  fake_epoch = 4;
  fake_clock_valid = fake_done = true;
  fake_capture_armed = fake_capture_busy = false;
  fake_hf_refs = fake_cleanup = fake_disabled = fake_arms = 0;
  generation = 1;
  source_live = 0;
  fake_ticker_status = 0;
  fake_mayfly = NULL;
  gm7_scheduler_init(&jobs, fake_now);
  clock_reads = second_read_advance = 0;
  second_read_invalid = false;
}
static void queued(void) {
  struct gm7_packet p = announce();
  assert(p.kind == GM7_ANNOUNCE);
  struct r2_group_config g = {.id = p.group, .epoch = p.key_epoch};
  for (unsigned i = 0; i < 32; i++)
    g.psk[i] = (uint8_t)i;
  struct gm7_window w = {.nominal = 1010000,
                         .latest_start = 1012000,
                         .latest_end = 1014000,
                         .enclosure_us = 500,
                         .channel = 37,
                         .physical = true};
  uint64_t ticket;
  assert(!gm7_scheduler_submit(&jobs, 0, &p, &g, w, 2500000, 0, 0, fake_now,
                               &ticket));
}
static void submit(void) {
  queued();
  uint8_t address[6] = {1, 2, 3, 4, 5, 0xc6};
  assert(!r2_gm7_controller_submit(0, &jobs, address, fake_now));
  r2_gm7_controller_source(2500000, true);
}
static void admission_errors(void) {
  uint8_t address[6] = {1, 2, 3, 4, 5, 0xc6};
  for (unsigned mode = 0; mode < 7; ++mode) {
    reset();
    queued();
    if (mode == 0)
      fake_now = jobs.job[0].window.nominal - 2999;
    if (mode == 1)
      fake_clock_valid = false;
    if (mode == 2)
      second_read_advance = 10000;
    if (mode == 3)
      second_read_advance = 9900;
    if (mode == 4)
      second_read_invalid = true;
    if (mode == 5)
      fake_ticker_status = TICKER_STATUS_FAILURE;
    if (mode == 6)
      jobs.job[0].window.nominal = fake_now + 250001;
    int expected[] = {-ETIMEDOUT, -ESTALE, -ETIME, -ERANGE,
                      -ESTALE,    -EBUSY,  -EINVAL};
    assert(r2_gm7_controller_submit(0, &jobs, address, 1000000) ==
           expected[mode]);
    assert(!native[0].owned && !r2_gm7_capture_busy());
  }
  reset();
  queued();
  fake_now = jobs.job[0].window.nominal - 3000;
  assert(!r2_gm7_controller_submit(0, &jobs, address, 1000000));
  assert(!r2_gm7_controller_drain(0, fake_now));
  puts("PASS actual native admission exact3ms boundary and distinct entrylead, "
       "clock/epoch, post-build expiry, prepare-offset, ticker and range "
       "errors");
}
static void prepare(void) {
  gm7_ticker_job(1009000, 0, 0, 0, 0, &native[0]);
  assert(fake_mayfly && native[0].queued && native[0].ull.refs == 1);
  fake_now = 1009500;
  fake_mayfly->fp(fake_mayfly->param);
  assert(fake_hf_refs == 1 && !native[0].queued && fake_phy == 1 &&
         fake_channel == 37 && fake_capture_armed);
}
static void successful_pair(void) {
  reset();
  submit();
  assert(r2_gm7_controller_drain(0, fake_now) ==
         0); /* untouched ticker, no mayfly */
  reset();
  submit();
  prepare();
  fake_now = 1010684;
  auxiliary_isr(&native[0].lll);
  assert(!native[0].aborted && fake_phy == 2 && fake_channel <= 36 &&
         native[0].result.exposed && fake_capture_armed);
  fake_now = 1011821;
  finish_isr(&native[0].lll);
  struct r2_gm7_tx r;
  assert(r2_gm7_controller_result(&r) && r.complete && r.exposed &&
         !fake_hf_refs && fake_cleanup == 1);
  assert(!r2_gm7_controller_result(&r));
  assert(!r2_gm7_controller_drain(0, fake_now) && r2_gm7_controller_idle());
  puts("PASS actual native controller immutable primary37/LE1M -> AuxPtr "
       "datachannel/LE2M, physical callbacks, complete event result-once and "
       "clock/LLL/drain cleanup");
}
static void shortened_and_cancelled(void) {
  for (unsigned mode = 0; mode < 3; mode++) {
    reset();
    submit();
    prepare();
    fake_now = 1010684;
    if (mode == 0)
      r2_gm7_controller_source(
          fake_now + 1000,
          true); /* covers START, fails gap341+payload796+margin */
    if (mode == 1)
      r2_gm7_controller_cancel(fake_now);
    if (mode == 2)
      ++fake_epoch;
    auxiliary_isr(&native[0].lll);
    assert(native[0].aborted && !native[0].result.exposed && fake_disabled &&
           fake_phy == 1);
    finish_isr(&native[0].lll);
    struct r2_gm7_tx r;
    assert(r2_gm7_controller_result(&r) && !r.complete);
    assert(!r2_gm7_controller_drain(0, fake_now));
  }
  reset();
  submit();
  gm7_ticker_job(1009000, 0, 0, 0, 0, &native[0]);
  assert(r2_gm7_controller_drain(0, fake_now) == -EAGAIN);
  fake_now = 1013000;
  fake_mayfly->fp(fake_mayfly->param);
  assert(native[0].aborted);
  finish_isr(&native[0].lll);
  assert(!r2_gm7_controller_drain(0, fake_now));
  reset();
  submit();
  gm7_ticker_job(1009000, 0, 0, 1, 0, &native[0]);
  struct r2_gm7_tx r;
  assert(r2_gm7_controller_result(&r) && !r.complete);
  assert(!r2_gm7_controller_drain(0, fake_now));
  puts("PASS current shortened source must cover entire remaining primary/AUX "
       "gap+payload; stale generation/epoch refuses AUX, pending mayfly owns "
       "drain, late prepare/lazy ticker abort finitely");
}
static void scan_and_anchor(void) {
  reset();
  r2_gm7_controller_scan(true, false, 0);
  assert(r2_gm7_controller_scan37());
  r2_gm7_controller_scan(true, true, 0);
  assert(r2_gm7_controller_scan37());
  ++fake_epoch;
  assert(!r2_gm7_controller_scan37());
  r2_gm7_controller_scan(false, false, 0);
  assert(!r2_gm7_controller_receiving());
  struct ull_hdr u = {.ticks_prepare_to_start = 2000, .ticks_slot = 2500};
  fake_epoch = 4;
  r2_gm7_controller_precision(true, 0, 1400000, 7);
  r2_gm7_controller_precision_bind(3, 7);
  uint32_t anchor = fake_ticker_now + 10000;
  assert(r2_gm7_precision_anchor(3, &u, &anchor, 62500));
  uint32_t start = anchor + lll_event_offset_get(&u) + EVENT_OVERHEAD_START_US +
                   radio_tx_ready_delay_get(1, 0);
  assert(start % 62500 == 0 && start + 2501 <= 1400000);
  assert(!r2_gm7_precision_anchor(4, &u, &anchor, 62500));
  assert(!r2_gm7_precision_anchor(3, &u, &anchor, 62501));
  r2_gm7_controller_precision_bind(3, 8);
  assert(!r2_gm7_precision_anchor(3, &u, &anchor, 62500));
  r2_gm7_controller_precision(false, 0, 0, 0);
  assert(r2_gm7_precision_anchor(55, &u, &anchor, 62500));
  puts("PASS actual controller phase-independent GM37 policy and epoch fence, "
       "prospective exact-handle/train62.5ms G1 anchor, SyncInfo ticker phase, "
       "stale/mismatched rejection and RC inactive bypass");
}
int main(void) {
  admission_errors();
  successful_pair();
  shortened_and_cancelled();
  scan_and_anchor();
  return 0;
}
