/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_codec.h"
#include <errno.h>
#include <string.h>
#include <tinycrypt/hmac.h>
static uint64_t get(const uint8_t *p, unsigned n) {
  uint64_t v = 0;
  while (n--)
    v = (v << 8) | *p++;
  return v;
}
static void put(uint8_t *p, uint64_t v, unsigned n) {
  while (n) {
    p[--n] = (uint8_t)v;
    v >>= 8;
  }
}
bool gm7_utc_valid(uint64_t v) { return v >= GM7_UTC_MIN && v < GM7_UTC_MAX; }
size_t gm7_wire_size(uint8_t k) {
  switch (k) {
  case GM7_ANNOUNCE:
  case GM7_STATUS:
  case GM7_WITHDRAW:
    return GM7_CONTROL_BYTES;
  case GM7_ROLE:
    return GM7_ROLE_BYTES;
  case GM7_GNSS:
    return GM7_GNSS_BYTES;
  default:
    return 0;
  }
}
static int tag(uint8_t out[16], const struct r2_group_config *g,
               const uint8_t *p, size_t n) {
  static const uint8_t domain[] = "PToB/GM7/implementation3";
  uint8_t full[32];
  struct tc_hmac_state_struct h;
  if (!r2_group_valid(g) || !tc_hmac_set_key(&h, g->psk, 32) ||
      !tc_hmac_init(&h) || !tc_hmac_update(&h, domain, sizeof domain) ||
      !tc_hmac_update(&h, p, (unsigned)n) ||
      !tc_hmac_final(full, sizeof full, &h))
    return -EIO;
  memcpy(out, full, 16);
  memset(full, 0, sizeof full);
  return 0;
}
static bool lineage(uint64_t birth, uint64_t founder, uint64_t term,
                    uint64_t active, uint64_t boot) {
  bool any = birth || founder || term || active || boot;
  return !any || (gm7_utc_valid(birth) && founder && term && active && boot);
}
int gm7_validate(const struct gm7_packet *p) {
  if (!p || !p->group || !p->key_epoch || !gm7_wire_size(p->kind))
    return -EINVAL;
  unsigned f = p->flags;
  if (p->kind <= GM7_WITHDRAW) {
    const struct gm7_control *c = &p->body.control;
    uint32_t max = p->kind == GM7_ANNOUNCE ? 2750000
                   : p->kind == GM7_STATUS ? 4500000
                                           : 1000000;
    if ((f & ~31u) || (p->kind != GM7_STATUS && (f & GM7_CAPABLE)) ||
        !c->sender || !c->boot || !c->sequence || !c->max_age_us ||
        c->max_age_us > max ||
        !lineage(c->birth_us, c->founder, c->term, c->active, c->active_boot) ||
        (c->sent_utc_us && !gm7_utc_valid(c->sent_utc_us)) ||
        ((f & GM7_ELIGIBLE) && (!c->source_gen || !c->capture_gen)) ||
        !!c->hot != !!c->hot_boot)
      return -EINVAL;
    if (p->kind != GM7_STATUS &&
        (!c->birth_us || c->sender != c->active || c->boot != c->active_boot ||
         !c->timing_epoch || !c->train_gen || c->shadow_gen || c->hot == c->active))
      return -EINVAL;
    if (p->kind != GM7_ANNOUNCE && c->source_valid_us)
      return -EINVAL;
    if (p->kind == GM7_ANNOUNCE) {
      if (!c->source_valid_us || c->source_valid_us > 1500000 ||
          !(f & GM7_ELIGIBLE) || (f & (GM7_SHADOW | GM7_HOT)) ||
          !c->sent_utc_us || c->birth_us > c->sent_utc_us ||
          !!(f & GM7_YIELD) != !!c->yield_utc_us)
        return -EINVAL;
      if ((f & GM7_YIELD) && (!c->hot || c->sent_utc_us > UINT64_MAX - 750000 ||
                              c->yield_utc_us != c->sent_utc_us + 750000))
        return -EINVAL;
    } else if (p->kind == GM7_WITHDRAW) {
      if (f || c->yield_utc_us)
        return -EINVAL;
    } else {
      if ((f & GM7_YIELD) || c->yield_utc_us ||
          ((f & GM7_ELIGIBLE) && !(f & GM7_CAPABLE)) ||
          !!(f & GM7_SHADOW) != !!c->shadow_gen ||
          !!(f & GM7_SHADOW) != !!c->train_gen)
        return -EINVAL;
      if ((f & GM7_SHADOW) && (!c->birth_us || c->sender == c->active))
        return -EINVAL;
      if (f & GM7_HOT) {
        if ((f & 19u) != 19u || !(f & GM7_SHADOW) || c->hot != c->sender ||
            c->hot_boot != c->boot)
          return -EINVAL;
      } else if (c->hot)
        return -EINVAL;
    }
  } else {
    const struct gm7_role *r = &p->body.role;
    const struct gm7_gnss *g = &p->body.gnss;
    /* Common diagnostic prefix is read from its typed member, never union
     * layout. */
    uint64_t sender = p->kind == GM7_ROLE ? r->sender : g->sender,
             boot = p->kind == GM7_ROLE ? r->boot : g->boot,
             sequence = p->kind == GM7_ROLE ? r->sequence : g->sequence;
    uint64_t birth = p->kind == GM7_ROLE ? r->birth_us : g->birth_us,
             founder = p->kind == GM7_ROLE ? r->founder : g->founder,
             term = p->kind == GM7_ROLE ? r->term : g->term,
             active = p->kind == GM7_ROLE ? r->active : g->active,
             aboot = p->kind == GM7_ROLE ? r->active_boot : g->active_boot;
    uint32_t valid = p->kind == GM7_ROLE ? r->valid_us : g->valid_us;
    if (f || !sender || !boot || !sequence || !valid ||
        valid > (p->kind == GM7_ROLE ? 4500000u : 30000000u) ||
        !lineage(birth, founder, term, active, aboot))
      return -EINVAL;
    if (p->kind == GM7_ROLE) {
      if ((r->blockers & ~8191u) || r->role > 4 || (r->known & ~7u) ||
          !!r->hot != !!r->hot_boot)
        return -EINVAL;
      uint32_t d[3] = {r->hot_duration_ms, r->active_duration_ms,
                       r->pps_duration_ms};
      for (unsigned i = 0; i < 3; i++)
        if (!!(r->known & (1u << i)) == (d[i] == UINT32_MAX))
          return -EINVAL;
    } else {
      if (g->reserved || (g->known & ~127u) || (g->values & ~7u) ||
          (g->values & ~g->known) ||
          ((g->values & 1) && g->rmc_age_ms >= 2500) ||
          ((g->values & 2) && g->pps_age_ms >= 1500) ||
          ((g->values & 4) && (g->values & 3) != 3) ||
          ((g->known & 40) && g->gga_age_ms >= 4000) ||
          !!(g->known & 64) == (g->utc_error_us == UINT32_MAX))
        return -EINVAL;
      if (g->known & 8) {
        if (g->sats > 99)
          return -EINVAL;
      } else if (g->sats != 255)
        return -EINVAL;
      if (!(g->known & 32) && g->hdop_centi != 65535)
        return -EINVAL;
    }
  }
  return 0;
}

