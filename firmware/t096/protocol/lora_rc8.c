/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_rc8.h"
#include <errno.h>
#include <string.h>
static bool nonzero(const uint8_t *p,size_t n){uint8_t b=0;for(size_t i=0;i<n;i++)b|=p[i];return b!=0;}
bool rc8_scope_equal(const struct rc8_scope *a,const struct rc8_scope *b){
 return a&&b&&a->emitter==b->emitter&&a->emitter_boot==b->emitter_boot&&a->stream==b->stream&&
 a->root==b->root&&a->root_boot==b->root_boot&&a->parent==b->parent&&a->parent_boot==b->parent_boot&&
 a->source_epoch==b->source_epoch&&a->scale==b->scale&&a->kind==b->kind;
}
int rc8_scope_validate(const struct rc8_scope *s){
 if(!s||!s->emitter||!s->emitter_boot||!s->stream||!s->root||!s->root_boot||!s->source_epoch||s->scale>RC8_UTC||
 (s->kind!=RC8_OWN_GM&&s->kind!=RC8_SELECTED_BLE)||(!s->parent!=!s->parent_boot)||
 (s->kind==RC8_OWN_GM&&s->root!=s->emitter)||(s->kind==RC8_SELECTED_BLE&&s->root==s->emitter))return -EINVAL;
 return 0;
}
static void immutable(uint8_t *w,const struct rc8_scope *s){
 w[0]='R';w[1]='8';w[2]=RC8_WIRE_VERSION;w[3]=1;
 r2_group_put(w+4,s->emitter,8);r2_group_put(w+12,s->emitter_boot,8);r2_group_put(w+20,s->stream,8);
 r2_group_put(w+28,s->root,8);r2_group_put(w+36,s->root_boot,8);r2_group_put(w+44,s->parent,8);
 r2_group_put(w+52,s->parent_boot,8);r2_group_put(w+60,s->source_epoch,4);w[64]=s->scale;w[65]=s->kind;
}
static int diagnostic_scope(const struct rc8_scope *s){
 if(s&&s->kind==RC8_NO_SOURCE&&s->emitter&&s->emitter_boot&&s->stream&&
 !s->root&&!s->root_boot&&!s->parent&&!s->parent_boot&&!s->source_epoch&&s->scale==RC8_RELATIVE)return 0;
 return rc8_scope_validate(s);
}
static int companion_context(uint8_t out[RC8_CONTEXT_BYTES],const struct rc8_scope *s,const struct r2_group_config *g){
 if(!out)return -EINVAL;
 memset(out,0,RC8_CONTEXT_BYTES);int rc=diagnostic_scope(s);if(rc)return rc;
 uint8_t w[66];immutable(w,s);return r2_group_mac(out,g,"PTOBR8C3",w,sizeof w);
}
int rc8_context_id(uint8_t out[RC8_CONTEXT_BYTES],const struct rc8_scope *s,const struct r2_group_config *g){
 if(!out)return -EINVAL;
 memset(out,0,RC8_CONTEXT_BYTES);int rc=rc8_scope_validate(s);if(rc)return rc;
 return companion_context(out,s,g);
}
int rc8_companion_encode(uint8_t w[RC8_META_BYTES],const struct rc8_companion *p,const struct r2_group_config *g){
 if(!w)return -EINVAL;
 memset(w,0,RC8_META_BYTES);
 if(!p||diagnostic_scope(&p->scope)||(p->scope.kind==RC8_NO_SOURCE&&p->time_valid)||!p->sequence||(p->quality.known&~8191u)||
 (p->quality.values&~15u)||(p->quality.values&~p->quality.known)||(p->events&~511u))return -EINVAL;
 immutable(w,&p->scope);r2_group_put(w+66,p->sequence,4);r2_group_put(w+70,p->snapshot_age_ms,4);
 r2_group_put(w+74,p->timing_sequence,4);r2_group_put(w+78,p->events,2);
 const struct rc8_quality *q=&p->quality;r2_group_put(w+80,q->known,2);r2_group_put(w+82,q->values,2);
 w[84]=q->role;w[85]=q->hop;w[86]=q->parent_class;w[87]=q->parent_accuracy;
 r2_group_put(w+88,q->parent_bound_ns,4);r2_group_put(w+92,q->correction_age_ms,4);r2_group_put(w+96,q->map_life_ms,4);
 r2_group_put(w+100,(uint32_t)q->rate_ppb,4);r2_group_put(w+104,q->residual_ns,4);
 w[108]=q->losses;w[109]=q->recoveries;w[111]=p->time_valid;
 int rc=r2_group_mac(w+112,g,"PTOBR8D3",w,112);if(rc)memset(w,0,RC8_META_BYTES);return rc;
}
int rc8_companion_decode(struct rc8_companion *p,uint8_t id[RC8_CONTEXT_BYTES],const uint8_t *w,size_t n,const struct r2_group_config *g){
 if(!p||!id)return -EINVAL;
 memset(p,0,sizeof *p);memset(id,0,RC8_CONTEXT_BYTES);
 if(!w||n!=RC8_META_BYTES||w[0]!='R'||w[1]!='8'||w[2]!=RC8_WIRE_VERSION||w[3]!=1||w[110]||w[111]>1)return -EINVAL;
 if(!r2_group_verify(w+112,g,"PTOBR8D3",w,112))return -EACCES;
 struct rc8_scope *s=&p->scope;s->emitter=r2_group_get(w+4,8);s->emitter_boot=r2_group_get(w+12,8);s->stream=r2_group_get(w+20,8);
 s->root=r2_group_get(w+28,8);s->root_boot=r2_group_get(w+36,8);s->parent=r2_group_get(w+44,8);s->parent_boot=r2_group_get(w+52,8);
 s->source_epoch=r2_group_get(w+60,4);s->scale=w[64];s->kind=w[65];
 p->sequence=r2_group_get(w+66,4);p->snapshot_age_ms=r2_group_get(w+70,4);p->timing_sequence=r2_group_get(w+74,4);p->events=r2_group_get(w+78,2);
 struct rc8_quality *q=&p->quality;q->known=r2_group_get(w+80,2);q->values=r2_group_get(w+82,2);
 q->role=w[84];q->hop=w[85];q->parent_class=w[86];q->parent_accuracy=w[87];q->parent_bound_ns=r2_group_get(w+88,4);
 q->correction_age_ms=r2_group_get(w+92,4);q->map_life_ms=r2_group_get(w+96,4);q->rate_ppb=(int32_t)(uint32_t)r2_group_get(w+100,4);
 q->residual_ns=r2_group_get(w+104,4);q->losses=w[108];q->recoveries=w[109];p->time_valid=w[111];
 int rc=diagnostic_scope(s);
 if((s->kind==RC8_NO_SOURCE&&p->time_valid)||!p->sequence||(q->known&~8191u)||(q->values&~15u)||(q->values&~q->known)||(p->events&~511u))rc=-EINVAL;
 if(!rc)rc=companion_context(id,s,g);
 if(rc){memset(p,0,sizeof *p);memset(id,0,RC8_CONTEXT_BYTES);}return rc;
}
/* Compatibility names describe the wire3 diagnostic packet, never wire2. */
int rc8_metadata_encode(uint8_t w[RC8_META_BYTES],const struct rc8_scope *s,const struct r2_group_config *g){
 if(!s)return -EINVAL;
 struct rc8_companion p={.scope=*s,.sequence=1,.time_valid=true};return rc8_companion_encode(w,&p,g);
}
int rc8_metadata_decode(struct rc8_scope *s,uint8_t id[RC8_CONTEXT_BYTES],const uint8_t *w,size_t n,const struct r2_group_config *g){
 if(!s)return -EINVAL;
 struct rc8_companion p={0};int rc=rc8_companion_decode(&p,id,w,n,g);*s=p.scope;return rc;
}
int rc8_packet_validate(const struct rc8_packet *p){
 if(!p||!p->sequence||!p->root||!p->source_epoch||p->scale>RC8_UTC||
 (p->kind!=RC8_OWN_GM&&p->kind!=RC8_SELECTED_BLE)||!nonzero(p->context,RC8_CONTEXT_BYTES))return -EINVAL;
 const struct rc8_correction *c=&p->correction;
 if(p->corrected){
  if(!c->sequence||c->sequence>=p->sequence||!c->map_generation||c->created_ns>c->event_ns||c->event_ns>=c->expires_ns||
   (c->event_ns-c->created_ns)/1000>UINT32_MAX||(c->expires_ns-c->event_ns)/1000>UINT32_MAX||
   (c->expires_ns-c->event_ns)/1000==0)return -EINVAL;
 }else if(c->sequence||c->map_generation||c->event_ns||c->created_ns||c->expires_ns||c->map_age_us||c->upstream_bound_ns)return -EINVAL;
 return 0;
}
int rc8_encode(uint8_t w[RC8_BYTES],const struct rc8_packet *p,const struct r2_group_config *g){
 if(!w)return -EINVAL;
 memset(w,0,RC8_BYTES);int rc=rc8_packet_validate(p);if(rc)return rc;
 w[0]='R';w[1]='8';w[2]=RC8_WIRE_VERSION;w[3]=(uint8_t)(2|(p->scale<<2)|(p->kind<<3));
 memcpy(w+4,p->context,16);r2_group_put(w+20,p->root,8);r2_group_put(w+28,p->source_epoch,4);r2_group_put(w+32,p->sequence,4);
 const struct rc8_correction *c=&p->correction;r2_group_put(w+36,c->sequence,4);r2_group_put(w+40,c->event_ns,8);
 r2_group_put(w+48,c->map_generation,4);r2_group_put(w+52,(c->event_ns-c->created_ns)/1000,4);
 r2_group_put(w+56,(c->expires_ns-c->event_ns)/1000,4);r2_group_put(w+60,c->map_age_us,4);
 rc=r2_group_mac(w+64,g,"PTOBR8T3",w,64);if(rc)memset(w,0,RC8_BYTES);return rc;
}
int rc8_decode(struct rc8_packet *p,const uint8_t *w,size_t n,const struct r2_group_config *g){
 if(!p)return -EINVAL;
 memset(p,0,sizeof *p);
 if(!w||n!=RC8_BYTES||w[0]!='R'||w[1]!='8'||w[2]!=RC8_WIRE_VERSION||(w[3]&~30u)||(w[3]&3u)!=2)return -EINVAL;
 if(!r2_group_verify(w+64,g,"PTOBR8T3",w,64))return -EACCES;
 memcpy(p->context,w+4,16);p->root=r2_group_get(w+20,8);p->source_epoch=r2_group_get(w+28,4);
 p->sequence=r2_group_get(w+32,4);p->scale=(w[3]>>2)&1u;p->kind=(w[3]>>3)&3u;
 struct rc8_correction *c=&p->correction;c->sequence=r2_group_get(w+36,4);c->event_ns=r2_group_get(w+40,8);
 c->map_generation=r2_group_get(w+48,4);uint64_t age=r2_group_get(w+52,4)*1000,remaining=r2_group_get(w+56,4)*1000;
 if(age>c->event_ns||remaining>UINT64_MAX-c->event_ns){memset(p,0,sizeof *p);return -EINVAL;}
 c->created_ns=c->event_ns-age;c->expires_ns=c->event_ns+remaining;c->map_age_us=r2_group_get(w+60,4);
 p->corrected=c->sequence!=0;if(p->corrected)c->upstream_bound_ns=RC8_UNKNOWN;
 int rc=rc8_packet_validate(p);if(rc)memset(p,0,sizeof *p);return rc;
}
