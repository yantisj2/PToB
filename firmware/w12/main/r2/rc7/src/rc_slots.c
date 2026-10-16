/* SPDX-License-Identifier: Apache-2.0 */
#include "rc_slots.h"
#include <string.h>
void rc_slots_init(struct rc_slots *s, uint8_t slot, uint8_t parent) {
  memset(s, 0, sizeof *s);
  s->slot = s->old_slot = s->target = slot;
  s->parent_slot = parent;
  s->max_slot = s->preferred_max_slot = 3;
  for (unsigned i = 0; i < 3; i++)
    s->rssi[i] = -127;
}
void rc_slots_seen(struct rc_slots *s, uint8_t slot, int8_t rssi,
                   uint64_t now) {
  if (!s || !s->scanning || slot < 1 || slot > 3 || rssi == 127)
    return;
  unsigned i = slot - 1;
  s->seen[i] = now;
  if (s->hits[i] != 255)
    s->hits[i]++;
  if (rssi > s->rssi[i])
    s->rssi[i] = rssi;
}
void rc_slots_contender(struct rc_slots *s, uint64_t identity, uint64_t now) {
  if (!s || !s->scanning || !identity)
    return;
  /* Reuse the old unused history cells: contender identity and last receipt. */
  if (s->previous_seen[0] != identity) {
    s->previous_seen[0] = identity;
    s->previous_seen[1] = now;
    s->conflict_hits = 1;
  } else if (now > s->previous_seen[1]) {
    s->previous_seen[1] = now;
    if (s->conflict_hits < 2)
      ++s->conflict_hits;
  }
}
static bool begin(struct rc_slots *s, uint64_t now, bool rescan,
                  uint32_t jitter, bool startup_check) {
  bool startup = !rescan || startup_check;
  uint64_t limit = startup ? RC_SLOT_STARTUP_LIMIT_US : RC_SLOT_SCAN_LIMIT_US;
  if (!s || s->scanning || s->moving || (!startup_check && now < s->retry_at) ||
      (rescan && !startup_check && s->changed_at && now - s->changed_at < RC_SLOT_COOLDOWN_US) ||
      now > UINT64_MAX - limit)
    return false;
  s->old_slot = s->slot;
  s->target = s->slot;
  s->rescan = rescan;
  s->scanning = true;
  s->restoring = false;
  memset(s->hits, 0, sizeof s->hits);
  memset(s->seen, 0, sizeof s->seen);
  memset(s->previous_seen, 0, sizeof s->previous_seen);
  s->conflict_hits = 0;
  for (unsigned i = 0; i < 3; i++)
    s->rssi[i] = -127;
  s->available = false;
  s->listening = false;
  s->scan_sample = now;
  s->scan_owned_us = 0;
  s->scan_required_us = startup ? RC_SLOT_STARTUP_SCAN_US : RC_SLOT_SCAN_US;
  s->scan_limit = now + limit;
  s->scan_until = now + s->scan_required_us + jitter % (startup ? 2000001 : 500001);
  ++s->scans;
  return true;
}
bool rc_slots_begin(struct rc_slots *s, uint64_t now, bool rescan, uint32_t jitter) {
  return begin(s, now, rescan, jitter, false);
}
bool rc_slots_begin_startup_check(struct rc_slots *s, uint64_t now, uint32_t jitter) {
  /* The service owner consumes this once, after its first real publication. */
  return begin(s, now, true, jitter, true);
}
void rc_slots_listen(struct rc_slots *s, uint64_t now, bool owned) {
  if (!s || !s->scanning)
    return;
  /* Credit only intervals whose two endpoints have the same passive owner. */
  if (owned && s->listening && now >= s->scan_sample) {
    uint64_t elapsed = now - s->scan_sample;
    s->scan_owned_us += elapsed;
    s->scan_owned_total_us += elapsed;
  }
  s->scan_sample = now;
  s->listening = owned;
}
bool rc_slots_finish(struct rc_slots *s, uint64_t now, uint32_t random) {
  if (!s || !s->scanning || now < s->scan_until)
    return false;
  if (s->scan_owned_us < s->scan_required_us) {
    if (now < s->scan_limit)
      return false;
    ++s->scan_failures;
    rc_slots_rollback(s, now);
    return true;
  }
  ++s->verified;
  s->scanning = false;
  if (s->rescan) {
    /* A request authorizes inspection, never blind rotation. Rate-limit even
     * an inconclusive scan so old clients cannot keep reopening discovery. */
    s->retry_at = now + RC_SLOT_COOLDOWN_US;
    if (s->conflict_hits < 2) {
      s->available = true;
      return true;
    }
  }
  uint8_t free[3], count = 0;
  /* Prefer slots leaving extension headroom. When those are occupied, a
   * free terminal slot can still serve receiver-only downstream clients. */
  for (unsigned tier = 0; tier < 2 && !count; tier++)
    for (uint8_t slot = s->parent_slot + 1; slot <= s->max_slot; slot++)
      if ((tier == 0 ? slot <= s->preferred_max_slot
                     : slot > s->preferred_max_slot) &&
          s->hits[slot - 1] < 2 && (!s->rescan || slot != s->old_slot))
        free[count++] = slot;
  if (!count) {
    ++s->busy;
    s->target = s->old_slot;
    s->retry_at = now + (s->rescan ? RC_SLOT_COOLDOWN_US : RC_SLOT_RETRY_US);
    return true;
  }
  s->available = true;
  s->target = free[random % count];
  s->moving = s->target != s->slot;
  return true;
}
void rc_slots_commit(struct rc_slots *s, uint64_t now) {
  if (!s)
    return;
  if (s->target != s->slot) {
    s->slot = s->target;
    ++s->changes;
  }
  s->moving = s->restoring = s->rescan = false;
  s->changed_at = now;
}
void rc_slots_rollback(struct rc_slots *s, uint64_t now) {
  if (!s)
    return;
  bool rescan = s->rescan;
  s->target = s->slot = s->old_slot;
  s->scanning = s->moving = s->rescan = false;
  s->available = false;
  s->restoring = true;
  s->retry_at = now + (rescan ? RC_SLOT_COOLDOWN_US : RC_SLOT_RETRY_US);
  ++s->rollbacks;
}
