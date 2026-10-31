// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "td_mesh.hpp"
#include <cstdio>
namespace v12::td_mesh {
inline void epoch_start_text(char* out,size_t size,uint64_t birth){
 if(birth<UINT64_C(946684800000000)||birth>=UINT64_C(4102444800000000)){std::snprintf(out,size,"--");return;}
 // Gregorian civil date from Unix days; bounded above to years 2000..2099.
 const auto seconds=birth/1000000;const int64_t z=int64_t(seconds/86400)+719468;
 const auto era=z/146097;const auto doe=unsigned(z-era*146097);
 const auto yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
 auto year=int(yoe)+int(era)*400;const auto doy=doe-(365*yoe+yoe/4-yoe/100);
 const auto mp=(5*doy+2)/153,day=doy-(153*mp+2)/5+1;
 const auto month=mp<10?mp+3:mp-9;year+=month<=2;
 const auto t=unsigned(seconds%86400);
 std::snprintf(out,size,"%04d-%02u-%02u %02u:%02u:%02u",year,month,day,t/3600,t/60%60,t%60);
}
inline bool epoch_elapsed(const Roles& r,uint64_t utc,uint64_t& elapsed){
 elapsed=0;if(!r.known||r.birth_utc_us<UINT64_C(946684800000000)||r.birth_utc_us>=UINT64_C(4102444800000000)||
  utc<r.birth_utc_us||utc>=UINT64_C(4102444800000000))return false;
 elapsed=utc-r.birth_utc_us;return true;
}

enum class EpochBasis:uint8_t {Unknown,MatchedUtc,ReportedUtc};
struct EpochTime {uint64_t utc_us=0,elapsed_us=0;EpochBasis basis=EpochBasis::Unknown;bool current=false;};
// Display-only UTC projection. In passive TD mode no admitted clock exists:
// use the authenticated ANN's UTC sample plus local elapsed time, explicitly
// estimated and LIVE/LAST under that ANN's ORIGINAL 2.75s receipt horizon.
// It never supplies clock training, timing permission or output time.
inline EpochTime epoch_time(const Snapshot& m,uint64_t now,bool matched=false,uint64_t utc=0){
 EpochTime out;const auto& r=m.roles;
 if(!r.known||r.wire_version!=4||r.withdrawn||r.root!=m.selected.root||r.root_boot!=m.selected.boot||r.epoch!=m.selected.epoch)return out;
 if(matched&&epoch_elapsed(r,utc,out.elapsed_us)){out.utc_us=utc;out.basis=EpochBasis::MatchedUtc;out.current=true;return out;}
 if(!r.utc_sample_us||!r.utc_received_us||now<r.utc_received_us||now-r.utc_received_us>UINT64_MAX-r.utc_sample_us)return out;
 out.utc_us=r.utc_sample_us+now-r.utc_received_us;
 if(!epoch_elapsed(r,out.utc_us,out.elapsed_us))return EpochTime{};
 out.basis=EpochBasis::ReportedUtc;out.current=live(r,now)&&now-r.utc_received_us<2750000;return out;
}
}
