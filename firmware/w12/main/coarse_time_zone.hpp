// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "r2/r2-g1/time_zone.h"
namespace v12 {
// Display adapter only. UTC clocks, diagnostics, PPS and wire timestamps stay UTC.
inline const char* clock_zone(r2_tz tz,uint64_t until,uint64_t now,int64_t utc,int64_t& shown){
 shown=utc;
 if(now>=until||!r2_tz_local_us(tz,utc,&shown))return "UTC";
 if(tz.id==1)return "UTC";
 if(tz.id==2&&tz.offset_min==-300)return "CDT";
 if(tz.id==2&&tz.offset_min==-360)return "CST";
 return "LOC";
}
}
