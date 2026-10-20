/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_source.h"
#include <limits.h>
#include <string.h>
static bool translated(uint64_t ns,int64_t offset,uint64_t *out){
 if(offset<0||ns>(uint64_t)INT64_MAX-(uint64_t)offset)return false;
 *out=ns+(uint64_t)offset;return true;
}
bool r2_lora_source_own(struct rc8_export *out,const struct r2_source_clock *s,struct rc8_scope scope,
 struct r2_capture now,uint64_t until){
 if(!out)return false;
 memset(out,0,sizeof *out);uint64_t ns,generation,expiry;
 if(!s||scope.kind!=RC8_OWN_GM||rc8_scope_validate(&scope)||scope.scale!=RC8_UTC||!s->utc_bound||
 s->utc_source_epoch!=scope.source_epoch||!r2_source_clock_read(s,now,&ns,&generation)||
 !r2_source_clock_expiry(s,now,&expiry)||!generation||generation>UINT32_MAX||until<=now.tick)return false;
 const struct r2_source_map *m=NULL;
 for(unsigned i=0;i<s->count&&i<R2_SOURCE_MAP_HISTORY;i++)if(s->maps[i].generation==generation)m=&s->maps[i];
 if(!m||m->until<=now.tick)return false;
 if(until>m->until)until=m->until;
 uint64_t created,expires,anchor;
 if(!translated(m->anchor_ns,s->utc_offset_ns,&anchor)||!translated(m->anchor_ns,s->utc_offset_ns,&created)||
 !translated(expiry,s->utc_offset_ns,&expires))return false;
 *out=(struct rc8_export){.scope=scope,.from_tick=m->from,.until_tick=until,.anchor_tick=m->from,
 .anchor_ns=anchor,.capture_epoch=now.epoch,.created_ns=created,.expires_ns=expires,
 .map_generation=(uint32_t)generation,.upstream_bound_ns=RC8_UNKNOWN,.rate_q16=m->rate_q16,
 .rate_ppb=m->phase_ppb,.mode=RC8_MAP_SOURCE_Q16,.authenticated=true,.valid=created<expires};
 /* Project the exclusive grant boundary with the SAME frozen Q16/phase map.
  * No nominal-rate approximation may extend wire source permission. */
 if(out->valid&&until<UINT64_MAX){
  struct rc8_export limit=*out;struct rc8_correction at;
  limit.until_tick=until+1;limit.created_ns=0;limit.expires_ns=UINT64_MAX;
  if(!rc8_export_event(&at,&limit,1,(struct rc8_event){.tick=until,.capture_epoch=now.epoch,.kind=RC8_EVENT_TX_DONE})){memset(out,0,sizeof *out);return false;}
  if(at.event_ns<out->expires_ns)out->expires_ns=at.event_ns;
 }
 return out->valid&&out->created_ns<out->expires_ns;
}
bool r2_lora_source_follower(struct rc8_export *out,const r2_servo_map_t *m,struct rc8_scope scope,
 struct r2_capture now,uint64_t until,uint64_t created,uint64_t expires,int64_t offset,uint32_t generation){
 if(!out)return false;
 memset(out,0,sizeof *out);uint64_t anchor,first,last;int64_t value;
 if(!m||!m->valid||scope.kind!=RC8_SELECTED_BLE||rc8_scope_validate(&scope)||scope.scale!=RC8_UTC||
 !generation||!now.epoch||now.tick<m->delivery||until<=now.tick||m->anchor<0||
 !r2_servo_mapped(m,now.tick,&value)||value<0||!translated((uint64_t)m->anchor,offset,&anchor)||
 !translated(created,offset,&first)||!translated(expires,offset,&last)||created>(uint64_t)value||(uint64_t)value>=expires)return false;
 *out=(struct rc8_export){.scope=scope,.from_tick=m->delivery,.until_tick=until,.anchor_tick=m->delivery,
 .anchor_ns=anchor,.capture_epoch=now.epoch,.created_ns=first,.expires_ns=last,.map_generation=generation,
 .upstream_bound_ns=RC8_UNKNOWN,.rate_ppb=m->rate,.phase_ns=m->phase,.mode=RC8_MAP_DELIVERED,.authenticated=true,.valid=true};
 return true;
}
