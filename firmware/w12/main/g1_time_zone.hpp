// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "r2/r2-g1/time_zone.h"
namespace v12 {
// Caller authenticates and binds the current parent/path before ordering metadata.
// No precision field, clock fit, source permission or radio owner is changed.
struct G1TimeZoneOrder {
 uint64_t token=0;bool ordered=false;
 static bool same(r2_tz a,r2_tz b){return a.id==b.id&&a.offset_min==b.offset_min;}
 bool observe(r2_tz_lease& lease,r2_tz value,uint64_t next,uint64_t received,uint64_t now,uint64_t period,bool equal){
  if(!r2_tz_valid(value))value=r2_tz_unknown();
  if(ordered&&(next<token||(next==token&&(!equal||!same(value,lease.value))))){
   if(!same(value,lease.value)&&lease.conflicts<UINT16_MAX)++lease.conflicts;
   return false;
  }
  if(!r2_tz_observe(&lease,value,received,now,period))return false;
  token=next;ordered=true;return true;
 }
 bool bootstrap(r2_tz_lease& lease,r2_tz value,uint64_t received,uint64_t now){
  return !ordered&&r2_tz_observe(&lease,value,received,now,R2_TZ_GM_PERIOD_US);
 }
};
}
