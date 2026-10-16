/* SPDX-License-Identifier: Apache-2.0 */
#include "group_precision.h"
#include <errno.h>
#include <string.h>
int r2_group_slot(const uint64_t ids[4], uint64_t id) {
  if (!id)
    return -1;
  for (unsigned i = 0; i < 4; i++)
    if (ids[i] == id)
      return (int)i;
  return -1;
}
int r2_group_precision_encode(uint8_t ad[158],
                              const struct r2_precision_packet *p,
                              const struct r2_group_config *g,
                              const uint64_t ids[4]) {
  if (!p || !ad || !ids || !r2_group_valid(g) || p->root >= 4 || !ids[p->root])
    return -EINVAL;
  static const uint8_t zero[4][16] = {{0}};
  int rc = r2_precision_encode(ad, p, zero);
  if (rc)
    return rc;
  ad[4] = 'G';
  ad[5] = '1';
  ad[6] = 1;
  ad[7] = 0;
  r2_group_put(ad + 126, ids[p->root], 8);
  r2_group_put(ad + 134, g->epoch, 4);
  memset(ad + 138, 0, 4);
  return r2_group_mac(ad + 142, g, "PTOBG1P1", ad, 142);
}
int r2_group_precision_decode(struct r2_precision_packet *p, const uint8_t *ad,
                              size_t n, const struct r2_group_config *g,
                              const uint64_t ids[4]) {
  if (!p || !ad || !ids || !r2_group_valid(g) || n != 158 || ad[0] != 157 || ad[1] != 255 || ad[2] != 255 ||
      ad[3] != 255 || ad[4] != 'G' || ad[5] != '1' || ad[6] != 1 || ad[7] ||
      r2_group_get(ad + 34, 2) != 50 || r2_group_get(ad + 60, 2) != 1 ||
      r2_group_get(ad + 134, 4) != g->epoch || r2_group_get(ad + 138, 4))
    return -EINVAL;
  if (!r2_group_verify(ad + 142, g, "PTOBG1P1", ad, 142))
    return -EACCES;
  int slot = r2_group_slot(ids, r2_group_get(ad + 126, 8));
  if (slot < 0)
    return -ENOENT;
  uint8_t copy[158];
  memcpy(copy, ad, 158);
  copy[7] = (uint8_t)slot;
  return r2_precision_parse_authenticated(p, copy);
}
static int origin(uint8_t out[60], const struct r2_relay_origin *o,
                  const uint64_t ids[4], bool generic) {
  if (!o || !out || !ids || o->root >= 4 || o->relay >= 4 || !ids[o->root] || (!generic && !ids[o->relay]) ||
      o->root == o->relay)
    return -EINVAL;
  r2_group_put(out, ids[o->root], 8);
  r2_group_put(out + 8, generic ? 0 : ids[o->relay], 8);
  r2_group_put(out + 16, o->boot, 8);
  r2_group_put(out + 24, o->source_epoch, 4);
  r2_group_put(out + 28, o->created_ns, 8);
  r2_group_put(out + 36, o->expires_ns, 8);
  r2_group_put(out + 44, o->frame_ns, 8);
  r2_group_put(out + 52, (uint64_t)o->utc_offset_ns, 8);
  return 0;
}
int r2_group_origin_sign(uint8_t proof[32], const struct r2_relay_origin *o,
                         const struct r2_group_config *g,
                         const uint64_t ids[4]) {
  uint8_t body[60];
  int rc = origin(body, o, ids, false);
  if (rc)
    return rc;
  memset(proof, 0, 32);
  return r2_group_mac(proof, g, "PTOBG1O1", body, sizeof body);
}
int r2_group_relay_encode(uint8_t ad[190], const struct r2_relay_packet *p,
                          const uint8_t proof[32],
                          const struct r2_group_config *g,
                          const uint64_t ids[4]) {
  if (!p || !ad || !proof || !ids || !r2_group_valid(g) || p->route.transmitter >= 4 || p->route.parent >= 4 ||
      !ids[p->route.transmitter] || !ids[p->route.parent])
    return -EINVAL;
  /* Reuse the strict canonical body encoder, never its recipient proof
   * semantics. */
  static const uint8_t zero[4][16] = {{0}};
  uint8_t legacy[190];
  int rc = r2_relay_encode(legacy, p, proof, zero);
  if (rc)
    return rc;
  memset(ad, 0, 190);
  ad[0] = 189;
  ad[1] = ad[2] = ad[3] = 255;
  ad[4] = 'G';
  ad[5] = 'R';
  ad[6] = 1;
  ad[7] = p->count;
  r2_group_put(ad + 8, 50, 2);
  r2_group_put(ad + 10, g->epoch, 4);
  rc = origin(ad + 14, &p->origin, ids, false);
  if (rc)
    return rc;
  memcpy(ad + 74, proof, 16);
  r2_group_put(ad + 90, ids[p->route.transmitter], 8);
  r2_group_put(ad + 98, ids[p->route.parent], 8);
  memcpy(ad + 106, legacy + 90, 30);
  memcpy(ad + 136, legacy + 120, 38);
  return r2_group_mac(ad + 174, g, "PTOBG1R1", ad, 174);
}
int r2_group_relay_decode(struct r2_relay_packet *p, const uint8_t *ad,
                          size_t n, const struct r2_group_config *g,
                          const uint64_t ids[4]) {
  if (!p || !ad || !ids || !r2_group_valid(g) || n != 190 || ad[0] != 189 || ad[1] != 255 || ad[2] != 255 ||
      ad[3] != 255 || ad[4] != 'G' || ad[5] != 'R' || ad[6] != 1 ||
      r2_group_get(ad + 8, 2) != 50 || r2_group_get(ad + 10, 4) != g->epoch)
    return -EINVAL;
  if (!r2_group_verify(ad + 74, g, "PTOBG1O1", ad + 14, 60) ||
      !r2_group_verify(ad + 174, g, "PTOBG1R1", ad, 174))
    return -EACCES;
  int root = r2_group_slot(ids, r2_group_get(ad + 14, 8)),
      relay = r2_group_slot(ids, r2_group_get(ad + 22, 8));
  int sender = r2_group_slot(ids, r2_group_get(ad + 90, 8)),
      parent = r2_group_slot(ids, r2_group_get(ad + 98, 8));
  if (root < 0 || relay < 0 || sender < 0 || parent < 0)
    return -ENOENT;
  uint8_t legacy[190] = {0};
  legacy[7] = ad[7];
  legacy[10] = (uint8_t)root;
  legacy[11] = (uint8_t)relay;
  memcpy(legacy + 12, ad + 30, 44);
  legacy[88] = (uint8_t)sender;
  legacy[89] = (uint8_t)parent;
  memcpy(legacy + 90, ad + 106, 30);
  memcpy(legacy + 120, ad + 136, 38);
  return r2_relay_parse_authenticated(p, legacy);
}

