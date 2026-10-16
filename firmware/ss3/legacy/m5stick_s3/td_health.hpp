#pragma once
#include "../../main/ss3_td_model.hpp"
namespace sticks3 {
// One priority order for the front panel, detail page and diagnostic telemetry.
// Time validity is the authenticated source envelope at observed TX, not UTC
// output accuracy or an independently qualified local clock.
struct TdHealth {
 enum Level {Good,Unknown,Warning,Fault};
 const v12::TdPeer *gm=nullptr,*link=nullptr,*relay=nullptr;
 const char* status="GM NOT SEEN";Level level=Unknown;
 uint32_t sample_age_ms=UINT32_MAX;uint16_t delivery=UINT16_MAX;
 int8_t rssi=127;uint8_t bars=0;
 bool source_recent=false,time_valid=false;
 static uint32_t age(uint64_t at,uint64_t now){return at&&now>=at?uint32_t(std::min<uint64_t>(UINT32_MAX-1,(now-at)/1000)):UINT32_MAX;}
 static TdHealth evaluate(const v12::TdSnapshot& d,uint32_t api_errors=0){
  TdHealth h;h.gm=v12::TdModel::best_peer(d,false,d.now_us);h.relay=v12::TdModel::best_peer(d,true,d.now_us);
  const auto id=d.target?d.target:d.last_target;
  for(const auto& p:d.peers)if(id&&p.id==id&&v12::TdModel::fresh(p,d.now_us))h.link=&p;
  if(!h.link)h.link=h.gm;
  if(h.link&&h.link->signal.fresh(d.now_us)){h.rssi=h.link->signal.value;h.bars=h.link->signal.bars;}
  if(h.link){const auto* trace=h.link->relay?d.relay:d.gm;
   for(unsigned i=0;i<24;i++){const auto& point=trace[i];if(point.id==h.link->id&&point.at_us&&d.now_us>=point.at_us&&d.now_us-point.at_us<30000000)h.delivery=point.delivery;}
  }
  if(h.gm){h.sample_age_ms=age(h.gm->time_seen_us,d.now_us);h.source_recent=h.gm->time_known&&h.sample_age_ms<30000;h.time_valid=h.source_recent&&h.gm->time_valid;}
  if(d.owner_stale){h.status="OWNER STALE";h.level=Fault;}
  else if(api_errors){h.status="RADIO FAULT";h.level=Fault;}
  else if(!h.gm){h.status="GM NOT SEEN";h.level=Unknown;}
  else if(!d.role.known){h.status=d.role.until_us?"GM POLLING":"REPORT MISSING";h.level=Unknown;}
  else if(!d.role.available){h.status="GM REPORT STALE";h.level=Warning;}
  else if(!h.source_recent){h.status="TIME NOT SAMPLED";h.level=Unknown;}
  else if(!h.time_valid){h.status="TIME INVALID";h.level=Fault;}
  else if(h.delivery<=1000&&h.delivery<v12::TdDeliveryWarningPermille){h.status="LINK LOSS";h.level=Warning;}
  else if(h.rssi!=127&&h.link->signal.weak){h.status="LINK WEAK";h.level=Warning;}
  else if(h.delivery>1000){h.status="LINK NOT SAMPLED";h.level=Unknown;}
  else if(!d.role.hot){h.status="NO HOT STANDBY";h.level=Warning;}
  else {h.status="GOOD HEALTH";h.level=Good;}
  return h;
 }
};
}