static void encode_body(uint8_t *w, const struct gm7_packet *p) {
  switch (p->kind) {
  case 1:
  case 2:
  case 3:
    put(w + 24, p->body.control.sender, 8);
    put(w + 32, p->body.control.boot, 8);
    put(w + 40, p->body.control.sequence, 8);
    put(w + 48, p->body.control.sent_utc_us, 8);
    put(w + 56, p->body.control.max_age_us, 4);
    put(w + 60, p->body.control.source_valid_us, 4);
    put(w + 64, p->body.control.source_gen, 4);
    put(w + 68, p->body.control.capture_gen, 4);
    put(w + 72, p->body.control.train_gen, 4);
    put(w + 76, p->body.control.priority1, 1);
    put(w + 77, p->body.control.clock_class, 1);
    put(w + 78, p->body.control.accuracy, 1);
    put(w + 79, p->body.control.priority2, 1);
    put(w + 80, p->body.control.variance, 2);
    put(w + 82, p->body.control.birth_us, 8);
    put(w + 90, p->body.control.founder, 8);
    put(w + 98, p->body.control.term, 8);
    put(w + 106, p->body.control.active, 8);
    put(w + 114, p->body.control.active_boot, 8);
    put(w + 122, p->body.control.hot, 8);
    put(w + 130, p->body.control.hot_boot, 8);
    put(w + 138, p->body.control.yield_utc_us, 8);
    put(w + 146, p->body.control.shadow_gen, 4);
    put(w + 150, p->body.control.sent_mono_us, 8);
    put(w + 158, p->body.control.timing_epoch, 4);
    break;
  case 6:
    put(w + 24, p->body.role.sender, 8);
    put(w + 32, p->body.role.boot, 8);
    put(w + 40, p->body.role.sequence, 8);
    put(w + 48, p->body.role.sampled_mono_us, 8);
    put(w + 56, p->body.role.birth_us, 8);
    put(w + 64, p->body.role.founder, 8);
    put(w + 72, p->body.role.term, 8);
    put(w + 80, p->body.role.active, 8);
    put(w + 88, p->body.role.active_boot, 8);
    put(w + 96, p->body.role.timing_epoch, 4);
    put(w + 100, p->body.role.valid_us, 4);
    put(w + 104, p->body.role.hot, 8);
    put(w + 112, p->body.role.hot_boot, 8);
    put(w + 120, p->body.role.source_gen, 4);
    put(w + 124, p->body.role.capture_gen, 4);
    put(w + 128, p->body.role.train_gen, 4);
    put(w + 132, p->body.role.shadow_gen, 4);
    put(w + 136, p->body.role.role, 1);
    put(w + 137, p->body.role.known, 1);
    put(w + 138, p->body.role.blockers, 2);
    put(w + 140, p->body.role.hot_duration_ms, 4);
    put(w + 144, p->body.role.active_duration_ms, 4);
    put(w + 148, p->body.role.pps_duration_ms, 4);
    break;
  case 7:
    put(w + 24, p->body.gnss.sender, 8);
    put(w + 32, p->body.gnss.boot, 8);
    put(w + 40, p->body.gnss.sequence, 8);
    put(w + 48, p->body.gnss.sampled_mono_us, 8);
    put(w + 56, p->body.gnss.birth_us, 8);
    put(w + 64, p->body.gnss.founder, 8);
    put(w + 72, p->body.gnss.term, 8);
    put(w + 80, p->body.gnss.active, 8);
    put(w + 88, p->body.gnss.active_boot, 8);
    put(w + 96, p->body.gnss.timing_epoch, 4);
    put(w + 100, p->body.gnss.valid_us, 4);
    put(w + 104, p->body.gnss.source_gen, 4);
    put(w + 108, p->body.gnss.capture_gen, 4);
    put(w + 112, p->body.gnss.train_gen, 4);
    put(w + 116, p->body.gnss.known, 2);
    put(w + 118, p->body.gnss.values, 2);
    put(w + 120, p->body.gnss.sats, 1);
    put(w + 121, p->body.gnss.clock_class, 1);
    put(w + 122, p->body.gnss.accuracy, 1);
    put(w + 123, p->body.gnss.priority1, 1);
    put(w + 124, p->body.gnss.priority2, 1);
    put(w + 125, p->body.gnss.reserved, 1);
    put(w + 126, p->body.gnss.variance, 2);
    put(w + 128, p->body.gnss.hdop_centi, 2);
    put(w + 130, p->body.gnss.rmc_age_ms, 2);
    put(w + 132, p->body.gnss.pps_age_ms, 2);
    put(w + 134, p->body.gnss.gga_age_ms, 2);
    put(w + 136, p->body.gnss.utc_error_us, 4);
    break;
  default:
    break;
  }
}

