/* SPDX-License-Identifier: Apache-2.0
 * HMAC-SHA256 from pinned TinyCrypt. Canonical LE context and 128-bit
 * truncation. Bounded worker-only operations. No provisioning, logging or heap
 * allocation. */
#include "group_auth.h"
#include <errno.h>
#include <string.h>
#include <tinycrypt/hmac.h>
uint64_t r2_group_get(const uint8_t *p, unsigned n) {
  uint64_t v = 0;
  for (unsigned i = 0; i < n; i++)
    v |= (uint64_t)p[i] << (8 * i);
  return v;
}
void r2_group_put(uint8_t *p, uint64_t v, unsigned n) {
  for (unsigned i = 0; i < n; i++)
    p[i] = (uint8_t)(v >> (8 * i));
}
bool r2_group_valid(const struct r2_group_config *g) {
  if (!g || !g->id || !g->epoch)
    return false;
  uint8_t bits = 0;
  for (unsigned i = 0; i < 32; i++)
    bits |= g->psk[i];
  return bits != 0;
}
bool r2_group_name(const char *p, size_t n) {
  if (!p || !n || n > R2_GROUP_NAME_MAX)
    return false;
  /* UTF-8, no NUL/control/overlong/surrogate or terminal-control code points.
   */
  for (size_t i = 0; i < n;) {
    uint8_t c = (uint8_t)p[i++];
    uint32_t v;
    unsigned more;
    uint32_t minimum;
    if (c < 0x80) {
      if (c < 0x20 || c == 0x7f)
        return false;
      continue;
    }
    if (c >= 0xc2 && c <= 0xdf) {
      v = c & 31;
      more = 1;
      minimum = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      v = c & 15;
      more = 2;
      minimum = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      v = c & 7;
      more = 3;
      minimum = 0x10000;
    } else
      return false;
    if (i + more > n)
      return false;
    while (more--) {
      c = (uint8_t)p[i++];
      if ((c & 0xc0) != 0x80)
        return false;
      v = (v << 6) | (c & 63);
    }
    if (v < minimum || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff) ||
        (v >= 0x80 && v <= 0x9f) || v == 0x2028 || v == 0x2029 ||
        (v >= 0x202a && v <= 0x202e) || (v >= 0x2066 && v <= 0x2069))
      return false;
  }
  return true;
}
int r2_group_mac(uint8_t out[16], const struct r2_group_config *g,
                 const char domain[8], const void *body, size_t size) {
  if (!out || !domain || !body || size > 512 || !r2_group_valid(g))
    return -EINVAL;
  uint8_t context[13], full[32];
  context[0] = R2_GROUP_VERSION;
  r2_group_put(context + 1, g->id, 8);
  r2_group_put(context + 9, g->epoch, 4);
  struct tc_hmac_state_struct h;
  if (!tc_hmac_set_key(&h, g->psk, 32) || !tc_hmac_init(&h) ||
      !tc_hmac_update(&h, domain, 8) ||
      !tc_hmac_update(&h, context, sizeof context) ||
      !tc_hmac_update(&h, body, (unsigned)size) ||
      !tc_hmac_final(full, sizeof full, &h))
    return -EIO;
  memcpy(out, full, 16);
  memset(full, 0, sizeof full);
  return 0;
}
bool r2_group_verify(const uint8_t tag[16], const struct r2_group_config *g,
                     const char domain[8], const void *body, size_t size) {
  uint8_t wanted[16], diff = 0;
  if (!tag || r2_group_mac(wanted, g, domain, body, size))
    return false;
  for (unsigned i = 0; i < 16; i++)
    diff |= wanted[i] ^ tag[i];
  return diff == 0;
}
void r2_group_pair_key(uint8_t out[16], const struct r2_group_config *g,
                       uint64_t sender, uint64_t receiver) {
  uint8_t ids[16];
  r2_group_put(ids, sender, 8);
  r2_group_put(ids + 8, receiver, 8);
  if (r2_group_mac(out, g, "PTOBG1K1", ids, sizeof ids))
    memset(out, 0, 16);
}
