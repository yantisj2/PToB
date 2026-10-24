/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_clock.h"
#include <hal/nrf_rtc.h>
#include <hal/nrf_timer.h>
#include <limits.h>
/* At most two reads: a preempted publication drops this opportunity instead
 * of spinning in LLL or reading a torn64bit counter. */
static uint32_t serial, base_lo, base_hi, base_raw, base_epoch, allowed_epoch,
    base_guard;
void r2_gm7_clock_begin(uint32_t epoch) {
  __atomic_store_n(&allowed_epoch, epoch, __ATOMIC_RELEASE);
}
void r2_gm7_clock_invalidate(void) {
  __atomic_store_n(&allowed_epoch, 0, __ATOMIC_RELEASE);
}
void r2_gm7_clock_publish(uint64_t extended, uint32_t raw, uint64_t epoch,
                          bool valid) {
  if (!valid) {
    r2_gm7_clock_invalidate();
    return;
  }
  uint32_t s = __atomic_load_n(&serial, __ATOMIC_RELAXED);
  if (s > UINT32_MAX - 2) {
    r2_gm7_clock_invalidate();
    __atomic_store_n(&base_epoch, 0, __ATOMIC_RELEASE);
    return;
  }
  __atomic_store_n(&serial, s + 1, __ATOMIC_RELEASE);
  __atomic_store_n(&base_lo, (uint32_t)extended, __ATOMIC_RELAXED);
  __atomic_store_n(&base_hi, (uint32_t)(extended >> 32), __ATOMIC_RELAXED);
  __atomic_store_n(&base_raw, raw, __ATOMIC_RELAXED);
  __atomic_store_n(&base_epoch,
                   valid && epoch && epoch <= UINT32_MAX ? (uint32_t)epoch : 0,
                   __ATOMIC_RELAXED);
  __atomic_store_n(&base_guard, nrf_rtc_counter_get(NRF_RTC1),
                   __ATOMIC_RELAXED);
  __atomic_store_n(&serial, s + 2, __ATOMIC_RELEASE);
}
static bool reference(uint64_t *ticks, uint32_t *raw, uint32_t *epoch) {
  for (unsigned attempt = 0; attempt < 2; attempt++) {
    uint32_t before = __atomic_load_n(&serial, __ATOMIC_ACQUIRE);
    if (before & 1)
      continue;
    uint32_t lo = __atomic_load_n(&base_lo, __ATOMIC_RELAXED),
             hi = __atomic_load_n(&base_hi, __ATOMIC_RELAXED),
             r = __atomic_load_n(&base_raw, __ATOMIC_RELAXED),
             e = __atomic_load_n(&base_epoch, __ATOMIC_RELAXED),
             guard = __atomic_load_n(&base_guard, __ATOMIC_RELAXED);
    if (before != __atomic_load_n(&serial, __ATOMIC_ACQUIRE))
      continue;
    /* Read-only negative continuity lease. RTC1 is the pinned Zephyr system
     * RTC; its value NEVER supplies GM7 time or a physical endpoint. Together
     * with the existing1s extension watchdog and hardware watchdog reset this
     * prevents a stalled HF extender aliasing a full TIMER3 wrap. */
    if (((nrf_rtc_counter_get(NRF_RTC1) - guard) & 0xffffffu) > 32768u)
      return false;
    if (!e || e != __atomic_load_n(&allowed_epoch, __ATOMIC_ACQUIRE))
      return false;
    *ticks = ((uint64_t)hi << 32) | lo;
    *raw = r;
    *epoch = e;
    return true;
  }
  return false;
}
static bool current_counter(uint64_t *ticks, uint32_t *raw, uint32_t *epoch) {
  uint64_t base;
  uint32_t reference_raw, e;
  if (!reference(&base, &reference_raw, &e))
    return false;
  /* CC2 is the existing software NOW channel, never a physical edge. This
   * reader samples without extending the counter. CC5 stays END-only. */
  nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_CAPTURE2);
  uint32_t current = nrf_timer_cc_get(NRF_TIMER3, NRF_TIMER_CC_CHANNEL2),
           delta = current - reference_raw;
  if (delta > 16000000 || base > UINT64_MAX - delta ||
      e != __atomic_load_n(&allowed_epoch, __ATOMIC_ACQUIRE))
    return false;
  *ticks = base + delta;
  *raw = current;
  *epoch = e;
  return true;
}
bool r2_gm7_clock_event(uint32_t raw, uint32_t epoch, uint64_t *ticks) {
  uint64_t current;
  uint32_t current_raw, e;
  if (!ticks || !current_counter(&current, &current_raw, &e) || e != epoch)
    return false;
  uint32_t age = current_raw - raw;
  /* Queue age is against the CURRENT counter, independently of the age of
   * the last serialized extension publication. Never a base-relative age. */
  if (age > 4000000 || current < age)
    return false;
  *ticks = current - age;
  return true;
}
bool r2_gm7_clock_now(uint64_t *us, uint32_t *epoch) {
  uint64_t ticks;
  uint32_t raw;
  if (!us || !epoch || !current_counter(&ticks, &raw, epoch))
    return false;
  *us = ticks / 16;
  return true;
}

bool r2_gm7_clock_start(uint32_t raw, uint32_t epoch, unsigned phy,
                        uint64_t *start) {
  if (!start || (phy != 1 && phy != 2))
    return false;
  uint64_t tick;
  if (!r2_gm7_clock_event(raw, epoch, &tick))
    return false;
  uint64_t prefix = (phy == 1 ? 40u : 24u) * 16u;
  if (tick < prefix)
    return false;
  *start = tick - prefix;
  return true;
}

bool r2_gm7_clock_bounds(uint32_t raw, uint32_t epoch, unsigned prefix,
                         unsigned chain, uint64_t *lo, uint64_t *hi) {
  if (!lo || !hi || prefix > 40 || chain > 10)
    return false;
  uint64_t tick;
  if (!r2_gm7_clock_event(raw, epoch, &tick))
    return false;
  uint64_t subtract = (uint64_t)prefix * 16, margin = (uint64_t)chain * 16 + 1;
  if (tick < subtract || tick - subtract < margin ||
      tick - subtract > UINT64_MAX - margin)
    return false;
  *lo = tick - subtract - margin;
  *hi = tick - subtract + margin;
  return true;
}