static void decode_body(struct gm7_packet *p, const uint8_t *w) {
  switch (p->kind) {
  case 1:
  case 2:
  case 3:
    p->body.control.sender = get(w + 24, 8);
    p->body.control.boot = get(w + 32, 8);
    p->body.control.sequence = get(w + 40, 8);
    p->body.control.sent_utc_us = get(w + 48, 8);
    p->body.control.max_age_us = get(w + 56, 4);
    p->body.control.source_valid_us = get(w + 60, 4);
    p->body.control.source_gen = get(w + 64, 4);
    p->body.control.capture_gen = get(w + 68, 4);
    p->body.control.train_gen = get(w + 72, 4);
    p->body.control.priority1 = get(w + 76, 1);
    p->body.control.clock_class = get(w + 77, 1);
    p->body.control.accuracy = get(w + 78, 1);
    p->body.control.priority2 = get(w + 79, 1);
    p->body.control.variance = get(w + 80, 2);
    p->body.control.birth_us = get(w + 82, 8);
    p->body.control.founder = get(w + 90, 8);
    p->body.control.term = get(w + 98, 8);
    p->body.control.active = get(w + 106, 8);
    p->body.control.active_boot = get(w + 114, 8);
    p->body.control.hot = get(w + 122, 8);
    p->body.control.hot_boot = get(w + 130, 8);
    p->body.control.yield_utc_us = get(w + 138, 8);
    p->body.control.shadow_gen = get(w + 146, 4);
    p->body.control.sent_mono_us = get(w + 150, 8);
    p->body.control.timing_epoch = (uint32_t)get(w + 158, 4);
    break;
  case 6:
    p->body.role.sender = get(w + 24, 8);
    p->body.role.boot = get(w + 32, 8);
    p->body.role.sequence = get(w + 40, 8);
    p->body.role.sampled_mono_us = get(w + 48, 8);
    p->body.role.birth_us = get(w + 56, 8);
    p->body.role.founder = get(w + 64, 8);
    p->body.role.term = get(w + 72, 8);
    p->body.role.active = get(w + 80, 8);
    p->body.role.active_boot = get(w + 88, 8);
    p->body.role.timing_epoch = get(w + 96, 4);
    p->body.role.valid_us = get(w + 100, 4);
    p->body.role.hot = get(w + 104, 8);
    p->body.role.hot_boot = get(w + 112, 8);
    p->body.role.source_gen = get(w + 120, 4);
    p->body.role.capture_gen = get(w + 124, 4);
    p->body.role.train_gen = get(w + 128, 4);
    p->body.role.shadow_gen = get(w + 132, 4);
    p->body.role.role = get(w + 136, 1);
    p->body.role.known = get(w + 137, 1);
    p->body.role.blockers = get(w + 138, 2);
    p->body.role.hot_duration_ms = get(w + 140, 4);
    p->body.role.active_duration_ms = get(w + 144, 4);
    p->body.role.pps_duration_ms = get(w + 148, 4);
    break;
  case 7:
    p->body.gnss.sender = get(w + 24, 8);
    p->body.gnss.boot = get(w + 32, 8);
    p->body.gnss.sequence = get(w + 40, 8);
    p->body.gnss.sampled_mono_us = get(w + 48, 8);
    p->body.gnss.birth_us = get(w + 56, 8);
    p->body.gnss.founder = get(w + 64, 8);
    p->body.gnss.term = get(w + 72, 8);
    p->body.gnss.active = get(w + 80, 8);
    p->body.gnss.active_boot = get(w + 88, 8);
    p->body.gnss.timing_epoch = get(w + 96, 4);
    p->body.gnss.valid_us = get(w + 100, 4);
    p->body.gnss.source_gen = get(w + 104, 4);
    p->body.gnss.capture_gen = get(w + 108, 4);
    p->body.gnss.train_gen = get(w + 112, 4);
    p->body.gnss.known = get(w + 116, 2);
    p->body.gnss.values = get(w + 118, 2);
    p->body.gnss.sats = get(w + 120, 1);
    p->body.gnss.clock_class = get(w + 121, 1);
    p->body.gnss.accuracy = get(w + 122, 1);
    p->body.gnss.priority1 = get(w + 123, 1);
    p->body.gnss.priority2 = get(w + 124, 1);
    p->body.gnss.reserved = get(w + 125, 1);
    p->body.gnss.variance = get(w + 126, 2);
    p->body.gnss.hdop_centi = get(w + 128, 2);
    p->body.gnss.rmc_age_ms = get(w + 130, 2);
    p->body.gnss.pps_age_ms = get(w + 132, 2);
    p->body.gnss.gga_age_ms = get(w + 134, 2);
    p->body.gnss.utc_error_us = get(w + 136, 4);
    break;
  default:
    break;
  }
}

