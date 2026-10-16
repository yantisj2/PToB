/* SPDX-License-Identifier: Apache-2.0 */
#include "precision_codec.h"
#include <errno.h>
#include <string.h>
static uint64_t get_le(const uint8_t *p, unsigned n) {
  uint64_t v = 0;
  for (unsigned i = 0; i < n; i++) {
    v |= (uint64_t)p[i] << (8 * i);
  }
  return v;
}
static void put_le(uint8_t *p, uint64_t v, unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    p[i] = (uint8_t)(v >> (8 * i));
  }
}
static uint64_t rotate(uint64_t v, unsigned n) {
  return (v << n) | (v >> (64 - n));
}
static void sip_round(uint64_t v[4]) {
  v[0] += v[1];
  v[1] = rotate(v[1], 13) ^ v[0];
  v[0] = rotate(v[0], 32);
  v[2] += v[3];
  v[3] = rotate(v[3], 16) ^ v[2];
  v[0] += v[3];
  v[3] = rotate(v[3], 21) ^ v[0];
  v[2] += v[1];
  v[1] = rotate(v[1], 17) ^ v[2];
  v[2] = rotate(v[2], 32);
}
static uint64_t precision_tag(const uint8_t wire[R2_PRECISION_BODY_BYTES],
                              const uint8_t key[16]) {
  /* Separate precision domain and SipHash-2-4.
   * Fixed122B input, bounded stack; no heap, logging or key provisioning. */
  uint8_t input[8 + R2_PRECISION_BODY_BYTES] = {'P', 'T', 'O', 'B',
                                                '1', '2', 'P', '1'};
  memcpy(input + 8, wire, R2_PRECISION_BODY_BYTES);
  uint64_t k0 = get_le(key, 8), k1 = get_le(key + 8, 8);
  uint64_t v[4] = {
      UINT64_C(0x736f6d6570736575) ^ k0, UINT64_C(0x646f72616e646f6d) ^ k1,
      UINT64_C(0x6c7967656e657261) ^ k0, UINT64_C(0x7465646279746573) ^ k1};
  size_t end = sizeof(input) / 8 * 8;
  for (size_t at = 0; at < end; at += 8) {
    uint64_t m = get_le(input + at, 8);
    v[3] ^= m;
    sip_round(v);
    sip_round(v);
    v[0] ^= m;
  }
  uint64_t last = (uint64_t)sizeof(input) << 56;
  last |= get_le(input + end, sizeof(input) - end);
  v[3] ^= last;
  sip_round(v);
  sip_round(v);
  v[0] ^= last;
  v[2] ^= 255;
  for (unsigned i = 0; i < 4; i++) {
    sip_round(v);
  }
  return v[0] ^ v[1] ^ v[2] ^ v[3];
}
static int valid(const struct r2_precision_packet *p) {
  if (!p || p->root >= 4 || !p->boot || !p->source_epoch || !p->train ||
      !p->publication || !p->capture_epoch || p->flags != 1 || p->count > 2 ||
      p->created_ns >= p->expires_ns ||
      p->expires_ns - p->created_ns > 1500000000u || p->expires_ns > INT64_MAX)
    return -EINVAL;
  for (unsigned i = 0; i < 2; i++) {
    const struct r2_precise_correction *c = &p->correction[i];
    if (i >= p->count) {
      if (c->publication || c->event || c->channel || c->ticks ||
          c->generation || c->expires_ns)
        return -EINVAL;
      continue;
    }
    if (!c->publication || c->channel > 36 || !c->ticks ||
        c->ticks > UINT64_MAX / 125 || !c->generation ||
        c->expires_ns <= c->ticks * 125 / 2 || c->expires_ns > INT64_MAX)
      return -EINVAL;
  }
  return 0;
}
int r2_precision_encode(uint8_t ad[R2_PRECISION_AD_BYTES],
                        const struct r2_precision_packet *p,
                        const uint8_t keys[4][16]) {
  if (!ad || !keys || valid(p))
    return -EINVAL;
  memset(ad, 0, R2_PRECISION_AD_BYTES);
  ad[0] = 157;
  ad[1] = ad[2] = ad[3] = 255;
  uint8_t *w = ad + 4;
  w[0] = 'P';
  w[1] = '2';
  w[2] = 1;
  w[3] = p->root;
  put_le(w + 4, p->boot, 8);
  put_le(w + 12, p->source_epoch, 4);
  put_le(w + 16, p->train, 4);
  put_le(w + 20, p->publication, 4);
  put_le(w + 24, p->capture_epoch, 4);
  w[28] = p->flags;
  w[29] = p->count;
  put_le(w + 30, R2_PRECISION_INTERVAL_UNITS, 2);
  put_le(w + 32, (uint64_t)p->utc_offset_ns, 8);
  put_le(w + 40, p->created_ns, 8);
  put_le(w + 48, p->expires_ns, 8);
  put_le(w + 56, 1, 2);
  for (unsigned i = 0; i < p->count; i++) {
    uint8_t *x = w + 58 + i * 32;
    const struct r2_precise_correction *c = &p->correction[i];
    put_le(x, c->publication, 4);
    put_le(x + 4, c->event, 2);
    x[6] = c->channel;
    x[7] = 1;
    put_le(x + 8, c->ticks, 8);
    put_le(x + 16, c->generation, 8);
    put_le(x + 24, c->expires_ns, 8);
  }
  for (unsigned i = 0; i < 4; i++)
    put_le(w + R2_PRECISION_BODY_BYTES + i * 8, precision_tag(w, keys[i]), 8);
  return 0;
}
int r2_precision_decode(struct r2_precision_packet *p, const uint8_t *ad,
                        size_t size, unsigned recipient,
                        const uint8_t key[16]) {
  if (!p || !ad || !key || recipient >= 4 || size != R2_PRECISION_AD_BYTES ||
      ad[0] != 157 || ad[1] != 255 || ad[2] != 255 || ad[3] != 255)
    return -EINVAL;
  const uint8_t *w = ad + 4;
  if (w[0] != 'P' || w[1] != '2' || w[2] != 1 || get_le(w + 30, 2) != R2_PRECISION_INTERVAL_UNITS ||
      get_le(w + 56, 2) != 1)
    return -EINVAL;
  uint64_t actual = precision_tag(w, key),
           expected = get_le(w + R2_PRECISION_BODY_BYTES + recipient * 8, 8);
  if (actual != expected)
    return -EACCES;
  return r2_precision_parse_authenticated(p,ad);
}
int r2_precision_parse_authenticated(struct r2_precision_packet *p,const uint8_t *ad) {
  const uint8_t *w=ad+4;
  memset(p, 0, sizeof(*p));
  p->root = w[3];
  p->boot = get_le(w + 4, 8);
  p->source_epoch = get_le(w + 12, 4);
  p->train = get_le(w + 16, 4);
  p->publication = get_le(w + 20, 4);
  p->capture_epoch = get_le(w + 24, 4);
  p->flags = w[28];
  p->count = w[29];
  /* Preserve signed two's-complement value without implementation-defined cast.
   */
  uint64_t offset = get_le(w + 32, 8);
  p->utc_offset_ns = offset <= INT64_MAX ? (int64_t)offset
                                         : -1 - (int64_t)(UINT64_MAX - offset);
  p->created_ns = get_le(w + 40, 8);
  p->expires_ns = get_le(w + 48, 8);
  if (p->count > 2)
    return -EINVAL;
  for (unsigned i = 0; i < 2; i++) {
    const uint8_t *x = w + 58 + i * 32;
    struct r2_precise_correction *c = &p->correction[i];
    if (x[7] != (i < p->count ? 1 : 0))
      return -EINVAL;
    c->publication = get_le(x, 4);
    c->event = get_le(x + 4, 2);
    c->channel = x[6];
    c->ticks = get_le(x + 8, 8);
    c->generation = get_le(x + 16, 8);
    c->expires_ns = get_le(x + 24, 8);
  }
  return valid(p);
}
