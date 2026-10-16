#pragma once
#include "g1_clock_prediction.hpp"
#include <cstdint>
namespace sticks3 {
// Presentation only. Fine-map expiry withdraws quality and relay readiness,
// while canonical coarse UTC remains visible only within its original fence.
struct DisplayClock {
 enum class Source:uint8_t {Invalid,Coarse,Fine};
 v12::G1ClockPrediction model{};
 uint64_t tick=0,until=0,model_until=0;
 int64_t utc_ns=0;
 static uint64_t deadline(uint64_t tick,uint64_t now,uint64_t until){
  return until>now&&(until-now)<UINT64_MAX/20&&tick<=UINT64_MAX-(until-now)*20?tick+(until-now)*20:0;
 }
 Source project(bool canonical_valid,bool timer,uint64_t now,int64_t& utc)const {
  utc=0;
  if(!canonical_valid||!timer||now<tick||now>=until)return Source::Invalid;
  if(model.tick&&now<model_until){utc=model.at(now);if(utc>0)return Source::Fine;}
  if(utc_ns<=0||now-tick>uint64_t(INT64_MAX-utc_ns)/50)return Source::Invalid;
  utc=utc_ns+int64_t(now-tick)*50;return Source::Coarse;
 }
};
}
