/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define RC_SLOT_STARTUP_SCAN_US UINT64_C(3000000)
#define RC_SLOT_STARTUP_LIMIT_US UINT64_C(6000000)
#define RC_SLOT_SCAN_US UINT64_C(1000000)
#define RC_SLOT_SCAN_LIMIT_US UINT64_C(2500000)
#define RC_SLOT_RETRY_US UINT64_C(10000000)
#define RC_SLOT_COOLDOWN_US UINT64_C(300000000)
#define RC_SLOT_CONFLICT_RSSI (-75)
/* Owner-thread only. Discovery is an avoidance hint, never time authority. */
struct rc_slots {
  uint64_t seen[3], previous_seen[3], scan_until, scan_limit, scan_sample,
      scan_owned_us, scan_owned_total_us, retry_at, changed_at;
  int16_t rssi[3];
  uint8_t hits[3], slot, old_slot, target, parent_slot, max_slot,
      preferred_max_slot, conflict_hits;
  uint32_t scans, verified, scan_failures, changes, rollbacks, busy, scan_required_us;
  bool scanning, listening, rescan, moving, restoring, available;
};
void rc_slots_init(struct rc_slots *, uint8_t slot, uint8_t parent);
void rc_slots_seen(struct rc_slots *, uint8_t slot, int8_t rssi, uint64_t now);
/* Caller requires a different, lower identity, matching root scope, same slot
 * and RSSI >= RC_SLOT_CONFLICT_RSSI. Discovery remains an avoidance hint. */
void rc_slots_contender(struct rc_slots *, uint64_t identity, uint64_t now);
bool rc_slots_begin(struct rc_slots *, uint64_t now, bool rescan,
                    uint32_t jitter);
bool rc_slots_begin_startup_check(struct rc_slots *, uint64_t now, uint32_t jitter);
void rc_slots_listen(struct rc_slots *, uint64_t now, bool owned);
bool rc_slots_finish(struct rc_slots *, uint64_t now, uint32_t random);
void rc_slots_commit(struct rc_slots *, uint64_t now);
void rc_slots_rollback(struct rc_slots *, uint64_t now);