/* GRv2 authenticates a generic immutable source envelope. The private indices
 * below belong to a timing path, never the four-member control roster. */
int r2_group_origin_sign2(uint8_t proof[32],const struct r2_relay_origin *o,
 const struct r2_group_config *g,const uint64_t ids[4]) {
 uint8_t body[60];int rc=origin(body,o,ids,true);
 if(rc||!proof||!r2_group_valid(g))return rc?rc:-EINVAL;
 memset(proof,0,32);return r2_group_mac(proof,g,"PTOBG2O2",body,sizeof body);
}
int r2_group_relay_encode2(uint8_t ad[190],const struct r2_relay_packet *p,
 const uint8_t proof[32],const struct r2_group_config *g,const uint64_t ids[4]) {
 if(!p||!ad||!proof||!ids||!r2_group_valid(g)||p->route.transmitter>=4||
    p->route.parent>=4||!ids[p->route.transmitter]||!ids[p->route.parent])return -EINVAL;
 static const uint8_t zero[4][16]={{0}};uint8_t legacy[190];
 int rc=r2_relay_encode(legacy,p,proof,zero);if(rc)return rc;
 memset(ad,0,190);ad[0]=189;ad[1]=ad[2]=ad[3]=255;ad[4]='G';ad[5]='R';ad[6]=2;ad[7]=p->count;
 r2_group_put(ad+8,50,2);r2_group_put(ad+10,g->epoch,4);
 rc=origin(ad+14,&p->origin,ids,true);if(rc)return rc;
 memcpy(ad+74,proof,16);r2_group_put(ad+90,ids[p->route.transmitter],8);
 r2_group_put(ad+98,ids[p->route.parent],8);memcpy(ad+106,legacy+90,30);
 memcpy(ad+136,legacy+120,38);return r2_group_mac(ad+174,g,"PTOBG2R2",ad,174);
}
int r2_group_relay_decode2(struct r2_relay_packet *p,const uint8_t *ad,size_t n,
 const struct r2_group_config *g,const uint64_t ids[4]) {
 if(!p||!ad||!ids||!r2_group_valid(g)||n!=190||ad[0]!=189||ad[1]!=255||ad[2]!=255||
    ad[3]!=255||ad[4]!='G'||ad[5]!='R'||ad[6]!=2||r2_group_get(ad+8,2)!=50||
    r2_group_get(ad+10,4)!=g->epoch||r2_group_get(ad+22,8))return -EINVAL;
 if(!r2_group_verify(ad+74,g,"PTOBG2O2",ad+14,60)||
    !r2_group_verify(ad+174,g,"PTOBG2R2",ad,174))return -EACCES;
 int root=r2_group_slot(ids,r2_group_get(ad+14,8));
 int sender=r2_group_slot(ids,r2_group_get(ad+90,8)),parent=r2_group_slot(ids,r2_group_get(ad+98,8));
 if(root<0||sender<0||parent<0)return -ENOENT;
 /* For depth0 the unused relay position is only a canonical parser sentinel.
  * A receiver's path context supplies the actual relay position for depth1. */
 int relay=ad[106]?sender:(root+1)%4;
 uint8_t legacy[190]={0};legacy[7]=ad[7];legacy[10]=(uint8_t)root;legacy[11]=(uint8_t)relay;
 memcpy(legacy+12,ad+30,44);legacy[88]=(uint8_t)sender;legacy[89]=(uint8_t)parent;
 memcpy(legacy+90,ad+106,30);memcpy(legacy+120,ad+136,38);
 return r2_relay_parse_authenticated(p,legacy);
}

