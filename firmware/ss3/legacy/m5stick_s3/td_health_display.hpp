#pragma once
#include "td_health.hpp"
#include <cstring>
#include <algorithm>
namespace sticks3 {
// Value-only LCD label. Raw health, source permission and control stay immediate.
struct TdHealthBanner {
 inline static constexpr const char* Labels[]={"HEALTH UNKNOWN","OWNER STALE","RADIO FAULT","GM NOT SEEN","GM POLLING","REPORT MISSING","GM REPORT STALE","TIME NOT SAMPLED","TIME INVALID","LINK LOSS","LINK WEAK","LINK NOT SAMPLED","NO HOT STANDBY","GOOD HEALTH"};
 uint8_t code=0;TdHealth::Level level=TdHealth::Unknown;bool valid=false;
 const char* text()const{return code<sizeof(Labels)/sizeof(*Labels)?Labels[code]:Labels[0];}
 bool good()const{return level==TdHealth::Good;}
 bool same(const TdHealthBanner& b)const{return valid==b.valid&&code==b.code&&level==b.level;}
 static TdHealthBanner from(const TdHealth& h){
  for(unsigned i=1;i<sizeof(Labels)/sizeof(*Labels);++i)if(!std::strcmp(Labels[i],h.status))return {uint8_t(i),h.level,true};
  return {0,TdHealth::Unknown,true};
 }
};
struct TdHealthDisplay {
 static constexpr uint64_t HoldUs=150000,ObservationGapUs=250000;
 TdHealthBanner shown{},candidate{};
 uint64_t last_us=0,since_us=0;bool pending=false,group_pending=false;
 uint32_t changes=0,cancelled=0,min_qualified_us=UINT32_MAX;
 static void increment(uint32_t& n){if(n!=UINT32_MAX)++n;}
 void cancel(){if(pending)increment(cancelled);pending=group_pending=false;}
 TdHealthBanner update(const TdHealth& raw,uint64_t now){
  const auto input=TdHealthBanner::from(raw);
  if(!shown.valid){shown=input;last_us=now;return shown;}
  if(now<last_us||now-last_us>ObservationGapUs)cancel();
  last_us=now;
  if(input.same(shown)){cancel();return shown;}
  // Healthy/unhealthy persistence is independent of the current cause: an
  // alternating set of problems cannot keep a continuously bad link green.
  const bool group=input.good()!=shown.good();
  if(!pending||group!=group_pending||(!group&&!input.same(candidate))){
   cancel();pending=true;group_pending=group;since_us=now;
  }
  candidate=input;
  const auto age=now-since_us;
  if(age>=HoldUs){shown=input;pending=group_pending=false;increment(changes);min_qualified_us=std::min(min_qualified_us,uint32_t(std::min<uint64_t>(age,UINT32_MAX)));}
  return shown;
 }
};
}
