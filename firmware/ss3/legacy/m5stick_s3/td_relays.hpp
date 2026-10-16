#pragma once
// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ss3_td_model.hpp"

namespace sticks3 {
// Display-only ranking: use the same fresh, smoothed RSSI that the row prints.
// Retain unknown RSSI at the end; never renew discovery or timing permission.
struct TdRelayList {
  const v12::TdPeer* rows[4]{};
  unsigned count = 0, total = 0;
  explicit TdRelayList(const v12::TdSnapshot& d) {
    const auto stronger = [&](const auto* a, const auto* b) {
      const bool known = a->signal.fresh(d.now_us), other = b->signal.fresh(d.now_us);
      if (known != other) return known;
      if (known && a->signal.value != b->signal.value)
        return a->signal.value > b->signal.value;
      return a->id < b->id;
    };
    for (const auto& p : d.peers) {
      if (!p.relay || !v12::TdModel::fresh(p, d.now_us)) continue;
      ++total;
      unsigned at = 0;
      while (at < count && !stronger(&p, rows[at])) ++at;
      if (at == 4) continue;
      if (count < 4) ++count;
      for (unsigned i = count - 1; i > at; --i) rows[i] = rows[i - 1];
      rows[at] = &p;
    }
  }
};
}
