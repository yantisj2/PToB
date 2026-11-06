// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// A new immutable source grant may arrive between an RF original and its
// correction. Use the production GR3 MAC codecs and adapter with synthetic captures.
#include "g1_corrected_clock.hpp"
extern "C" {
#include "group_precision.h"
}
#include <cassert>
#include <cstdio>
using namespace v12;
namespace {
constexpr uint64_t Epoch=UINT64_C(1750000000000000000);
struct Trial {
 r2_group_config group{};uint64_t ids[4]={111,222,0,0};
 unsigned authenticated=0;
 G1CorrectedClock clock;G1Anchor anchor{true,111,55,Epoch/1000+2000000,2000000,3500000,2,3,120000};
 Trial(){group.id=17;group.epoch=1;for(unsigned i=0;i<32;++i)group.psk[i]=uint8_t(i);}
 r2_relay_packet packet(uint32_t pub,uint64_t created,uint64_t expires){
  r2_relay_packet p{};p.origin={0,1,55,2,created,expires,created,int64_t(Epoch),0,0};
  p.route={0,0,0,0,55,3,pub,9,3,UINT32_MAX};return p;
 }
 bool feed(const r2_relay_packet& p,uint64_t at,bool bad_mac=false){
  uint8_t proof[32],wire[190];assert(!r2_group_origin_sign3(proof,&p.origin,&group,ids));
  assert(!r2_group_relay_encode3(wire,&p,proof,&group,ids));if(bad_mac)wire[189]^=1;
  r2_relay_packet decoded{};if(r2_group_relay_decode3(&decoded,wire,190,&group,ids))return false;
  ++authenticated;
  periodic_rx::Stamp s{};s.actual=s.association_observed=true;s.tick20=at*20;s.available20=s.tick20+200;
  s.epoch=1;s.sequence=p.route.publication;s.event=uint16_t(p.route.publication);s.channel=p.route.publication==1?4:5;s.map_enclosure_ns=1000;
  return clock.receive(decoded,s,s.available20,at+10,anchor);
 }
 bool pair(uint64_t original,uint64_t later,uint64_t old_expires,uint64_t new_expires,bool bad_mac=false,unsigned replacement=0){
  auto first=packet(1,1000000000,old_expires);assert(!feed(first,original));
  auto next=packet(2,2000000000,new_expires);next.count=1;
  next.correction[0]={1,1,4,original*1000*2/125,3};
  if(replacement==1)++next.origin.source_epoch;
  if(replacement==2){++next.route.train;++next.route.map_generation;++next.correction[0].generation;}
  if(replacement==3){++next.origin.boot;++next.route.boot;}
  if(replacement==4)++next.route.capture_epoch;
  return feed(next,later,bad_mac);
 }
};
}
int main(){
 // Both wire packets are valid: the old RF event is after the new grant's
 // creation, but its data still carried the earlier immutable envelope.
 Trial renewal;const auto accepted=renewal.pair(2100000,2162500,2650000000,3650000000);
 if(!accepted)std::fprintf(stderr,"COUNTEREXAMPLE GR3 renewal authenticated=%u old_RF_us=2100000 correction_us=2162500 old_expiry_us=2650000 new_expiry_us=3650000 admitted_coarse_us=120000 valid_old_pair_discarded\n",renewal.authenticated);
 assert(accepted&&renewal.authenticated==2);
 assert(renewal.clock.accepted()==1&&renewal.clock.original20()==42000000);
 const auto old_limit=renewal.clock.until();assert(old_limit>2162510&&old_limit<2650000);
 assert(!renewal.clock.covers(old_limit,old_limit)); // New header cannot extend old RF permission.
 Trial shortened;assert(shortened.pair(2100000,2162500,2650000000,2500000000));
 assert(shortened.clock.until()<old_limit&&shortened.clock.until()<2500000);
 Trial expired;assert(!expired.pair(2100000,2162500,2200000000,3650000000));
 assert(!expired.clock.until()&&!expired.clock.accepted());
 Trial unauthenticated;assert(!unauthenticated.pair(2100000,2162500,2650000000,3650000000,true));
 assert(!unauthenticated.clock.until()&&!unauthenticated.clock.accepted());
 for(unsigned replacement=1;replacement<=4;++replacement){
  Trial changed;assert(!changed.pair(2100000,2162500,2650000000,3650000000,false,replacement));
  assert(changed.authenticated==2&&!changed.clock.until()&&!changed.clock.accepted());
 }
 // A newly received source header alone cannot renew the old mapped pair.
 auto header=renewal.packet(3,2000000000,3650000000);
 assert(!renewal.feed(header,2225000)&&renewal.clock.until()==old_limit);
 auto matched=renewal.packet(4,2000000000,3650000000);matched.count=1;
 matched.correction[0]={3,3,5,UINT64_C(2225000)*1000*2/125,3};
 assert(renewal.feed(matched,2287500)&&renewal.clock.until()>old_limit&&renewal.clock.until()<3650000);
 unsigned schedules=0;
 for(uint64_t phase:{0u,62500u,125000u,250000u,375000u})
  for(uint64_t delay:{62500u,125000u,187500u,249000u,250000u})
   for(uint64_t expiry:{UINT64_C(2500000000),UINT64_C(2650000000),UINT64_C(3000000000),UINT64_C(3650000000)}){
    Trial x;const auto original=2000000+phase,now=original+delay+10;
    const auto until=std::min(UINT64_C(2650000000),expiry)/1000-x.anchor.guard(now)-1;
    const bool expected=delay+10<=250000&&until>now;
    const bool result=x.pair(original,original+delay,2650000000,expiry);
    assert(x.authenticated==2&&result==expected);
    if(result){assert(x.clock.until()==until);assert(!x.clock.covers(until,until));}
    else assert(!x.clock.until());
    ++schedules;
   }
 std::printf("GR3_SOURCE_RENEWAL schedules=%u both_MACs=1 original_expiry=UNCHANGED scope_fences=UNCHANGED\n",schedules);
 std::printf("PASS authenticated GR3 grant rollover preserves a valid old physical pair, clips both grants, and refuses expired or bad-MAC renewal; old_deadline=%llu\n",(unsigned long long)old_limit);
}
