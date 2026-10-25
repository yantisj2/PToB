/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual service bridge+cycle and actual HF reader; fake public HAL/kernel.
 */
#define R2_GM7_REAL_CLOCK 1
#define main unused_gm7_service_cases
#include "gm7_service_test.c"
#undef main
static unsigned pending_preemption, intrinsic_latency, blocked_preemptions;
uint32_t gm7_test_counter_read(void) {
  if (pending_preemption && fake_irq_key) {
    ++blocked_preemptions;
    pending_preemption = 0;
  } else if (pending_preemption) {
    fake_hf += pending_preemption;
    fake_kernel += pending_preemption;
    pending_preemption = 0;
    /* A preempting capture owner may publish the same physical epoch. */
    r2_gm7_clock_publish(fake_hf * 16, (uint32_t)(fake_hf * 16), 4, true);
  }
  fake_hf += intrinsic_latency;
  fake_kernel += intrinsic_latency;
  intrinsic_latency = 0;
  return (uint32_t)(fake_hf * 16);
}
uint32_t gm7_test_rtc_read(void) {
  return (uint32_t)(fake_kernel * 32768 / 1000000) & 0xffffff;
}
int main(int argc, char **argv) {
  assert(argc == 2);
  unsigned mode = (unsigned)strtoul(argv[1], NULL, 10);
  assert(mode <= 5);
  setup(false);
  r2_gm7_clock_begin(4);
  r2_gm7_clock_publish(fake_hf * 16, (uint32_t)(fake_hf * 16), 4, true);
  cycle(&service_work);
  assert(status.cycles == 1 && status.active && !status.error);
  if (mode == 0 || mode == 5) {
    pending_preemption = 500;
    cycle(&service_work);
    assert(blocked_preemptions && !pending_preemption && !fake_irq_key &&
           status.cycles == 2 && status.active && !status.error);
    /* A nested caller retains its own original mask; bridge cannot unlock it.
     */
    fake_irq_key = 1;
    struct gm7_bridge b;
    assert(bridge(&b) && fake_irq_key == 1);
    fake_irq_key = 0;
    if (mode == 5) {
      r2_service_stop();
      cycle(&service_work);
      assert(status.closed && !status.active && !status.error && !fake_irq_key);
    }
  } else {
    if (mode == 1)
      intrinsic_latency = 101; /* No relaxed100us enclosure, even masked. */
    if (mode == 2)
      r2_gm7_clock_invalidate();
    if (mode == 3) {
      r2_gm7_clock_begin(5);
      r2_gm7_clock_publish(fake_hf * 16, (uint32_t)(fake_hf * 16), 5, true);
    }
    if (mode == 4)
      tick_to(fake_hf + 1000001); /* Original1s continuity lease. */
    cycle(&service_work);
    assert(status.error == -ESTALE && !status.active && status.closed &&
           !fake_irq_key);
  }
  printf("PASS actual service/HF paired bracket mode%u: preemption/CC2 "
         "publication exclusion, nested IRQ restore, unchanged latency/domain "
         "fences and drain\n",
         mode);
  return 0;
}
