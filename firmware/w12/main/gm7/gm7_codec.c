#define GM7_CODEC_IMPLEMENTATION
/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
// Receiver control subset; ROLE/GNSS remain outside this routing consumer.
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

size_t gm7_tz_wire_size(uint8_t k) {
 size_t n=gm7_wire_size(k);return n&&k<=GM7_WITHDRAW?n+4:n;
}
int gm7_tz_encode(uint8_t *w,size_t capacity,const struct gm7_packet *p,const struct r2_group_config *g) {
 if(!w||!p||!r2_group_valid(g)||p->group!=g->id||p->key_epoch!=g->epoch||gm7_validate(p))return -EINVAL;
 size_t n=gm7_tz_wire_size(p->kind);if(capacity<n)return -EMSGSIZE;
 memset(w,0,n);memcpy(w,"G7V4",4);w[4]=4;w[5]=p->kind;put(w+6,n,2);
 put(w+8,p->flags,2);put(w+12,p->group,8);put(w+20,p->key_epoch,4);encode_body(w,p);
 if(p->kind<=GM7_WITHDRAW)r2_tz_put(w+162,p->body.control.tz);
 return r2_group_mac(w+n-16,g,"PTOBTZ04",w,n-16);
}
int gm7_tz_decode(struct gm7_packet *out,const uint8_t *w,size_t n,const struct r2_group_config *g) {
 if(!out||!w||!r2_group_valid(g)||n<40||memcmp(w,"G7V4",4)||w[4]!=4||
    !gm7_tz_wire_size(w[5])||n!=gm7_tz_wire_size(w[5])||get(w+6,2)!=n||get(w+10,2)||
    get(w+12,8)!=g->id||get(w+20,4)!=g->epoch)return -EINVAL;
 if(!r2_group_verify(w+n-16,g,"PTOBTZ04",w,n-16))return -EACCES;
 struct gm7_packet p={.kind=w[5],.flags=(uint16_t)get(w+8,2),.group=g->id,.key_epoch=g->epoch};
 decode_body(&p,w);if(p.kind<=GM7_WITHDRAW)p.body.control.tz=r2_tz_get(w+162);
 int rc=gm7_validate(&p);if(!rc)*out=p;return rc;
}
int gm7_tz_ad_encode(uint8_t *w,size_t capacity,const struct gm7_packet *p,const struct r2_group_config *g) {
 size_t n=p?gm7_tz_wire_size(p->kind):0;if(!w||!n||capacity<n+4)return -EMSGSIZE;
 int rc=gm7_tz_encode(w+4,capacity-4,p,g);if(rc)return rc;
 w[0]=(uint8_t)(n+3);w[1]=w[2]=w[3]=255;return 0;
}
int gm7_tz_ad_decode(struct gm7_packet *p,const uint8_t *w,size_t n,const struct r2_group_config *g) {
 if(!w||n<4||n>GM7_TZ_MAX_AD_BYTES||w[0]!=n-1||w[1]!=255||w[2]!=255||w[3]!=255)return -EINVAL;
 return gm7_tz_decode(p,w+4,n-4,g);
}
