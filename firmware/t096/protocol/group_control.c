/* SPDX-License-Identifier: Apache-2.0 */
#include "group_control.h"
#include "group_join.h"
#include <errno.h>
#include <string.h>
/* One byte carries bounded name length (0..24) and capability bits (0..3).
 * Name bytes are zero padded; capabilities are authenticated claims, not
 * evidence. */
size_t r2_group_control_size(unsigned k) {
  return r2_auth_size(k) - 8 + 4 + 32 + (k == R2_AUTH_HELLO ? 25 : 0) + 16;
}
size_t r2_control_wire_size(const uint8_t *w) {
  if (w[0] == 14)
    return R2_GROUP_JOIN_BYTES;
  if(w[0]==19&&w[1]<3)return 160;
  return (w[0] == 13 || w[0] == 15 || w[0] == 17 || w[0]==19) ? r2_group_control_size(w[1]) : r2_auth_size(w[1]);
}
static bool dictionary(const uint64_t ids[4]) {
  for (unsigned i = 0; i < 4; i++)
    if (ids[i])
      for (unsigned j = 0; j < i; j++)
        if (ids[i] == ids[j])
          return false;
  return true;
}
static int control_encode(uint8_t out[R2_GROUP_CONTROL_MAX],
                            const struct r2_auth_record *r,
                            const struct r2_group_config *g,
                            const uint64_t ids[4], const char *name,
                            uint8_t caps,unsigned v14) {
  if (!out || !r || !ids || !r2_group_valid(g) || !dictionary(ids) ||
      r->sender >= 4 || r->target >= 4 || !ids[r->sender] || !ids[r->target] ||
      caps > 3)
    return -EINVAL;
  uint8_t legacy[R2_AUTH_BYTES];
  int rc = r2_auth_encode_body(legacy, r, v14==5);
  if (rc)
    return rc;
  size_t body = r2_auth_size(r->kind) - 8, n = r2_group_control_size(r->kind);
  memset(out, 0, R2_GROUP_CONTROL_MAX);
  memcpy(out, legacy, body);
  out[0] = v14==5?19:v14?17:13;
  r2_group_put(out + body, g->epoch, 4);
  for (unsigned i = 0; i < 4; i++)
    r2_group_put(out + body + 4 + i * 8, ids[i], 8);
  if (r->kind == R2_AUTH_HELLO) {
    const char *end = name ? memchr(name, 0, 25) : NULL;
    size_t len = end ? (size_t)(end - name) : 25;
    if (!r2_group_name(name, len))
      return -EINVAL;
    out[body + 36] = (uint8_t)len | (caps << 5);
    memcpy(out + body + 37, name, len);
  }
  return r2_group_mac(out + n - 16, g, v14==5?"PTOBG5C5":v14?"PTOBG3C4":"PTOBG1C1", out, n - 16);
}
static int identity_index(const uint64_t ids[4], uint64_t id) {
  if (!id)
    return -1;
  for (unsigned i = 0; i < 4; i++)
    if (ids[i] == id)
      return (int)i;
  return -1;
}
static bool remap(uint8_t *slot, const uint64_t remote[4],
                  const uint64_t local[4], bool nullable) {
  if (nullable && *slot == 255)
    return true;
  if (*slot >= 4)
    return false;
  int at = identity_index(local, remote[*slot]);
  if (at < 0)
    return false;
  *slot = (uint8_t)at;
  return true;
}
static int control_decode(struct r2_auth_record *r, const uint8_t *w,
                            size_t size, const struct r2_group_config *g,
                            const uint64_t ids[4], uint64_t expected_sender,
                            char name[25], uint8_t *caps,unsigned v14,struct r2_group_pair_hint *hint) {
  if(hint)*hint=(struct r2_group_pair_hint){0};
  if (!r || !w || !ids || !r2_group_valid(g) || !name || !caps || size < 2 ||
      w[0] != (v14==5?19:v14?17:13) || w[1] < 8 || w[1] > 13 ||
      size != r2_group_control_size(w[1]))
    return -EINVAL;
  size_t body = r2_auth_size(w[1]) - 8;
  if (r2_group_get(w + body, 4) != g->epoch ||
      !r2_group_verify(w + size - 16, g, v14==5?"PTOBG5C5":v14?"PTOBG3C4":"PTOBG1C1", w, size - 16))
    return -EACCES;
  uint64_t remote[4];
  for (unsigned i = 0; i < 4; i++)
    remote[i] = r2_group_get(w + body + 4 + i * 8, 8);
  if (!dictionary(remote) || w[2] >= 4 || remote[w[2]] != expected_sender)
    return -EACCES;
  struct r2_auth_record x;
  int rc = r2_auth_parse_authenticated(&x, w, v14==5);
  if (rc)
    return rc;
  struct r2_group_pair_hint extracted={0};
  if(x.kind==R2_AUTH_HELLO&&x.pair_until_us&&x.pair_root<4&&x.pair_standby<4)
    extracted=(struct r2_group_pair_hint){remote[x.pair_root],remote[x.pair_standby],x.pair_root_boot,x.pair_standby_boot,x.pair_until_us,x.pair_epoch};
  if (!remap(&x.sender, remote, ids, false) ||
      !remap(&x.target, remote, ids, false) ||
      !remap(&x.root, remote, ids, true))
    return -ESTALE;
  if (x.pair_until_us && (!remap(&x.pair_root, remote, ids, false) ||
                          !remap(&x.pair_standby, remote, ids, false))) {
    /* An authenticated optional pair hint may name a not-yet-discovered
     * partner. Omit control authority, retaining only separate bounded discovery
     * evidence when requested; do not deadlock this peer's fresh HELLO. */
    x.pair_root = x.pair_standby = 0;
    x.pair_root_boot = x.pair_standby_boot = x.pair_until_us = 0;
    x.pair_epoch = 0;
  }
  name[0] = 0;
  *caps = 0;
  if (x.kind == R2_AUTH_HELLO) {
    unsigned length = w[body + 36] & 31;
    *caps = w[body + 36] >> 5;
    if (*caps > 3 || !r2_group_name((const char *)w + body + 37, length))
      return -EINVAL;
    for (unsigned i = length; i < 24; i++)
      if (w[body + 37 + i])
        return -EINVAL;
    memcpy(name, w + body + 37, length);
    name[length] = 0;
  }
  *r = x;if(hint)*hint=extracted;
  return 0;
}

