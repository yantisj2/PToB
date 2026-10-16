/* SPDX-License-Identifier: Apache-2.0 */
#include "relay_codec.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
uint32_t r2_relay_bound_add(uint32_t upstream, uint32_t hop) {
  return upstream == R2_RELAY_BOUND_UNKNOWN || hop == R2_RELAY_BOUND_UNKNOWN ||
         hop >= R2_RELAY_BOUND_UNKNOWN - upstream
         ? R2_RELAY_BOUND_UNKNOWN : upstream + hop;
}
static uint64_t get(const uint8_t *p, unsigned n) {
  uint64_t v=0; for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i); return v;
}
static void put(uint8_t *p,uint64_t v,unsigned n) {
  for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));
}
static uint64_t rotate(uint64_t v,unsigned n){return(v<<n)|(v>>(64-n));}
static void round_(uint64_t v[4]) {
  v[0]+=v[1];v[1]=rotate(v[1],13)^v[0];v[0]=rotate(v[0],32);
  v[2]+=v[3];v[3]=rotate(v[3],16)^v[2];v[0]+=v[3];
  v[3]=rotate(v[3],21)^v[0];v[2]+=v[1];v[1]=rotate(v[1],17)^v[2];v[2]=rotate(v[2],32);
}
static uint64_t tag(const char domain[8],const uint8_t *body,size_t size,const uint8_t key[16]) {
  uint8_t input[162];memcpy(input,domain,8);memcpy(input+8,body,size);size+=8;
  uint64_t a=get(key,8),b=get(key+8,8),v[4]={
    UINT64_C(0x736f6d6570736575)^a,UINT64_C(0x646f72616e646f6d)^b,
    UINT64_C(0x6c7967656e657261)^a,UINT64_C(0x7465646279746573)^b};
  size_t end=size/8*8;
  for(size_t i=0;i<end;i+=8){uint64_t m=get(input+i,8);v[3]^=m;round_(v);round_(v);v[0]^=m;}
  uint64_t last=(uint64_t)size<<56;last|=get(input+end,(unsigned)(size-end));
  v[3]^=last;round_(v);round_(v);v[0]^=last;v[2]^=255;
  for(unsigned i=0;i<4;i++) {
    round_(v);
  }
  return v[0]^v[1]^v[2]^v[3];
}
static void origin_wire(uint8_t w[46],const struct r2_relay_origin *o) {
  w[0]=o->root;w[1]=o->relay;put(w+2,o->boot,8);put(w+10,o->source_epoch,4);
  put(w+14,o->created_ns,8);put(w+22,o->expires_ns,8);put(w+30,o->frame_ns,8);
  put(w+38,(uint64_t)o->utc_offset_ns,8);
}
static int valid_origin_limit(const struct r2_relay_origin *o,uint64_t maximum) {
  return !o||o->root>=4||o->relay>=4||o->root==o->relay||!o->boot||!o->source_epoch||
    !o->created_ns||o->created_ns>=o->expires_ns||o->expires_ns>INT64_MAX||
    o->expires_ns-o->created_ns>maximum||o->frame_ns>o->created_ns
    ?-EINVAL:0;
}
static int validate_limit(const struct r2_relay_packet *p,uint64_t maximum) {
  if(!p||valid_origin_limit(&p->origin,maximum)||p->count>2)return-EINVAL;
  if((!p->origin.hold_horizon_us)!=(!p->origin.hold_budget_ns)||
     p->origin.hold_horizon_us>10000000||p->origin.hold_budget_ns==UINT32_MAX)return -EINVAL;
  uint64_t permitted=p->origin.expires_ns;
  if(p->origin.hold_horizon_us){
   uint64_t delta=(uint64_t)p->origin.hold_horizon_us*1000;
   if(p->origin.created_ns>INT64_MAX-delta||delta<p->origin.expires_ns-p->origin.created_ns)return -EINVAL;
   permitted=p->origin.created_ns+delta;
  }
  const struct r2_relay_route *r=&p->route;
  if(r->transmitter>=4||r->parent>=4||r->depth>1||!r->boot||!r->train||
    !r->publication||!r->capture_epoch||!r->map_generation)return-EINVAL;
  if(!r->depth){
    if(r->transmitter!=p->origin.root||r->parent!=p->origin.root||r->slot||r->boot!=p->origin.boot)return-EINVAL;
  }else if(r->transmitter!=p->origin.relay||r->parent!=p->origin.root||r->slot<1||r->slot>3)return-EINVAL;
  if(!p->origin.frame_ns && (r->depth || p->count)) return -EINVAL;
  for(unsigned i=0;i<2;i++){
    const struct r2_relay_correction *c=&p->correction[i];
    if(i>=p->count){if(c->publication||c->event||c->channel||c->ticks||c->generation)return-EINVAL;}
    else if(!c->publication||c->channel>36||!c->ticks||!c->generation||c->generation!=r->map_generation||
      c->ticks>UINT64_MAX/125||c->ticks*125/2<p->origin.created_ns||
      c->ticks*125/2>=permitted)return-EINVAL;
  }
  return 0;
}
int r2_relay_validate(const struct r2_relay_packet *p){return validate_limit(p,UINT64_C(1500000000));}
int r2_relay_validate3(const struct r2_relay_packet *p){return validate_limit(p,UINT64_C(1650000000));}
int r2_relay_origin_sign(uint8_t tags[32],const struct r2_relay_origin *o,const uint8_t keys[4][16]) {
  if(!tags||!keys||valid_origin_limit(o,UINT64_C(1500000000)))return-EINVAL;
  uint8_t w[46];origin_wire(w,o);for(unsigned i=0;i<4;i++)put(tags+i*8,tag("PTOBR2O2",w,46,keys[i]),8);return 0;
}
int r2_relay_encode(uint8_t ad[190],const struct r2_relay_packet *p,const uint8_t ot[32],const uint8_t keys[4][16]) {
  if(!ad||!ot||!keys||r2_relay_validate(p)||p->origin.hold_horizon_us||p->origin.hold_budget_ns)return-EINVAL;
  memset(ad,0,190);ad[0]=189;ad[1]=ad[2]=ad[3]=255;
  uint8_t *w=ad+4;w[0]='R';w[1]='2';w[2]=2;w[3]=p->count;put(w+4,50,2);
  origin_wire(w+6,&p->origin);memcpy(w+52,ot,32);
  const struct r2_relay_route *r=&p->route;uint8_t *x=w+84;
  x[0]=r->transmitter;x[1]=r->parent;x[2]=r->depth;x[3]=r->slot;
  put(x+4,r->boot,8);put(x+12,r->train,4);put(x+16,r->publication,4);
  put(x+20,r->capture_epoch,4);put(x+24,r->map_generation,4);put(x+28,r->bound_ns,4);
  for(unsigned i=0;i<p->count;i++){const struct r2_relay_correction *c=&p->correction[i];x=w+116+i*19;
    put(x,c->publication,4);put(x+4,c->event,2);x[6]=c->channel;put(x+7,c->ticks,8);put(x+15,c->generation,4);}
  for(unsigned i=0;i<4;i++) {
    put(w+154+i*8,tag("PTOBR2T2",w,154,keys[i]),8);
  }
  return 0;
}
int r2_relay_decode(struct r2_relay_packet *p,const uint8_t *ad,size_t size,unsigned recipient,const uint8_t rk[16],const uint8_t sk[16]) {
  if(!p||!ad||!rk||!sk||recipient>=4||size!=190||ad[0]!=189||ad[1]!=255||ad[2]!=255||ad[3]!=255)return-EINVAL;
  const uint8_t *w=ad+4;
  if(w[0]!='R'||w[1]!='2'||w[2]!=2||get(w+4,2)!=50)return-EINVAL;
  if(tag("PTOBR2O2",w+6,46,rk)!=get(w+52+recipient*8,8)||
    tag("PTOBR2T2",w,154,sk)!=get(w+154+recipient*8,8))return-EACCES;
  return r2_relay_parse_authenticated(p,ad);
}
int r2_relay_parse_authenticated(struct r2_relay_packet *p,const uint8_t *ad) {
  const uint8_t *w=ad+4;
  memset(p,0,sizeof(*p));p->count=w[3];const uint8_t *x=w+6;
  struct r2_relay_origin *o=&p->origin;o->root=x[0];o->relay=x[1];o->boot=get(x+2,8);o->source_epoch=(uint32_t)get(x+10,4);
  o->created_ns=get(x+14,8);o->expires_ns=get(x+22,8);o->frame_ns=get(x+30,8);
  uint64_t off=get(x+38,8);o->utc_offset_ns=off<=INT64_MAX?(int64_t)off:-1-(int64_t)(UINT64_MAX-off);
  struct r2_relay_route *r=&p->route;x=w+84;r->transmitter=x[0];r->parent=x[1];r->depth=x[2];r->slot=x[3];
  r->boot=get(x+4,8);r->train=(uint32_t)get(x+12,4);r->publication=(uint32_t)get(x+16,4);
  r->capture_epoch=(uint32_t)get(x+20,4);r->map_generation=(uint32_t)get(x+24,4);r->bound_ns=(uint32_t)get(x+28,4);
  for(unsigned i=0;i<2;i++){struct r2_relay_correction *c=&p->correction[i];x=w+116+i*19;
    c->publication=(uint32_t)get(x,4);c->event=(uint16_t)get(x+4,2);c->channel=x[6];c->ticks=get(x+7,8);c->generation=(uint32_t)get(x+15,4);}
  return r2_relay_validate(p);
}