/* GRv3 keeps both 19-byte physical corrections, original reference and both
 * 128-bit MACs. The formerly zero generic relay-id field holds signed policy.
 * Header version3 and separate MAC domains reject every earlier wire version. */
static int origin3(uint8_t body[60],const struct r2_relay_origin *o,const uint64_t ids[4]){
 int rc=origin(body,o,ids,true);if(rc)return rc;
 if((!o->hold_horizon_us)!=(!o->hold_budget_ns)||o->hold_horizon_us>10000000||
    o->hold_budget_ns==UINT32_MAX)return -EINVAL;
 r2_group_put(body+8,o->hold_horizon_us,4);r2_group_put(body+12,o->hold_budget_ns,4);return 0;
}
int r2_group_origin_sign3(uint8_t proof[32],const struct r2_relay_origin *o,
 const struct r2_group_config *g,const uint64_t ids[4]){
 uint8_t body[60];if(!proof||!r2_group_valid(g))return -EINVAL;
 int rc=origin3(body,o,ids);if(rc)return rc;
 memset(proof,0,32);return r2_group_mac(proof,g,"PTOBG3O3",body,60);
}
int r2_group_relay_encode3(uint8_t ad[190],const struct r2_relay_packet *p,
 const uint8_t proof[32],const struct r2_group_config *g,const uint64_t ids[4]){
 if(!ad||!p||!proof||!ids||!r2_group_valid(g)||r2_relay_validate3(p)||
    !ids[p->route.transmitter]||!ids[p->route.parent])return -EINVAL;
 memset(ad,0,190);ad[0]=189;ad[1]=ad[2]=ad[3]=255;ad[4]='G';ad[5]='R';ad[6]=3;ad[7]=p->count;
 r2_group_put(ad+8,50,2);r2_group_put(ad+10,g->epoch,4);
 int rc=origin3(ad+14,&p->origin,ids);if(rc)return rc;
 memcpy(ad+74,proof,16);r2_group_put(ad+90,ids[p->route.transmitter],8);
 r2_group_put(ad+98,ids[p->route.parent],8);
 const struct r2_relay_route *r=&p->route;ad[106]=r->depth;ad[107]=r->slot;
 r2_group_put(ad+108,r->boot,8);r2_group_put(ad+116,r->train,4);
 r2_group_put(ad+120,r->publication,4);r2_group_put(ad+124,r->capture_epoch,4);
 r2_group_put(ad+128,r->map_generation,4);r2_group_put(ad+132,r->bound_ns,4);
 for(unsigned i=0;i<p->count;i++){
  const struct r2_relay_correction *c=&p->correction[i];uint8_t *w=ad+136+19*i;
  r2_group_put(w,c->publication,4);r2_group_put(w+4,c->event,2);w[6]=c->channel;
  r2_group_put(w+7,c->ticks,8);r2_group_put(w+15,c->generation,4);
 }
 return r2_group_mac(ad+174,g,"PTOBG3R3",ad,174);
}
int r2_group_relay_decode3(struct r2_relay_packet *p,const uint8_t *ad,size_t n,
 const struct r2_group_config *g,const uint64_t ids[4]){
 if(!p||!ad||!ids||!r2_group_valid(g)||n!=190||ad[0]!=189||ad[1]!=255||ad[2]!=255||
    ad[3]!=255||ad[4]!='G'||ad[5]!='R'||ad[6]!=3||r2_group_get(ad+8,2)!=50||
    r2_group_get(ad+10,4)!=g->epoch)return -EINVAL;
 if(!r2_group_verify(ad+74,g,"PTOBG3O3",ad+14,60)||
    !r2_group_verify(ad+174,g,"PTOBG3R3",ad,174))return -EACCES;
 int root=r2_group_slot(ids,r2_group_get(ad+14,8));
 int sender=r2_group_slot(ids,r2_group_get(ad+90,8)),parent=r2_group_slot(ids,r2_group_get(ad+98,8));
 if(root<0||sender<0||parent<0)return -ENOENT;
 memset(p,0,sizeof *p);p->count=ad[7];struct r2_relay_origin *o=&p->origin;
 o->root=root;o->relay=ad[106]?sender:(root+1)%4;
 o->hold_horizon_us=r2_group_get(ad+22,4);o->hold_budget_ns=r2_group_get(ad+26,4);
 o->boot=r2_group_get(ad+30,8);o->source_epoch=r2_group_get(ad+38,4);
 o->created_ns=r2_group_get(ad+42,8);o->expires_ns=r2_group_get(ad+50,8);o->frame_ns=r2_group_get(ad+58,8);
 uint64_t offset=r2_group_get(ad+66,8);o->utc_offset_ns=offset<=INT64_MAX?(int64_t)offset:-1-(int64_t)(UINT64_MAX-offset);
 struct r2_relay_route *r=&p->route;r->transmitter=sender;r->parent=parent;r->depth=ad[106];r->slot=ad[107];
 r->boot=r2_group_get(ad+108,8);r->train=r2_group_get(ad+116,4);r->publication=r2_group_get(ad+120,4);
 r->capture_epoch=r2_group_get(ad+124,4);r->map_generation=r2_group_get(ad+128,4);r->bound_ns=r2_group_get(ad+132,4);
 for(unsigned i=0;i<2;i++){
  const uint8_t *w=ad+136+19*i;struct r2_relay_correction *c=&p->correction[i];
  c->publication=r2_group_get(w,4);c->event=r2_group_get(w+4,2);c->channel=w[6];
  c->ticks=r2_group_get(w+7,8);c->generation=r2_group_get(w+15,4);
 }
 return r2_relay_validate3(p);
}
