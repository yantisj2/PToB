/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Descriptive only. No member can be consumed as clock/source permission. */
enum r2_ble_causal_flag {
  R2_BLE_AVAILABLE = 1u << 0,
  R2_BLE_FOLLOWING = 1u << 1,
  R2_BLE_CAPTURED = 1u << 2,
  R2_BLE_MANAGED = 1u << 3,
  R2_BLE_STOPPED = 1u << 4,
  R2_BLE_BOUND = 1u << 5,
  R2_BLE_SCOPE_EQUAL = 1u << 6,
  R2_BLE_HEADER_KNOWN = 1u << 7,
  R2_BLE_MATCHED_KNOWN = 1u << 8,
  R2_BLE_MODEL_KNOWN = 1u << 9,
  R2_BLE_REASON_KNOWN = 1u << 10,
  R2_BLE_RETIRED = 1u << 11,
  R2_BLE_SAMPLE_KNOWN = 1u << 12,
  R2_BLE_OUTPUT_VALID = 1u << 13,
  R2_BLE_SERVO_FAULT = 1u << 14,
  R2_BLE_HEADER_MATCHES = 1u << 15
};
#define R2_BLE_PRIMARY_SHIFT 16u
#define R2_BLE_KIND_SHIFT 24u
#define R2_BLE_PRIMARY(r) (((r).flags >> R2_BLE_PRIMARY_SHIFT) & 255u)
#define R2_BLE_KIND(r) (((r).flags >> R2_BLE_KIND_SHIFT) & 255u)
enum r2_ble_causal_kind {
  R2_BLE_ACQUIRING = 1,
  R2_BLE_ACQUIRED,
  R2_BLE_LOSS,
  R2_BLE_CAUSE_CHANGE,
  R2_BLE_CONTEXT_CHANGE,
  R2_BLE_RECOVERY
};
enum r2_ble_causal_primary {
  R2_BLE_P_NONE,
  R2_BLE_P_CONTROL,
  R2_BLE_P_CAPTURE,
  R2_BLE_P_PHYSICAL,
  R2_BLE_P_DATA,
  R2_BLE_P_OUTPUT,
  R2_BLE_P_FIT,
  R2_BLE_P_KERNEL,
  R2_BLE_P_UTC,
  R2_BLE_P_UNKNOWN,
  R2_BLE_P_CONTROL_PERMISSION,
  R2_BLE_P_MULTIPLE
};
#define R2_BLE_WHY_RETIRED (1u << 19)
#define R2_BLE_WHY_SCOPE_DIFFERENT (1u << 20)
struct r2_ble_causal_record {
  uint64_t sample_us, commit_us, tick, epoch, root, boot, physical_until, data_until, last_accepted;
  uint64_t grant_root, grant_boot, grant_until;
  uint64_t header_root, header_boot, header_tick, header_epoch, header_created, header_expires;
  uint64_t scope_until, effective_until, matched_created, matched_expires, publication_epoch;
  int64_t model_source_ns, header_offset;
  uint32_t source, train, capture, grant_source, grant_train, grant_capture;
  uint32_t header_source, header_train, header_capture, header_publication;
  uint32_t why, flags, sequence, incident, rms_ns, quality_bins, reason, generation, caller_pc;
};
_Static_assert(sizeof(struct r2_ble_causal_record) == 280, "Fixed descriptive record");
/* First LOSS since physical boot. Immutable even when later episodes rotate. */
struct r2_ble_causal_first {
  uint64_t root, boot, detected_us, tick, epoch;
  uint32_t source, train, capture, primary_flags, reason, caller_pc;
};
_Static_assert(sizeof(struct r2_ble_causal_first) == 64, "Fixed first-loss summary");
struct r2_ble_causal_meta {
  uint64_t last_seen_us, last_seen_tick, drain_due_ms;
  uint32_t observations, losses, recoveries, cause_changes, context_changes, repeats;
  uint32_t coalesced, journal_attempts, serial, first_sequence, flags, atomic_refreshes;
};
_Static_assert(sizeof(struct r2_ble_causal_meta) == 72, "Fixed counters/rate state");
#if defined(R2_BLE_CAUSAL_EVENTS)
void r2_ble_causal_observe(const struct r2_ble_causal_record *);
void r2_ble_causal_atomic_refresh(void);
void r2_ble_causal_poll(uint64_t physical_boot, uint64_t now_ms);
int r2_ble_causal_json(char *, size_t, uint64_t physical_boot, unsigned page);
void r2_ble_causal_snapshot(unsigned slot, struct r2_ble_causal_record *,
                            struct r2_ble_causal_first *, struct r2_ble_causal_meta *);
#else
static inline void r2_ble_causal_observe(const struct r2_ble_causal_record *r) { (void)r; }
static inline void r2_ble_causal_poll(uint64_t b, uint64_t n) {
  (void)b;
  (void)n;
}
static inline int r2_ble_causal_json(char *s, size_t n, uint64_t b, unsigned p) {
  (void)s;
  (void)n;
  (void)b;
  (void)p;
  return -1;
}
#endif
