/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_rc8.h"
#include <errno.h>
#include <string.h>
static bool nonzero(const uint8_t *p,size_t n){
 uint8_t bits=0;
 for(size_t i=0;i<n;i++)bits|=p[i];
 return bits!=0;
}
bool rc8_scope_equal(const struct rc8_scope *a,const struct rc8_scope *b){
 return a&&b&&a->emitter==b->emitter&&a->emitter_boot==b->emitter_boot&&a->stream==b->stream&&
 a->root==b->root&&a->root_boot==b->root_boot&&a->source_epoch==b->source_epoch&&a->scale==b->scale&&a->kind==b->kind;
}
int rc8_scope_validate(const struct rc8_scope *s){
 if(!s||!s->emitter||!s->emitter_boot||!s->stream||!s->root||!s->root_boot||!s->source_epoch||s->scale>RC8_UTC||
 (s->kind!=RC8_OWN_GM&&s->kind!=RC8_SELECTED_BLE)||
 (s->kind==RC8_OWN_GM&&s->root!=s->emitter)||
 (s->kind==RC8_SELECTED_BLE&&s->root==s->emitter))return -EINVAL;
 return 0;
}
int rc8_metadata_encode(uint8_t w[RC8_META_BYTES],const struct rc8_scope *s,const struct r2_group_config *g){
 if(!w)return -EINVAL;
 memset(w,0,RC8_META_BYTES);
 int rc=rc8_scope_validate(s);
 if(rc)return rc;
 w[0]='R';w[1]='8';w[2]=RC8_WIRE_VERSION;w[3]=1;
 r2_group_put(w+4,s->emitter,8);r2_group_put(w+12,s->emitter_boot,8);r2_group_put(w+20,s->stream,8);
 r2_group_put(w+28,s->root,8);r2_group_put(w+36,s->root_boot,8);r2_group_put(w+44,s->source_epoch,4);
 w[48]=s->scale;w[49]=s->kind;
 rc=r2_group_mac(w+52,g,"PTOBR8M2",w,52);
 if(rc)memset(w,0,RC8_META_BYTES);
 return rc;
}
int rc8_context_id(uint8_t out[RC8_CONTEXT_BYTES],const struct rc8_scope *s,const struct r2_group_config *g){
 if(!out)return -EINVAL;
 uint8_t wire[RC8_META_BYTES];int rc=rc8_metadata_encode(wire,s,g);
 memset(out,0,RC8_CONTEXT_BYTES);
 if(!rc)memcpy(out,wire+52,RC8_CONTEXT_BYTES);
 return rc;
}
int rc8_metadata_decode(struct rc8_scope *s,uint8_t context[RC8_CONTEXT_BYTES],const uint8_t *w,size_t n,const struct r2_group_config *g){
 if(!s||!context)return -EINVAL;
 memset(s,0,sizeof *s);memset(context,0,RC8_CONTEXT_BYTES);
 if(!w||n!=RC8_META_BYTES||w[0]!='R'||w[1]!='8'||w[2]!=RC8_WIRE_VERSION||w[3]!=1||w[50]||w[51])return -EINVAL;
 if(!r2_group_verify(w+52,g,"PTOBR8M2",w,52))return -EACCES;
 s->emitter=r2_group_get(w+4,8);s->emitter_boot=r2_group_get(w+12,8);s->stream=r2_group_get(w+20,8);
 s->root=r2_group_get(w+28,8);s->root_boot=r2_group_get(w+36,8);s->source_epoch=r2_group_get(w+44,4);
 s->scale=w[48];s->kind=w[49];
 int rc=rc8_scope_validate(s);
 if(rc)memset(s,0,sizeof *s);else memcpy(context,w+52,RC8_CONTEXT_BYTES);
 return rc;
}
int rc8_packet_validate(const struct rc8_packet *p){
 if(!p||!p->sequence||!nonzero(p->context,RC8_CONTEXT_BYTES))return -EINVAL;
 const struct rc8_correction *c=&p->correction;
 if(p->corrected){
  if(!c->sequence||c->sequence>=p->sequence||!c->map_generation||c->created_ns>c->event_ns||c->event_ns>=c->expires_ns)return -EINVAL;
 }else if(c->sequence||c->map_generation||c->event_ns||c->created_ns||c->expires_ns||c->map_age_us||c->upstream_bound_ns)return -EINVAL;
 return 0;
}
int rc8_encode(uint8_t w[RC8_BYTES],const struct rc8_packet *p,const struct r2_group_config *g){
 if(!w)return -EINVAL;
 memset(w,0,RC8_BYTES);
 int rc=rc8_packet_validate(p);
 if(rc)return rc;
 w[0]='R';w[1]='8';w[2]=RC8_WIRE_VERSION;w[3]=2;
 memcpy(w+4,p->context,RC8_CONTEXT_BYTES);r2_group_put(w+20,p->sequence,4);
 const struct rc8_correction *c=&p->correction;
 r2_group_put(w+24,c->sequence,4);r2_group_put(w+28,c->map_generation,4);r2_group_put(w+32,c->event_ns,8);
 r2_group_put(w+40,c->created_ns,8);r2_group_put(w+48,c->expires_ns,8);
 r2_group_put(w+56,c->map_age_us,4);r2_group_put(w+60,c->upstream_bound_ns,4);
 rc=r2_group_mac(w+64,g,"PTOBR8T2",w,64);
 if(rc)memset(w,0,RC8_BYTES);
 return rc;
}
int rc8_decode(struct rc8_packet *p,const uint8_t *w,size_t n,const struct r2_group_config *g){
 if(!p)return -EINVAL;
 memset(p,0,sizeof *p);
 if(!w||n!=RC8_BYTES||w[0]!='R'||w[1]!='8'||w[2]!=RC8_WIRE_VERSION||w[3]!=2)return -EINVAL;
 if(!r2_group_verify(w+64,g,"PTOBR8T2",w,64))return -EACCES;
 memcpy(p->context,w+4,RC8_CONTEXT_BYTES);p->sequence=r2_group_get(w+20,4);
 struct rc8_correction *c=&p->correction;
 c->sequence=r2_group_get(w+24,4);c->map_generation=r2_group_get(w+28,4);c->event_ns=r2_group_get(w+32,8);
 c->created_ns=r2_group_get(w+40,8);c->expires_ns=r2_group_get(w+48,8);
 c->map_age_us=r2_group_get(w+56,4);c->upstream_bound_ns=r2_group_get(w+60,4);p->corrected=c->sequence!=0;
 int rc=rc8_packet_validate(p);
 if(rc)memset(p,0,sizeof *p);
 return rc;
}
