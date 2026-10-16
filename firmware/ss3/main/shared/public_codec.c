/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <limits.h>
#include <string.h>
#include "public_codec.h"
#include "p256_native.h"

/* The revision byte is included in the ninth domain byte. */
static const uint8_t full_domains[4][9]={
  {0x50,0x54,0x4f,0x42,0x31,0x32,0x52,0x54,1},
  {0x50,0x54,0x4f,0x42,0x31,0x32,0x45,0x54,1},
  {0x50,0x54,0x4f,0x42,0x31,0x32,0x52,0x47,1},
  {0x50,0x54,0x4f,0x42,0x31,0x32,0x45,0x47,1}};
static bool roster(uint8_t n){return n==0||n==1||n==2||n==4||n==5;}
static void put(uint8_t *p,uint64_t value,unsigned n){for(unsigned i=0;i<n;i++){p[i]=(uint8_t)value;value>>=8;}}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static void prefix(uint8_t *p,uint8_t kind){p[0]=0xff;p[1]=0xff;p[2]=12;p[3]=kind;}
static int framing(const uint8_t *ad,size_t n)
{
  if(!ad||n<6||n>V12_PUBLIC_MAX_AD||ad[0]!=n-1||ad[1]!=0xff)return -EMSGSIZE;
  if(ad[2]!=0xff||ad[3]!=0xff||ad[4]!=12)return -EPROTO;
  return 0;
}
static int timing_valid(const struct v12_public_timing *r)
{
  if(!r||!roster(r->publisher)||!roster(r->root)||r->correction_count>2)return -EINVAL;
  if((r->flags&~7u)||!(r->flags&V12_PUBLIC_SOURCE_QUALIFIED))return -EPROTO;
  if(r->kind==V12_PUBLIC_ROOT_TIMING){
    if(r->publisher!=r->root||(r->flags&V12_PUBLIC_DEPTH_ONE))return -EPROTO;
  }else if(r->kind==V12_PUBLIC_RELAY_TIMING){
    if(r->publisher==r->root||!(r->flags&V12_PUBLIC_DEPTH_ONE))return -EPROTO;
  }else return -EPROTO;
  if(r->source_age_ceil_ms>V12_PUBLIC_MAX_AGE_MS||r->run_phase_125us>=8000||
     r->source_second>UINT64_MAX/1000000000u||r->payload_generation==UINT32_MAX)return -ERANGE;
  for(unsigned i=0;i<r->correction_count;i++){
    const struct v12_public_correction *c=&r->corrections[i];
    if(c->source_age_ceil_ms>V12_PUBLIC_MAX_AGE_MS||c->uncertainty_ceil_ns==UINT16_MAX||
       c->event==UINT32_MAX||c->payload_generation==UINT32_MAX)return -ERANGE;
    if(i && c->event==r->corrections[0].event)return -EPROTO;
  }
  return 0;
}
int v12_public_timing_body(uint8_t out[116],size_t *length,const struct v12_public_timing *r)
{
  if(!out||!length)return -EINVAL;
  int rc=timing_valid(r);if(rc)return rc;
  unsigned h=r->kind==V12_PUBLIC_ROOT_TIMING?52:76,n=h+20*r->correction_count;
  memset(out,0,n);uint8_t *p=out;
  prefix(p,r->kind);p[4]=r->publisher;p[5]=r->root;p[6]=r->flags;p[7]=r->correction_count;
  put(p+8,r->root_boot,8);put(p+16,r->root_train,8);put(p+24,r->source_epoch,4);
  put(p+28,r->source_second,8);put(p+36,r->root_permit_id,8);put(p+44,r->payload_generation,4);
  put(p+48,r->source_age_ceil_ms,2);put(p+50,r->run_phase_125us,2);
  if(h==76){put(p+52,r->emitter_boot,8);put(p+60,r->emitter_train,8);put(p+68,r->upstream_original_id,8);}
  for(unsigned i=0;i<r->correction_count;i++){
    uint8_t *c=p+h+20*i;const struct v12_public_correction *s=&r->corrections[i];
    put(c,s->event,4);put(c+4,s->payload_generation,4);put(c+8,s->UTC_ns,8);
    put(c+16,s->source_age_ceil_ms,2);put(c+18,s->uncertainty_ceil_ns,2);
  }
  *length=n;return 0;
}
int v12_public_timing_pack(uint8_t out[190],size_t *length,const struct v12_public_timing *r)
{
  if(!out||!length||!r||!v12_p256_signature_canonical(r->signature))return -EINVAL;
  size_t body;int rc=v12_public_timing_body(out+2,&body,r);if(rc)return rc;
  *length=body+66;out[0]=(uint8_t)(*length-1);out[1]=0xff;memcpy(out+2+body,r->signature,64);return 0;
}
int v12_public_timing_unpack(struct v12_public_timing *r,const uint8_t *ad,size_t n)
{
  if(!r)return -EINVAL;
  memset(r,0,sizeof(*r));
  int rc=framing(ad,n);if(rc)return rc;
  const uint8_t *p=ad+2;unsigned h;
  if(p[3]==V12_PUBLIC_ROOT_TIMING)h=52;
  else if(p[3]==V12_PUBLIC_RELAY_TIMING)h=76;
  else return -EPROTO;
  if(n<h+66||p[7]>2||n!=h+20*p[7]+66)return -EMSGSIZE;
  r->kind=p[3];r->publisher=p[4];r->root=p[5];r->flags=p[6];r->correction_count=p[7];
  r->root_boot=get(p+8,8);r->root_train=get(p+16,8);r->source_epoch=get(p+24,4);
  r->source_second=get(p+28,8);r->root_permit_id=get(p+36,8);r->payload_generation=get(p+44,4);
  r->source_age_ceil_ms=get(p+48,2);r->run_phase_125us=get(p+50,2);
  if(h==76){r->emitter_boot=get(p+52,8);r->emitter_train=get(p+60,8);r->upstream_original_id=get(p+68,8);}
  for(unsigned i=0;i<r->correction_count;i++){
    const uint8_t *c=p+h+20*i;struct v12_public_correction *s=&r->corrections[i];
    s->event=get(c,4);s->payload_generation=get(c+4,4);s->UTC_ns=get(c+8,8);
    s->source_age_ceil_ms=get(c+16,2);s->uncertainty_ceil_ns=get(c+18,2);
  }
  memcpy(r->signature,p+h+20*r->correction_count,64);
  rc=timing_valid(r);if(rc)return rc;
  return v12_p256_signature_canonical(r->signature)?0:-EINVAL;
}
static int grant_valid(const struct v12_public_grant *r)
{
  if(!r||!roster(r->issuer)||!roster(r->publisher)||!roster(r->root))return -EINVAL;
  if(r->flags!=1||r->algorithm!=1)return -EPROTO;
  if(r->kind==V12_PUBLIC_ROOT_GRANT){if(r->depth||r->publisher!=r->root)return -EPROTO;}
  else if(r->kind==V12_PUBLIC_RELAY_GRANT){if(r->depth!=1||r->publisher==r->root)return -EPROTO;}
  else return -EPROTO;
  if(!r->duration_ceil_100us||r->duration_ceil_100us>30000||
     r->expires_UTC_ns<(uint64_t)r->duration_ceil_100us*100000u)return -ERANGE;
  return 0;
}
int v12_public_grant_body(uint8_t out[124],const struct v12_public_grant *r)
{
  if(!out)return -EINVAL;
  int rc=grant_valid(r);if(rc)return rc;
  memset(out,0,124);uint8_t *p=out;
  prefix(p,r->kind);p[4]=r->issuer;p[5]=r->publisher;p[6]=r->root;p[7]=r->depth;p[8]=r->flags;p[9]=r->algorithm;
  put(p+10,r->coordinator_boot,8);
  if(r->kind==V12_PUBLIC_ROOT_GRANT){
    put(p+18,r->root_boot,8);put(p+26,r->root_train,8);put(p+34,r->source_epoch,4);
    put(p+38,r->root_permit_id,8);put(p+46,r->expires_UTC_ns,8);put(p+54,r->config_epoch,4);
  }else{
    put(p+18,r->emitter_boot,8);put(p+26,r->emitter_train,8);put(p+34,r->root_permit_id,8);
    put(p+42,r->relay_grant_id,8);put(p+50,r->expires_UTC_ns,8);
  }
  put(p+58,r->duration_ceil_100us,2);memcpy(p+60,r->publisher_public_key_XY,64);return 0;
}
int v12_public_grant_pack(uint8_t out[190],const struct v12_public_grant *r)
{
  if(!out||!r||!v12_p256_signature_canonical(r->signature))return -EINVAL;
  int rc=v12_public_grant_body(out+2,r);if(rc)return rc;
  out[0]=189;out[1]=0xff;memcpy(out+126,r->signature,64);return 0;
}
int v12_public_grant_unpack(struct v12_public_grant *r,const uint8_t *ad,size_t n)
{
  if(!r)return -EINVAL;
  memset(r,0,sizeof(*r));int rc=framing(ad,n);if(rc)return rc;
  if(n!=190)return -EMSGSIZE;
  const uint8_t *p=ad+2;
  r->kind=p[3];r->issuer=p[4];r->publisher=p[5];r->root=p[6];r->depth=p[7];r->flags=p[8];r->algorithm=p[9];
  r->coordinator_boot=get(p+10,8);
  if(r->kind==V12_PUBLIC_ROOT_GRANT){
    r->root_boot=get(p+18,8);r->root_train=get(p+26,8);r->source_epoch=get(p+34,4);
    r->root_permit_id=get(p+38,8);r->expires_UTC_ns=get(p+46,8);r->config_epoch=get(p+54,4);
  }else if(r->kind==V12_PUBLIC_RELAY_GRANT){
    r->emitter_boot=get(p+18,8);r->emitter_train=get(p+26,8);r->root_permit_id=get(p+34,8);
    r->relay_grant_id=get(p+42,8);r->expires_UTC_ns=get(p+50,8);
  }else return -EPROTO;
  r->duration_ceil_100us=get(p+58,2);memcpy(r->publisher_public_key_XY,p+60,64);memcpy(r->signature,p+124,64);
  rc=grant_valid(r);if(rc)return rc;
  return v12_p256_signature_canonical(r->signature)?0:-EINVAL;
}
int v12_public_auth_view(struct v12_public_auth_view *out,const uint8_t *ad,size_t n)
{
  if(!out)return -EINVAL;
  memset(out,0,sizeof(*out));int index,rc=framing(ad,n);if(rc)return rc;
  if(ad[5]==V12_PUBLIC_ROOT_TIMING||ad[5]==V12_PUBLIC_RELAY_TIMING){
    struct v12_public_timing record;rc=v12_public_timing_unpack(&record,ad,n);index=ad[5]-1;
  }else{
    struct v12_public_grant record;rc=v12_public_grant_unpack(&record,ad,n);index=ad[5]==129?2:3;
  }
  if(rc)return rc;
  out->domain=full_domains[index];out->domain_bytes=sizeof(full_domains[index]);
  out->body=ad+2;out->body_bytes=n-66;out->signature=ad+n-64;
  return 0;
}
const uint8_t *v12_public_domain(uint8_t kind,size_t *length)
{
  if(!length)return NULL;
  unsigned i;
  if(kind==1)i=0;else if(kind==2)i=1;else if(kind==129)i=2;else if(kind==130)i=3;
  else{*length=0;return NULL;}
  *length=9;return full_domains[i];
}
int v12_public_duration(uint64_t created,uint64_t expires,uint16_t *units)
{
  if(!units||expires<=created||expires-created>3000000000u)return -ERANGE;
  uint64_t delta=expires-created;*units=(uint16_t)(delta/100000u+(delta%100000u!=0));return 0;
}
int v12_public_created_bounds(const struct v12_public_grant *g,uint64_t *lower,uint64_t *upper)
{
  if(!lower||!upper)return -EINVAL;
  int rc=grant_valid(g);if(rc)return rc;
  *lower=g->expires_UTC_ns-(uint64_t)g->duration_ceil_100us*100000u;
  *upper=*lower+100000u;return 0;
}
int v12_public_parent_match(const struct v12_public_grant *r,const struct v12_public_grant *e)
{
  int rc=grant_valid(r);if(rc)return rc;rc=grant_valid(e);if(rc)return rc;
  if(r->kind!=129||e->kind!=130||r->issuer!=e->issuer||r->coordinator_boot!=e->coordinator_boot||
     r->root!=e->root||r->root_permit_id!=e->root_permit_id||e->expires_UTC_ns>r->expires_UTC_ns)return -EPROTO;
  return 0;
}
int v12_public_timing_scope(const struct v12_public_timing *t,const struct v12_public_grant *r,
                            const struct v12_public_grant *e)
{
  int rc=timing_valid(t);if(rc)return rc;rc=grant_valid(r);if(rc)return rc;
  if(r->kind!=129||t->root!=r->root||t->root_boot!=r->root_boot||t->root_train!=r->root_train||
     t->source_epoch!=r->source_epoch||t->root_permit_id!=r->root_permit_id)return -EPROTO;
  if(t->kind==1)return t->publisher==r->publisher?0:-EPROTO;
  rc=v12_public_parent_match(r,e);if(rc)return rc;
  return t->publisher==e->publisher&&t->emitter_boot==e->emitter_boot&&t->emitter_train==e->emitter_train?0:-EPROTO;
}
int v12_public_freshness(const struct v12_public_grant *g,bool verified,bool trusted,uint64_t lower,uint64_t upper)
{
  if(!verified||!trusted)return -EACCES;
  uint64_t created_lower,created_upper;int rc=v12_public_created_bounds(g,&created_lower,&created_upper);if(rc)return rc;
  /* Rounding gives a creation interval, not an exact creation timestamp.
   * Require current bound after its upper endpoint to avoid future issuance. */
  return lower<=upper&&lower>=created_upper&&upper<g->expires_UTC_ns?0:-ESTALE;
}