int r2_group_control_encode(uint8_t out[R2_GROUP_CONTROL_MAX],const struct r2_auth_record *r,const struct r2_group_config *g,const uint64_t ids[4],const char *name,uint8_t caps){return control_encode(out,r,g,ids,name,caps,false);}
int r2_group_control_encode14(uint8_t *out,const struct r2_auth_record *r,const struct r2_group_config *g,const uint64_t ids[4],const char *name,uint8_t caps){return control_encode(out,r,g,ids,name,caps,true);}
int r2_group_control_decode(struct r2_auth_record *r,const uint8_t *w,size_t size,const struct r2_group_config *g,const uint64_t ids[4],uint64_t sender,char name[25],uint8_t *caps){return control_decode(r,w,size,g,ids,sender,name,caps,false,NULL);}
int r2_group_control_decode14(struct r2_auth_record *r,const uint8_t *w,size_t size,const struct r2_group_config *g,const uint64_t ids[4],uint64_t sender,char name[25],uint8_t *caps){return control_decode(r,w,size,g,ids,sender,name,caps,true,NULL);}
int r2_group_control_decode14_hint(struct r2_auth_record *r,const uint8_t *w,size_t size,const struct r2_group_config *g,const uint64_t ids[4],uint64_t sender,char name[25],uint8_t *caps,struct r2_group_pair_hint *hint){return control_decode(r,w,size,g,ids,sender,name,caps,true,hint);}

int r2_group_control_encode5(uint8_t *w,const struct r2_auth_record *r,const struct r2_group_config *g,const uint64_t ids[4],const char *name,uint8_t caps){return control_encode(w,r,g,ids,name,caps,5);}
int r2_group_control_decode5(struct r2_auth_record *r,const uint8_t *w,size_t size,const struct r2_group_config *g,const uint64_t ids[4],uint64_t sender,char name[25],uint8_t *caps,struct r2_group_pair_hint *hint){return control_decode(r,w,size,g,ids,sender,name,caps,5,hint);}
