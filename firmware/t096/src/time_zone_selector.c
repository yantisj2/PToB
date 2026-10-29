/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "time_zone_selector.h"

_Static_assert(sizeof(struct r2_tz_selector) <= 40, "Bounded selector state");
_Static_assert(sizeof(struct r2_position) <= 24, "Bounded GNSS position snapshot");
static void pending_clear(struct r2_tz_selector *s) {
 s->candidate=0;s->hits=0;s->candidate_at=0;
}
void r2_tz_selector_step(struct r2_tz_selector *s,const struct r2_position *p,
 uint64_t now,uint64_t utc,bool utc_valid) {
 if(!s)return;
 if(s->looked && now<s->lookup_at){*s=(struct r2_tz_selector){0};}
 if(!utc_valid || !r2_tz_db_window(utc)){
  pending_clear(s);s->reason=utc_valid?R2_TZ_DB_EXPIRED:R2_TZ_NO_UTC;return;
 }
 if(!p||!p->valid||p->received_us<0||(uint64_t)p->received_us>now||
    now-(uint64_t)p->received_us>R2_TZ_POSITION_US){
  pending_clear(s);s->reason=R2_TZ_STALE;return;
 }
 if(s->looked && now-s->lookup_at<R2_TZ_LOOKUP_US)return;
 s->lookup_at=now;s->looked=true;
 uint16_t id=r2_tz_db_geo(p->latitude_e7,p->longitude_e7);
 if(!id||id==UINT16_MAX){
  pending_clear(s);s->reason=id?R2_TZ_AMBIGUOUS:R2_TZ_UNSUPPORTED;return;
 }
 if(s->selected==id){
  s->confirmed_at=(uint64_t)p->received_us;s->reason=R2_TZ_LIVE;
  pending_clear(s);s->last_fix=(uint64_t)p->received_us;return;
 }
 if(s->candidate!=id){s->candidate=id;s->candidate_at=now;s->hits=0;}
 if(!s->hits || s->last_fix!=(uint64_t)p->received_us){
  if(s->hits<3)++s->hits;
  s->last_fix=(uint64_t)p->received_us;
 }
 s->reason=R2_TZ_WAIT;
 if(s->hits>=3 && now>=s->candidate_at && now-s->candidate_at>=R2_TZ_LOOKUP_US){
  s->selected=id;s->confirmed_at=(uint64_t)p->received_us;
  pending_clear(s);s->reason=R2_TZ_LIVE;
 }
}
struct r2_tz r2_tz_selector_value(const struct r2_tz_selector *s,unsigned mode,
 struct r2_tz manual,uint64_t now,uint64_t utc,bool utc_valid) {
 if(mode==R2_TZ_MANUAL_OFFSET)return r2_tz_valid(manual)?manual:r2_tz_unknown();
 if(!utc_valid || !r2_tz_db_window(utc))return r2_tz_unknown();
 uint16_t id=mode==R2_TZ_MANUAL_ZONE?manual.id:0;
 if(mode==R2_TZ_AUTOMATIC && s && s->selected && now>=s->confirmed_at &&
    now-s->confirmed_at<R2_TZ_HOLD_US)id=s->selected;
 int16_t offset;
 return id && r2_tz_db_offset(id,utc,&offset)?(struct r2_tz){id,offset}:r2_tz_unknown();
}
const char *r2_tz_reason_name(unsigned r){
 static const char *const names[]={"WAIT","LIVE","AUTO HOLD","STALE POSITION",
  "AMBIGUOUS","UNSUPPORTED","NO UTC","DATABASE EXPIRED"};
 return r<sizeof names/sizeof names[0]?names[r]:"UNKNOWN";
}
