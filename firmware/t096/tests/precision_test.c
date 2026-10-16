/* SPDX-License-Identifier: Apache-2.0 */
#include "precision.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static void pps(void) {
  for (int sign = -1; sign <= 1; sign += 2) {
    struct r2_pps_precision p;
    r2_pps_precision_init(&p);
    uint64_t t = 100000000, interval = 16000000 + sign * 320;
    for (unsigned i = 0; i < 241; i++) {
      assert(
          r2_pps_precision_add(&p, (struct r2_capture){t, 1, R2_CAPTURE_PPS}));
      t += interval;
      if (i < 240)
        assert(!p.fit_valid);
    }
    assert(p.fit_valid && p.drift_ppb == sign * 20000 && p.fit_rms_ns == 0);
    assert(abs(p.raw_rate_ppb - sign * 20000) <= 1 && p.prediction_rms_ns == 0);
    assert(abs(r2_pps_mapping_trim_ppb(&p) + sign * 20000) <= 1);
    /* Missing edge resets rather than pretending adjacent integer seconds. */
    assert(!r2_pps_precision_add(
        &p, (struct r2_capture){t + interval, 1, R2_CAPTURE_PPS}));
    assert(!p.fit_valid && !p.rate_valid && !p.error_count);
    assert(!r2_pps_precision_add(
        &p, (struct r2_capture){t, 1, R2_CAPTURE_UNKNOWN}));
    assert(
        r2_pps_precision_add(&p, (struct r2_capture){100, 2, R2_CAPTURE_PPS}));
    assert(p.epoch == 2 && p.count == 1 && !p.fit_valid);
  }
  /* Sub-us variation produces causal RMS without hiding the prior prediction.
   */
  struct r2_pps_precision p;
  r2_pps_precision_init(&p);
  uint64_t t = 100000000;
  for (unsigned i = 0; i < 300; i++) {
    assert(r2_pps_precision_add(
        &p, (struct r2_capture){t + (i % 2 ? 8 : 0), 3, R2_CAPTURE_PPS}));
    t += 16000000;
  }
  assert(p.fit_valid && p.fit_rms_ns >= 200 && p.fit_rms_ns <= 300);
  assert(p.prediction_rms_ns >= 500 && p.prediction_rms_ns <= 600);
}
static void servo(void) {
  r2_servo_clock_t c = {0};
  for (unsigned i = 1; i <= 600; i++) {
    uint64_t t = 160000000 + (uint64_t)i * 4000000;
    r2_servo_sample_t s = {1, i, t, t + t / 50000, t + 80000, false};
    assert(r2_servo_observe_permitted(&c, &s, true, true));
  }
  assert(c.fit_valid && c.output.valid && abs(c.rate - 20000) <= 1);
  assert(r2_servo_output_quality_rms(&c) < 100);
  assert(r2_servo_pll_locked(&c, c.last_available));
  /* Score the frozen delivered map, not the freshly refitted line. A500ns
   * constant bias must remain in RMS even though the fit is perfect. */
  c = (r2_servo_clock_t){0};
  for (unsigned i = 1; i <= 150; i++) {
    uint64_t t = 160000000 + (uint64_t)i * 16000000;
    r2_servo_sample_t s = {2, i, t, t, t + 80000, false};
    assert(r2_servo_observe_permitted(&c, &s, true, true));
  }
  c.output.anchor -= 500;
  for (unsigned i = 151; i <= 190; i++) {
    uint64_t t = 160000000 + (uint64_t)i * 16000000;
    r2_servo_sample_t s = {2, i, t, t, t + 80000, false};
    assert(r2_servo_observe_permitted(&c, &s, false, true));
  }
  assert(r2_servo_output_quality_rms(&c) == 500 &&
         !r2_servo_output_quality(&c));
  uint64_t t = c.last_local + 16000000;
  r2_servo_sample_t tail = {2, 191, t, t + 1600, t + 80000, false};
  assert(r2_servo_observe_permitted(&c, &tail, false, true));
  assert(r2_servo_output_quality_rms(&c) > 17000); /*100us tail retained*/
  assert(!r2_servo_observe_permitted(&c, &tail, true, true)); /*duplicate*/
  assert(!r2_servo_pll_locked(&c, c.last_available + 3 * R2_CAPTURE_HZ));
  tail = (r2_servo_sample_t){3,    1, t + 16000000, t + 16000000, t + 16080000,
                             false};
  assert(r2_servo_observe_permitted(&c, &tail, true, true));
  assert(!c.output.valid && !c.fit_valid && !c.output_quality.count);
}
static void source_clock(void) {
  struct r2_pps_precision p;
  struct r2_source_clock s;
  r2_pps_precision_init(&p);
  r2_source_clock_init(&s);
  uint64_t ns, generation;
  assert(r2_pps_precision_add(&p, (struct r2_capture){100, 1, R2_CAPTURE_PPS}));
  assert(!r2_source_clock_publish(&s, &p,
                                  (struct r2_capture){200, 1, R2_CAPTURE_NOW}));
  assert(r2_pps_precision_add(
      &p, (struct r2_capture){16000100, 1, R2_CAPTURE_PPS}));
  uint64_t first = 16080100;
  assert(r2_source_clock_publish(
      &s, &p, (struct r2_capture){first, 1, R2_CAPTURE_NOW}));
  assert(!r2_source_clock_read(
      &s, (struct r2_capture){first - 1, 1, R2_CAPTURE_RADIO}, &ns,
      &generation));
  assert(r2_source_clock_read(
      &s, (struct r2_capture){first, 1, R2_CAPTURE_RADIO}, &ns, &generation));
  assert(ns == 5000000 && generation == 1);
  uint64_t event = 30000000, old_ns, old_generation;
  assert(r2_source_clock_read(&s,
                              (struct r2_capture){event, 1, R2_CAPTURE_RADIO},
                              &old_ns, &old_generation));
  assert(r2_pps_precision_add(
      &p, (struct r2_capture){32000420, 1, R2_CAPTURE_PPS}));
  uint64_t next = 32240420, at_boundary;
  assert(r2_source_clock_read(&s,
                              (struct r2_capture){next, 1, R2_CAPTURE_RADIO},
                              &at_boundary, &generation));
  assert(r2_source_clock_publish(&s, &p,
                                 (struct r2_capture){next, 1, R2_CAPTURE_NOW}));
  /* Rate change cannot step the delivered clock or rewrite old event time. */
  assert(r2_source_clock_read(
      &s, (struct r2_capture){next, 1, R2_CAPTURE_RADIO}, &ns, &generation));
  assert(ns == at_boundary && generation == 2);
  assert(r2_source_clock_read(
      &s, (struct r2_capture){event, 1, R2_CAPTURE_RADIO}, &ns, &generation));
  assert(ns == old_ns && generation == old_generation);
  assert(!r2_source_clock_publish(
      &s, &p, (struct r2_capture){next + 1, 1, R2_CAPTURE_NOW}));
  assert(!r2_source_clock_read(
      &s, (struct r2_capture){next, 2, R2_CAPTURE_RADIO}, &ns, &generation));
  assert(!r2_source_clock_read(
      &s, (struct r2_capture){next, 1, R2_CAPTURE_UNKNOWN}, &ns, &generation));
  assert(!r2_source_clock_read(
      &s, (struct r2_capture){p.last + 24000000, 1, R2_CAPTURE_RADIO}, &ns,
      &generation));
  for (unsigned i = 0; i < 10; ++i) {
    assert(r2_pps_precision_add(
        &p, (struct r2_capture){p.last + 16000320, 1, R2_CAPTURE_PPS}));
    assert(r2_source_clock_publish(
        &s, &p, (struct r2_capture){p.last + 80000, 1, R2_CAPTURE_NOW}));
  }
  assert(!r2_source_clock_read(
      &s, (struct r2_capture){event, 1, R2_CAPTURE_RADIO}, &ns, &generation));
  /* A late PPS update cannot backfill an expired delivered-map interval. */
  assert(r2_pps_precision_add(
             &p, (struct r2_capture){p.last + 32000000, 1, R2_CAPTURE_PPS}) ==
         false);
  assert(!r2_source_clock_publish(
      &s, &p, (struct r2_capture){p.last + 80000, 1, R2_CAPTURE_NOW}));
  assert(s.count == 0);
}
static void source_phase(void){
 for(int sign=-1;sign<=1;sign+=2){
  struct r2_pps_precision p;struct r2_source_clock s;
  r2_pps_precision_init(&p);r2_source_clock_init(&s);uint64_t tick=100000;
  for(unsigned sec=0;sec<800;sec++){
   if(sec)tick+=sec>=100?(uint64_t)(16000000+sign*72):16000000;
   struct r2_capture edge={tick,1,R2_CAPTURE_PPS},available={tick+16000+(sec%7)*16000,1,R2_CAPTURE_NOW};
   uint64_t ns,gen,old=0;bool have=r2_source_clock_read(&s,available,&old,&gen);
   if(r2_source_clock_read(&s,edge,&ns,&gen)&&sec>=600){
    int64_t error=(int64_t)ns-(int64_t)(sec-1)*INT64_C(1000000000);
    assert(error>=-10&&error<=10); /* Causal signed error, not demeaned. */
   }
   assert(r2_pps_precision_add(&p,edge));
   if(p.rate_valid){assert(r2_source_clock_publish(&s,&p,available));
    assert(r2_source_clock_read(&s,available,&ns,&gen));if(have)assert(ns==old);
    uint64_t next;assert(r2_source_clock_read(&s,(struct r2_capture){available.tick+1,1,R2_CAPTURE_RADIO},&next,&gen));assert(next>ns);
   }
  }
 }
}
int main(void) {
  pps();
  servo();
  source_clock();
  source_phase();
  char text[24];
  r2_precision_format(text, sizeof(text), 999, true);
  assert(!strcmp(text, "999 ns"));
  r2_precision_format(text, sizeof(text), 1000, true);
  assert(!strcmp(text, "1.000 us"));
  r2_precision_format(text, sizeof(text), 1001, true);
  assert(!strcmp(text, "1.001 us"));
  r2_precision_format(text, sizeof(text), 0, false);
  assert(!strcmp(text, "--"));
  puts("PASS precision PPS drift/trim/241point fit, causal servo, bias/tails, "
       "stale/epoch, RMS units");
}
