/* SPDX-License-Identifier: Apache-2.0 */
#include "gm_standby.h"
#include "authority_codec.h"
#include <errno.h>
#include <string.h>

#define RETAIN __attribute__((used, retain))
static uint64_t get_le(const uint8_t *p, unsigned n)
{
  uint64_t v = 0;
  for (unsigned i = 0; i < n; i++) { v |= (uint64_t)p[i] << (8 * i); }
  return v;
}
static void put_le(uint8_t *p, uint64_t v, unsigned n)
{
  for (unsigned i = 0; i < n; i++) { p[i] = (uint8_t)(v >> (8 * i)); }
}
static uint64_t rotate(uint64_t v, unsigned n)
{
  return (v << n) | (v >> (64 - n));
}
static void sip_round(uint64_t v[4])
{
  v[0] += v[1]; v[1] = rotate(v[1], 13) ^ v[0]; v[0] = rotate(v[0], 32);
  v[2] += v[3]; v[3] = rotate(v[3], 16) ^ v[2];
  v[0] += v[3]; v[3] = rotate(v[3], 21) ^ v[0];
  v[2] += v[1]; v[1] = rotate(v[1], 17) ^ v[2]; v[2] = rotate(v[2], 32);
}
static uint64_t control_tag(const uint8_t wire[R2_AUTH_BODY_BYTES], const uint8_t key[16],size_t body)
{
  /* Versioned eight-byte Config5 domain and SipHash-2-4.
   * Fixed154B input, bounded stack; no heap, logging or key provisioning. */
  uint8_t input[8 + R2_AUTH_BODY_BYTES] = {'P','T','O','B','1','2','A',5};
  memcpy(input + 8, wire, body);
  size_t length=8+body;
  uint64_t k0 = get_le(key, 8), k1 = get_le(key + 8, 8);
  uint64_t v[4] = {UINT64_C(0x736f6d6570736575) ^ k0,
    UINT64_C(0x646f72616e646f6d) ^ k1, UINT64_C(0x6c7967656e657261) ^ k0,
    UINT64_C(0x7465646279746573) ^ k1};
  size_t end = length / 8 * 8;
  for (size_t at = 0; at < end; at += 8) {
    uint64_t m = get_le(input + at, 8);
    v[3] ^= m; sip_round(v); sip_round(v); v[0] ^= m;
  }
  uint64_t last = (uint64_t)length << 56;
  last |= get_le(input + end, length - end);
  v[3] ^= last; sip_round(v); sip_round(v); v[0] ^= last; v[2] ^= 255;
  for (unsigned i = 0; i < 4; i++) { sip_round(v); }
  return v[0] ^ v[1] ^ v[2] ^ v[3];
}
static int valid(const struct r2_auth_record *r,bool gm5)
{
  if (!r || r->kind<8 || r->kind>13 || r->sender==255 || r->target==255 ||
      r->sender==r->target || (r->flags&~7u) || r->config_epoch!=R2_AUTH_CONFIG ||
      !r->boot || !r->coordinator_boot || !r->challenge || !r->sequence ||
      r->expires_us<=r->created_us || r->evidence_us>r->created_us ||
      r->expires_us-r->created_us>(gm5?R2_GM5_RECORD_US:R2_AUTH_ORIGINAL_TTL_US)) return -EINVAL;
  if(r->pair_until_us){
    if(r->kind!=R2_AUTH_HELLO||r->pair_root>=4||r->pair_standby>=4||r->pair_root==r->pair_standby||
       (r->sender!=r->pair_root&&r->sender!=r->pair_standby)||!r->pair_root_boot||!r->pair_standby_boot||!r->pair_epoch||
       r->pair_until_us<=r->created_us||r->pair_until_us-r->created_us>10000000)return -EINVAL;
  }else if(r->pair_root||r->pair_standby||r->pair_root_boot||r->pair_standby_boot||r->pair_epoch)return -EINVAL;
  return 0;
}
int r2_auth_encode_body(uint8_t out[R2_AUTH_BYTES],const struct r2_auth_record *r,bool gm5)
{
  if (!out || valid(r,gm5)) { return -EINVAL; }
  memset(out,0,R2_AUTH_BYTES);
  out[0] = 12; out[1] = r->kind; out[2] = r->sender; out[3] = r->target;
#define PUT(o, field, n) put_le(out + (o), r->field, (n))
  PUT(4, boot, 8); PUT(12, coordinator_boot, 8); PUT(20, challenge, 8);
  PUT(28, sequence, 8); PUT(36, decision, 8); out[44] = r->root;
  PUT(45, root_boot, 8); PUT(53, source_epoch, 4); PUT(57, source_second, 8);
  PUT(65, evidence_us, 8); PUT(73, created_us, 8); PUT(81, expires_us, 8);
  PUT(89, permit_until_us, 8); PUT(97, config_epoch, 4); out[101] = r->flags;
  out[102]=r->priority1;out[103]=r->clock_class;out[104]=r->accuracy;out[105]=r->priority2;
  PUT(106,variance,2);PUT(108,boot_age_ms,4);PUT(112,source_age_ms,4);
  if(r->kind==R2_AUTH_HELLO){out[116]=r->pair_root;out[117]=r->pair_standby;PUT(118,pair_root_boot,8);PUT(126,pair_standby_boot,8);PUT(134,pair_epoch,4);PUT(138,pair_until_us,8);}
#undef PUT
  return 0;
}
RETAIN int r2_auth_encode(uint8_t out[R2_AUTH_BYTES],const struct r2_auth_record *r,const uint8_t key[16]){
 if(!key)return -EINVAL;
 int rc=r2_auth_encode_body(out,r,false);if(rc)return rc;
 size_t body=r2_auth_size(r->kind)-8;put_le(out+body,control_tag(out,key,body),8);return 0;
}
RETAIN int r2_auth_decode(struct r2_auth_record *r, const uint8_t *wire,
                              size_t length, const uint8_t key[16])
{
  if (!r || !wire || !key || length<2 || length != r2_auth_size(wire[1])) { return -EINVAL; }
  memset(r, 0, sizeof(*r));
  if (wire[0] != 12) { return -EPROTONOSUPPORT; }
  uint8_t computed[8], mismatch = 0;
  put_le(computed, control_tag(wire, key,length-8), 8);
  for (unsigned i = 0; i < 8; i++) { mismatch |= computed[i] ^ wire[length-8 + i]; }
  if (mismatch) { return -EACCES; }
  return r2_auth_parse_authenticated(r,wire,false);
}
int r2_auth_parse_authenticated(struct r2_auth_record *r,const uint8_t *wire,bool gm5)
{
  struct r2_auth_record candidate = {0};
  candidate.kind = wire[1]; candidate.sender = wire[2]; candidate.target = wire[3];
  candidate.root = wire[44]; candidate.flags = wire[101];
  candidate.priority1=wire[102];candidate.clock_class=wire[103];candidate.accuracy=wire[104];candidate.priority2=wire[105];
#define GET(o, field, n) candidate.field = get_le(wire + (o), (n))
  GET(4, boot, 8); GET(12, coordinator_boot, 8); GET(20, challenge, 8);
  GET(28, sequence, 8); GET(36, decision, 8); GET(45, root_boot, 8);
  GET(53, source_epoch, 4); GET(57, source_second, 8); GET(65, evidence_us, 8);
  GET(73, created_us, 8); GET(81, expires_us, 8); GET(89, permit_until_us, 8);
  GET(97, config_epoch, 4);GET(106,variance,2);GET(108,boot_age_ms,4);GET(112,source_age_ms,4);
  if(candidate.kind==R2_AUTH_HELLO){candidate.pair_root=wire[116];candidate.pair_standby=wire[117];GET(118,pair_root_boot,8);GET(126,pair_standby_boot,8);GET(134,pair_epoch,4);GET(138,pair_until_us,8);}
#undef GET
  if (valid(&candidate,gm5)) { return -EINVAL; }
  *r = candidate;
  return 0;
}
