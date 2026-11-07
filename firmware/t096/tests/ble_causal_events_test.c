/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "../src/ble_causal_events.c"
#include "core.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static int64_t fake_ms;
static unsigned notes, note_attempts;
static bool blocked;
static char last_note[512];
int64_t k_uptime_get(void) { return fake_ms; }
int r2_console_note(const char *s, size_t n, uint64_t deadline) {
  assert(n < sizeof last_note &&
         deadline ==
             ((uint64_t)fake_ms <= UINT64_MAX - 150 ? (uint64_t)fake_ms + 150 : UINT64_MAX));
  ++note_attempts;
  if (blocked)
    return -EAGAIN;
  memcpy(last_note, s, n);
  last_note[n] = 0;
  ++notes;
  return 0;
}
static struct r2_ble_causal_record sample(bool usable) {
  return (struct r2_ble_causal_record){
      .root = 17,
      .boot = 88,
      .source = 3,
      .train = 4,
      .capture = 5,
      .epoch = 6,
      .sample_us = 1000000,
      .commit_us = 1000001,
      .tick = 16000000,
      .physical_until = 40000000,
      .data_until = 35000000,
      .grant_root = 17,
      .grant_boot = 88,
      .grant_source = 3,
      .grant_train = 4,
      .grant_capture = 5,
      .grant_until = 10000000,
      .scope_until = 10000000,
      .effective_until = 2200000,
      .flags = R2_BLE_FOLLOWING | R2_BLE_MANAGED | R2_BLE_CAPTURED | R2_BLE_SAMPLE_KNOWN | R2_BLE_BOUND |
               R2_BLE_SCOPE_EQUAL | (usable ? R2_BLE_AVAILABLE : 0)};
}
int main(void) {
  struct r2_ble_causal_record r = sample(false);
  r.why = R2_FOLLOW_FIT | R2_FOLLOW_OUTPUT;
  r2_ble_causal_observe(&r);
  assert(!causal.meta.losses && R2_BLE_KIND(causal.latest) == R2_BLE_ACQUIRING);
  r = sample(true);
  r2_ble_causal_observe(&r);
  assert(!causal.meta.losses && !causal.meta.recoveries &&
         R2_BLE_KIND(causal.recovery) == R2_BLE_ACQUIRED);
  /* Raw state flags while readable are not an availability loss. */
  r.why = R2_FOLLOW_FIT;
  r2_ble_causal_observe(&r);
  assert(!causal.meta.losses);
  r.flags &= ~R2_BLE_AVAILABLE;
  r.flags |= 0xffff0000u;
  r.why = R2_FOLLOW_DATA_EXPIRED | R2_FOLLOW_PT_UTC;
  r.tick = 36000000;
  r2_ble_causal_observe(&r);
  assert(causal.meta.losses == 1 && R2_BLE_PRIMARY(causal.onset) == R2_BLE_P_DATA);
  struct r2_ble_causal_record onset = causal.onset;
  struct r2_ble_causal_first first = causal.first;
  for (unsigned i = 0; i < 1000; i++) {
    ++r.commit_us;
    r2_ble_causal_observe(&r);
  }
  assert(causal.meta.losses == 1 && causal.meta.repeats >= 1000 &&
         !memcmp(&onset, &causal.onset, sizeof onset));
  r.tick = 41000000;
  r.why |= R2_FOLLOW_PHYSICAL_EXPIRED;
  r2_ble_causal_observe(&r);
  assert(!memcmp(&onset, &causal.onset, sizeof onset) && causal.meta.cause_changes);
  /* Both bounds crossed after delay: earlier comparable HF bound wins. */
  assert(primary(&r) == R2_BLE_P_DATA);
  r.physical_until = 34000000;
  assert(primary(&r) == R2_BLE_P_PHYSICAL);
  r.why |= R2_FOLLOW_BINDING;
  assert(primary(&r) == R2_BLE_P_MULTIPLE);
  r.why = R2_FOLLOW_BINDING;
  assert(primary(&r) == R2_BLE_P_CONTROL_PERMISSION);
  r.flags |= R2_BLE_RETIRED;
  assert(primary(&r) == R2_BLE_P_CONTROL);
  r.flags = R2_BLE_FOLLOWING | R2_BLE_MANAGED;
  r.why = R2_FOLLOW_CAPTURE;
  assert(primary(&r) == R2_BLE_P_CAPTURE);
  r = sample(true);
  r2_ble_causal_observe(&r);
  assert(causal.meta.recoveries == 1 && !memcmp(&onset, &causal.onset, sizeof onset));
  for (unsigned i = 0; i < 29 * 5; i++)
    r2_timing_event(R2_TIMING_GM_CONTROL, 0, 0, 0, 0, 0);
  assert(!memcmp(&onset, &causal.onset, sizeof onset) &&
         !memcmp(&first, &causal.first, sizeof first));
  /* Later episodes rotate onset, never immutable first-loss summary. */
  for (unsigned i = 0; i < 100; i++) {
    r.flags &= ~R2_BLE_AVAILABLE;
    r.why = R2_FOLLOW_PHYSICAL_EXPIRED;
    r2_ble_causal_observe(&r);
    r.flags |= R2_BLE_AVAILABLE;
    r.why = 0;
    r2_ble_causal_observe(&r);
  }
  assert(causal.meta.losses == 101 && !memcmp(&first, &causal.first, sizeof first) &&
         causal.meta.coalesced);
  /* Main-only token budget: burst2, then one attempt/5s; backpressure never
   * destroys sticky records and no healthy repeat polls produce notes. */
  fake_ms = 10000;
  blocked = true;
  r2_ble_causal_poll(7, (uint64_t)fake_ms);
  r2_ble_causal_poll(7, (uint64_t)fake_ms);
  unsigned attempts = causal.meta.journal_attempts;
  r2_ble_causal_poll(7, (uint64_t)fake_ms);
  assert(causal.meta.journal_attempts == attempts && !notes);
  fake_ms += 4999;
  r2_ble_causal_poll(7, (uint64_t)fake_ms);
  assert(causal.meta.journal_attempts == attempts);
  fake_ms++;
  blocked = false;
  r2_ble_causal_poll(7, (uint64_t)fake_ms);
  assert(notes == 1 && strstr(last_note, "detected_us") && strstr(last_note, "emitted_ms"));
  assert(!memcmp(&first, &causal.first, sizeof first));
  /* Full-width actual formatter and original shared-buffer CRC framing. */
  memset(&causal.onset, 0xff, sizeof causal.onset);
  causal.onset.flags = (1u << 16) - 1;
  causal.onset.flags &=
      ~(R2_BLE_AVAILABLE | R2_BLE_BOUND | R2_BLE_SCOPE_EQUAL | R2_BLE_HEADER_MATCHES);
  causal.onset.flags |= R2_BLE_P_CONTROL << R2_BLE_PRIMARY_SHIFT | R2_BLE_CONTEXT_CHANGE
                                                                       << R2_BLE_KIND_SHIFT;
  causal.onset.why = (1u << 21) - 1;
  causal.onset.model_source_ns = causal.onset.header_offset = INT64_MIN;
  char body[2976], frame[3072];
  int n = r2_ble_causal_json(body, sizeof body, UINT64_MAX, 11);
  fprintf(stderr, "EVENT_MAX_JSON %d\n", n);
  assert(n > 0 && n < 2976);
  printf("EVENT_MAX_PAYLOAD %s\n", body);
  int f = r2_frame(frame, sizeof frame, UINT32_MAX, UINT32_MAX, body);
  assert(f > n && f < 3072);
  printf("EVENT_MAX_FRAME %d %s\n", f, frame);
  assert(r2_ble_causal_json(body, 8, 1, 11) == -ENOSPC);
  /* Unknown origin/capture values remain null, never inferred expiry. */
  causal.onset = sample(false);
  causal.onset.sequence = 1;
  n = r2_ble_causal_json(body, sizeof body, 1, 11);
  assert(n > 0 && strstr(body, "\"header_expires_source_ns\":null") &&
         strstr(body, "\"matched_expires_source_ns\":null"));
  memset(&causal, 0, sizeof causal);
  r = sample(true);
  r2_ble_causal_observe(&r);
  causal.meta.observations = causal.meta.repeats = UINT32_MAX - 1;
  r2_ble_causal_observe(&r);
  r2_ble_causal_observe(&r);
  assert(causal.meta.observations == UINT32_MAX && causal.meta.repeats == UINT32_MAX);
  causal.meta.serial = UINT32_MAX;
  onset = causal.onset;
  r.why = R2_FOLLOW_DATA_EXPIRED;
  r.flags &= ~R2_BLE_AVAILABLE;
  r2_ble_causal_observe(&r);
  assert(!memcmp(&onset, &causal.onset, sizeof onset));
  fake_ms = -1;
  blocked = true;
  unsigned note_before = note_attempts;
  causal.meta.flags &= ~(DRAIN_INIT | T_MASK);
  causal.meta.flags |= 1u << (N_SHIFT + 2);
  for (unsigned i = 0; i < 100; i++)
    r2_ble_causal_poll(1, UINT64_MAX);
  assert(note_attempts - note_before <= 2);
  puts("PASS actual causal recorder: "
       "event-once/repeats/rawflags/recovery/onset/firststicky/journaloverflow/coalescing/rate/"
       "backpressure/unknown/framing/exhaustion");
}