int gm7_encode(uint8_t *w, size_t capacity, const struct gm7_packet *p,
               const struct r2_group_config *g) {
  if (!w || !p || !r2_group_valid(g) || p->group != g->id ||
      p->key_epoch != g->epoch || gm7_validate(p))
    return -EINVAL;
  size_t n = gm7_wire_size(p->kind);
  if (capacity < n)
    return -EMSGSIZE;
  memset(w, 0, n);
  memcpy(w, "G7V3", 4);
  w[4] = GM7_REVISION;
  w[5] = p->kind;
  put(w + 6, n, 2);
  put(w + 8, p->flags, 2);
  put(w + 12, p->group, 8);
  put(w + 20, p->key_epoch, 4);
  encode_body(w, p);
  return tag(w + n - 16, g, w, n - 16);
}
int gm7_decode(struct gm7_packet *out, const uint8_t *w, size_t n,
               const struct r2_group_config *g) {
  if (!out || !w || !r2_group_valid(g) || n < 40 || memcmp(w, "G7V3", 4) ||
      w[4] != GM7_REVISION || n != gm7_wire_size(w[5]) || get(w + 6, 2) != n ||
      get(w + 10, 2) || get(w + 12, 8) != g->id || get(w + 20, 4) != g->epoch)
    return -EINVAL;
  uint8_t wanted[16], diff = 0;
  if (tag(wanted, g, w, n - 16))
    return -EIO;
  for (unsigned i = 0; i < 16; i++)
    diff |= wanted[i] ^ w[n - 16 + i];
  if (diff)
    return -EACCES;
  struct gm7_packet p = {.kind = w[5],
                         .flags = (uint16_t)get(w + 8, 2),
                         .group = g->id,
                         .key_epoch = g->epoch};
  decode_body(&p, w);
  int rc = gm7_validate(&p);
  if (!rc)
    *out = p;
  return rc;
}
int gm7_ad_encode(uint8_t *w, size_t capacity, const struct gm7_packet *p,
                  const struct r2_group_config *g) {
  size_t n = p ? gm7_wire_size(p->kind) : 0;
  if (!w || !n || capacity < n + 4)
    return -EMSGSIZE;
  int rc = gm7_encode(w + 4, capacity - 4, p, g);
  if (rc)
    return rc;
  w[0] = (uint8_t)(n + 3);
  w[1] = w[2] = w[3] = 255;
  return 0;
}
int gm7_ad_decode(struct gm7_packet *p, const uint8_t *w, size_t n,
                  const struct r2_group_config *g) {
  if (!w || n < 4 || n > GM7_MAX_AD_BYTES || w[0] != n - 1 || w[1] != 255 ||
      w[2] != 255 || w[3] != 255)
    return -EINVAL;
  return gm7_decode(p, w + 4, n - 4, g);
}
