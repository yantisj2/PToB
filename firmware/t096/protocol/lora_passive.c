/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_passive.h"
#include <errno.h>
#include <string.h>
#include <tinycrypt/sha256.h>

size_t lp_size(unsigned k){
 switch(k){case LP_DISC:return LP_DISC_BYTES;case LP_META:return LP_META_BYTES;
 case LP_SYNC:return LP_SYNC_BYTES;case LP_FOLLOW:return LP_FOLLOW_BYTES;default:return 0;}
}
static bool any(const uint8_t *p,size_t n){uint8_t b=0;for(size_t i=0;i<n;i++)b|=p[i];return b!=0;}
static bool scope_valid(const struct lp_scope *s){
 /* Revision one supports own-reference sources only, never finite-grant forwarding. */
 return s&&s->emitter&&s->boot&&s->root==s->emitter&&s->root_boot&&
 !s->parent&&!s->parent_boot&&s->epoch&&s->stream&&s->seed;
}
bool lp_scope_equal(const struct lp_scope *a,const struct lp_scope *b){
 return a&&b&&a->emitter==b->emitter&&a->boot==b->boot&&a->root==b->root&&
 a->root_boot==b->root_boot&&a->parent==b->parent&&a->parent_boot==b->parent_boot&&
 a->epoch==b->epoch&&a->stream==b->stream&&a->seed==b->seed;
}
static void immutable(uint8_t w[64],const struct lp_scope *s,unsigned schedule){
 memset(w,0,64);w[0]='L';w[1]=LP_VERSION;w[2]=LP_META;w[3]=(uint8_t)schedule;
 const uint64_t ids[]={s->emitter,s->boot,s->root,s->root_boot,s->parent,s->parent_boot};
 for(unsigned i=0;i<6;i++)r2_group_put(w+4+i*8,ids[i],8);
 r2_group_put(w+52,s->epoch,4);r2_group_put(w+56,s->stream,4);r2_group_put(w+60,s->seed,4);
}
int lp_context(uint8_t out[16],const struct lp_scope *s,unsigned schedule,const struct r2_group_config *g){
 if(!out)return -EINVAL;
 memset(out,0,16);if(schedule!=LP_SCHEDULE||!scope_valid(s))return -EINVAL;
 uint8_t w[64];immutable(w,s,schedule);return r2_group_mac(out,g,"PTL2CTX1",w,sizeof w);
}
static bool valid(const struct lp_packet *p){
 if(!p||p->schedule!=LP_SCHEDULE||!lp_size(p->kind)||p->cursor>UINT32_MAX-2)return false;
 if(p->kind==LP_META&&(!scope_valid(&p->scope)||!p->serial||!p->utc_ns||!(r2_tz_valid(p->tz)||(!p->tz.id&&p->tz.offset_min==INT16_MIN))))return false;
 if(p->kind==LP_FOLLOW&&!p->utc_ns)return false;
 if(p->kind==LP_META||p->kind==LP_DISC){
  if(p->has_pair?(!p->time_valid||!p->delay_10ms||p->delay_10ms>400):(p->delay_10ms!=0))return false;
 }else if(p->has_pair||p->time_valid||p->delay_10ms)return false;
 return true;
}
static int small_mac(uint8_t out[16],const struct r2_group_config *g,const char domain[8],
                      const uint8_t context[16],const uint8_t *w,size_t n){
 if(!context||!any(context,16)||n>16)return -EINVAL;
 uint8_t body[32];memcpy(body,context,16);memcpy(body+16,w,n);
 return r2_group_mac(out,g,domain,body,16+n);
}
static const char domains[4][9]={"PTL2DIS1","PTL2MET1","PTL2SYN1","PTL2FOL1"};
int lp_encode(uint8_t *w,size_t capacity,const struct lp_packet *p,const uint8_t context[16],const struct r2_group_config *g){
 if(!w||!valid(p)||capacity<lp_size(p->kind))return -EINVAL;
 size_t n=lp_size(p->kind),body=n-16;memset(w,0,n);
 if(p->kind==LP_META)immutable(w,&p->scope,p->schedule);
 w[0]='L';w[1]=LP_VERSION;w[2]=p->kind|(p->has_pair?0x80:0)|(p->time_valid?0x40:0);w[3]=p->schedule;
 if(p->kind==LP_META){
  r2_group_put(w+64,p->cursor,4);r2_group_put(w+68,p->serial,4);r2_group_put(w+72,p->utc_ns,8);
  r2_tz_put(w+80,p->tz);r2_group_put(w+84,p->delay_10ms,2);
 }else{r2_group_put(w+4,p->cursor,4);
  if(p->kind==LP_DISC)r2_group_put(w+8,p->delay_10ms,2);
  if(p->kind==LP_FOLLOW)r2_group_put(w+8,p->utc_ns,8);
 }
 int rc=p->kind==LP_META?r2_group_mac(w+body,g,domains[p->kind-1],w,body):small_mac(w+body,g,domains[p->kind-1],context,w,body);
 if(rc)memset(w,0,n);
 return rc;
}
int lp_decode(struct lp_packet *p,const uint8_t *w,size_t n,const uint8_t context[16],const struct r2_group_config *g){
 if(!p)return -EINVAL;
 memset(p,0,sizeof *p);
 if(!w||n<4||w[0]!='L'||w[1]!=LP_VERSION||w[3]!=LP_SCHEDULE||(w[2]&0x30))return -EINVAL;
 unsigned k=w[2]&15;if(n!=lp_size(k)||!n)return -EINVAL;
 size_t body=n-16;uint8_t wanted[16];
 int rc=k==LP_META?r2_group_mac(wanted,g,domains[k-1],w,body):small_mac(wanted,g,domains[k-1],context,w,body);
 if(rc)return rc;
 uint8_t diff=0;for(unsigned i=0;i<16;i++)diff|=wanted[i]^w[body+i];if(diff)return -EACCES;
 /* Reject canonical-shape errors before writing the caller's output. Avoid
  * a second full packet on the native2KiB receiver/crypto call chain. */
 if(k==LP_META){uint8_t canonical[4];r2_tz_put(canonical,r2_tz_get(w+80));
  if(memcmp(canonical,w+80,4)||any(w+86,10))return -EINVAL;}
 if(k==LP_DISC&&any(w+10,2))return -EINVAL;
 p->kind=(uint8_t)k;p->schedule=w[3];p->has_pair=!!(w[2]&0x80);p->time_valid=!!(w[2]&0x40);
 if(k==LP_META){
  p->scope=(struct lp_scope){.emitter=r2_group_get(w+4,8),.boot=r2_group_get(w+12,8),.root=r2_group_get(w+20,8),
   .root_boot=r2_group_get(w+28,8),.parent=r2_group_get(w+36,8),.parent_boot=r2_group_get(w+44,8),
   .epoch=r2_group_get(w+52,4),.stream=r2_group_get(w+56,4),.seed=r2_group_get(w+60,4)};
  p->cursor=r2_group_get(w+64,4);p->serial=r2_group_get(w+68,4);p->utc_ns=r2_group_get(w+72,8);
  p->tz=r2_tz_get(w+80);p->delay_10ms=r2_group_get(w+84,2);
 }else{p->cursor=r2_group_get(w+4,4);
  if(k==LP_DISC)p->delay_10ms=r2_group_get(w+8,2);
  if(k==LP_FOLLOW)p->utc_ns=r2_group_get(w+8,8);
 }
 if(!valid(p)){memset(p,0,sizeof *p);return -EINVAL;}
 return 0;
}

int lp_hop_order(uint8_t out[64],uint32_t seed){
 if(!out||!seed)return -EINVAL;
 for(unsigned i=0;i<64;i++)out[i]=(uint8_t)i;
 uint32_t counter=0;uint8_t input[23],digest[32];
 memcpy(input,"PTOB-HOP-MODEL\0",15);r2_group_put(input+15,seed,4);
 for(unsigned i=63;i;i--){uint32_t draw;uint64_t ceiling=(UINT64_C(1)<<32)/(i+1)*(i+1);
  do{r2_group_put(input+19,counter++,4);struct tc_sha256_state_struct h;
   if(!tc_sha256_init(&h)||!tc_sha256_update(&h,input,sizeof input)||!tc_sha256_final(digest,&h))return -EIO;
   draw=(uint32_t)r2_group_get(digest,4);
  }while(draw>=ceiling);
  unsigned j=draw%(i+1);uint8_t v=out[i];out[i]=out[j];out[j]=v;
 }
 return 0;
}
unsigned lp_channel(const uint8_t order[64],uint32_t cursor){return order[(cursor%64+cursor/64)%64];}
