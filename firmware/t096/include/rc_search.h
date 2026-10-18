/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define RC_SEARCH_COLLECT_US UINT64_C(10000000)
#define RC_SEARCH_LIMIT_US UINT64_C(60000000)
#define RC_SEARCH_LOCATOR_US UINT64_C(65000000)
/* Host-owner exposure, not measured RF airtime. Controller preemptions remain
 * unknown. Pauses cannot manufacture listening; every search has a wall cap. */
struct rc_search { uint64_t at, owned; };
static inline void rc_search_observe(struct rc_search *s, bool eligible,
                                      uint64_t now) {
  if (eligible && s->at && now >= s->at && now-s->at <= 250000) {
    uint64_t elapsed=now-s->at;
    if (s->owned < RC_SEARCH_COLLECT_US)
      s->owned += elapsed;
  }
  s->at=eligible?now:0;
}
static inline bool rc_search_ready(const struct rc_search *s, uint64_t start,
                                   uint64_t now) {
  return start && now >= start &&
      (s->owned >= RC_SEARCH_COLLECT_US || now-start >= RC_SEARCH_LIMIT_US);
}
