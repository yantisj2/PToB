/* SPDX-License-Identifier: Apache-2.0 */
#include "unicast_time.h"
#include <errno.h>
#include <string.h>
static const char *domain(unsigned kind) {
  switch (kind) {
  case UC1_STATE:
    return "UCS1STAT";
  case UC1_CONTROL:
    return "UCS1CTRL";
  case UC1_PROBE:
    return "UCS1PROB";
  case UC1_SAMPLE:
    return "UCS1TIME";
  default:
    return NULL;
  }
}
static size_t length(unsigned kind) {
  return kind == UC1_CONTROL  ? UC1_CONTROL_BYTES
         : kind == UC1_PROBE  ? UC1_PROBE_BYTES
         : kind == UC1_STATE  ? UC1_STATE_BYTES
         : kind == UC1_SAMPLE ? UC1_SAMPLE_BYTES
                              : 0;
}
void uc1_key(struct r2_group_config *out, const struct r2_group_config *group,
             uint64_t id) {
  uint8_t body[8], key[16];
  r2_group_put(body, id, 8);
  *out = (struct r2_group_config){.id = group->id, .epoch = group->epoch};
  if (!r2_group_mac(key, group, "UCS1KEYS", body, 8)) {
    memcpy(out->psk, key, 16);
    memcpy(out->psk + 16, key, 16);
  }
  memset(key, 0, sizeof key);
}
bool uc1_verify(const uint8_t *wire, size_t n, unsigned kind,
                const struct r2_group_config *key) {
  return wire && length(kind) == n && wire[0] == 'U' && wire[1] == 'C' &&
         wire[2] == 1 && wire[3] == kind &&
         r2_group_verify(wire + n - 16, key, domain(kind), wire, n - 16);
}
int uc1_sign(uint8_t *wire, size_t n, unsigned kind,
             const struct r2_group_config *key) {
  if (!wire || n != length(kind))
    return -EINVAL;
  wire[0] = 'U';
  wire[1] = 'C';
  wire[2] = 1;
  wire[3] = kind;
  return r2_group_mac(wire + n - 16, key, domain(kind), wire, n - 16);
}
bool uc1_live(const struct uc1_session *s, uint64_t now) {
  return s && s->hz && now < s->until;
}
int uc1_control(struct uc1_session *s, const uint8_t *w, size_t n,
                const struct r2_group_config *key, uint64_t now,
                uint64_t stream, uint16_t mtu, uint32_t interval_us) {
  if (!s || !uc1_verify(w, n, UC1_CONTROL, key) || w[7] || w[6] > 3)
    return -EINVAL;
  for (unsigned i = 48; i < 56; i++)
    if (w[i])
      return -EINVAL;
  uint64_t nonce = r2_group_get(w + 24, 8), id = r2_group_get(w + 32, 8);
  uint32_t seq = (uint32_t)r2_group_get(w + 40, 4),
           lease = (uint32_t)r2_group_get(w + 44, 4);
  if (r2_group_get(w + 8, 8) != s->boot ||
      r2_group_get(w + 16, 8) != s->challenge || !nonce || !seq)
    return -EINVAL;
  if (seq == s->control_seq)
    return !memcmp(w, s->last_control, n) ? 0 : -EINVAL;
  if (seq < s->control_seq || w[4] < UC1_ON || w[4] > UC1_OFF)
    return -EINVAL;
  if (now > UINT64_MAX - lease)
    return -EINVAL;
  if (w[4] == UC1_ON) {
    if (uc1_live(s, now) || id || !stream || !lease || lease > UC1_LEASE_US ||
        (w[5] != 1 && w[5] != 4 && w[5] != 8 && w[5] != 16))
      return -EINVAL;
    uint8_t hz = w[5];
    s->options = w[6] | (hz == 8 ? UC1_SUPPORT_8HZ : 0);
    s->reason = UC1_OK;
    /* OFF/ON and lease renewal do not reset the connection's low-water mark. */
    if (s->ceiling && hz > s->ceiling) {
      hz = s->options & UC1_ALLOW_DOWNGRADE ? s->ceiling : 0;
      s->reason = UC1_CAPACITY;
    }
    if (mtu < UC1_SAMPLE_BYTES + 3) {
      s->reason = UC1_MTU;
      hz = 0;
    } else if (hz == 16 && interval_us > 30000) {
      s->reason = UC1_CAPACITY;
      if (w[6] & UC1_ALLOW_DOWNGRADE) {
        hz = s->options & UC1_SUPPORT_8HZ ? 8 : 4;
        s->reason = UC1_CAPACITY;
      } else
        hz = 0;
    }
    if (hz == 8 && interval_us > 60000) {
      hz = w[6] & UC1_ALLOW_DOWNGRADE ? 4 : 0;
      s->reason = UC1_CAPACITY;
    }
    if (hz == 4 && interval_us > 125000) {
      if (w[6] & UC1_ALLOW_DOWNGRADE) {
        hz = 1;
        s->reason = UC1_CAPACITY;
      } else
        hz = 0;
    }
    s->nonce = nonce;
    s->stream = stream;
    s->hz = hz;
    if (hz && (!s->ceiling || hz < s->ceiling))
      s->ceiling = hz;
    s->until = hz ? now + lease : now;
    s->probe_seq = 0;
    /* Arrival debt is connection-owned, including OFF followed by ON. */
  } else {
    if (nonce != s->nonce || !id || id != s->stream)
      return -EINVAL;
    if (w[4] == UC1_RENEW) {
      if (!uc1_live(s, now) || !lease || lease > UC1_LEASE_US)
        return -EINVAL;
      s->until = now + lease;
    } else {
      s->hz = 0;
      s->until = now;
      s->reason = UC1_OK;
    }
  }
  s->control_seq = seq;
  memcpy(s->last_control, w, n);
  return 1;
}
bool uc1_pool(struct uc1_session *const sessions[UC1_CLIENTS], uint64_t now) {
  uint8_t rates[UC1_CLIENTS];
  unsigned total = 0;
  for (unsigned i = 0; i < UC1_CLIENTS; i++) {
    rates[i] = uc1_live(sessions[i], now) ? sessions[i]->hz : 0;
    total += rates[i];
  }
  while (total > UC1_POOL_HZ) {
    int largest = -1;
    for (unsigned i = 0; i < UC1_CLIENTS; i++)
      if (rates[i] > 4 && (sessions[i]->options & UC1_ALLOW_DOWNGRADE) &&
          (largest < 0 || rates[i] >= rates[largest]))
        largest = (int)i;
    if (largest < 0)
      return false; /* Transactional refusal: existing sessions unchanged. */
    unsigned next =
        rates[largest] == 16 && (sessions[largest]->options & UC1_SUPPORT_8HZ)
            ? 8
            : 4;
    total -= rates[largest] - next;
    rates[largest] = next;
  }
  for (unsigned i = 0; i < UC1_CLIENTS; i++)
    if (rates[i] && rates[i] < sessions[i]->hz) {
      sessions[i]->hz = sessions[i]->ceiling = rates[i];
      sessions[i]->reason = UC1_CAPACITY;
      /* Preserve arrival debt across downgrades and renewal. */
    }
  return true;
}
bool uc1_credit(uint64_t *next, uint64_t now, unsigned hz, unsigned burst) {
  if (!next || !hz || !burst)
    return false;
  uint64_t period = UINT64_C(1000000) / hz, slack = period * (burst - 1u);
  if (now > UINT64_MAX - slack || now + slack < *next)
    return false;
  uint64_t base = now > *next ? now : *next;
  if (base > UINT64_MAX - period)
    return false;
  *next = base + period;
  return true;
}
bool uc1_probe(struct uc1_session *s, const uint8_t *w, size_t n,
               const struct r2_group_config *key, uint64_t now) {
  if (!s || !uc1_live(s, now) || !uc1_verify(w, n, UC1_PROBE, key) ||
      r2_group_get(w + 8, 8) != s->boot ||
      r2_group_get(w + 16, 8) != s->stream ||
      r2_group_get(w + 24, 8) != s->nonce)
    return false;
  for (unsigned i = 36; i < 48; i++)
    if (w[i])
      return false;
  uint32_t seq = (uint32_t)r2_group_get(w + 4, 4),
           age = (uint32_t)r2_group_get(w + 32, 4);
  if (!seq || seq <= s->probe_seq || !age || age > 2000000 ||
      now < s->last_probe_at)
    return false;
  /* Bounded two-token arrival budget, replenished only by elapsed time.
   * BLE can compress correctly paced client requests at callback entry. One
   * period of slack absorbs that jitter without creating a server timer or
   * queue; a third immediate request is refused. Renewal preserves the debt. */
  if (!uc1_credit(&s->rate_next_at, now, s->hz, 2))
    return false;
  s->probe_seq = seq;
  s->last_probe_at = now;
  return true;
}
