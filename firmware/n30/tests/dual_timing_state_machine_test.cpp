// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Event-sequence model: physical source observations and UI publications have
// independent clocks. Execute the production selection policy at its actual
// consumer boundary, rather than assuming a perfectly periodic painter.
#include "dual_time.hpp"
#include <cassert>
#include <cstdio>
#include <cstdint>

namespace {
constexpr uint64_t Start=1000000,Dwell=30000000,End=Start+Dwell+5000000;
n30::Model ble(uint64_t at,bool qualified){
 n30::Model m{};m.sampled_us=m.utc_at_us=at;m.utc_us=1700000000000000+at;
 const auto physical_at=at-at%62500;
 m.until_us=std::min(at+n30::SnapshotMaxUs,physical_at+250000);m.valid=true;m.locked=qualified;
 m.state=n30::State::Follow;m.source=n30::Source::Ble;
 m.ble={at,m.utc_us,m.until_us,3,44,5,true,true};
 m.precision.scope=3;m.precision.root_boot=44;m.precision.source_epoch=5;
 m.precision.quality_scope=7;m.precision.valid=true;m.precision.locked=qualified;m.precision.until_us=m.until_us;m.precision.rms_ns=55;
 return m;
}
n30_lora_view lora(uint64_t at){
 n30_lora_view l{};l.requested=l.active=true;l.state=2;
 l.sampled_us=at;l.utc_us=1700000000000000+at;l.until_us=Start+120000000;
 l.root=3;l.root_boot=44;l.source_epoch=5;return l;
}
// Independent reference liveness obligation: after a continuously observed,
// qualified same-scope physical source for30s, the next fresh publication must
// return to BLE. Rendering does not define whether the source was observed.
bool schedule(uint64_t publish_period,uint64_t jitter_period,uint64_t jitter){
 n30::ClockSelection implementation;
 auto initial=ble(Start,false);implementation.select(initial,lora(Start),Start);
 assert(initial.source==n30::Source::LoRa);
 uint64_t next_publish=Start+publish_period,qualified_since=Start+10000;
 bool failed=false;unsigned publications=0;
 for(uint64_t now=Start+1000;now<=End;now+=1000){
  // Source observation advances independently at1ms. Its original expiry is
  // refreshed only by a new synthetic physical grant, never by the painter.
  const bool source_ready=now>=qualified_since;
  implementation.observe(source_ready,3,44,5,3,9,7,now,source_ready?0:1);
  if(now<next_publish)continue;
  auto m=ble(now,source_ready);implementation.select(m,lora(now),now);++publications;
  const bool expected_ble=source_ready&&now-qualified_since>=Dwell+2*publish_period;
  if(expected_ble&&m.source!=n30::Source::Ble){
   std::printf("COUNTEREXAMPLE live=1 scope=3/44/5 physical_observed_us=%llu publish_period_us=%llu jitter_every_us=%llu jitter_us=%llu at_us=%llu expected=BLE actual=LOR publications=%u\n",
    (unsigned long long)(now-qualified_since),(unsigned long long)publish_period,
    (unsigned long long)jitter_period,(unsigned long long)jitter,(unsigned long long)now,publications);
   failed=true;break;
  }
  next_publish=now+publish_period;
  if(jitter_period&&now/jitter_period!=next_publish/jitter_period)next_publish+=jitter;
 }
 return !failed;
}
bool coupled(unsigned event,uint64_t publish_period,uint64_t inject,uint64_t duration){
 n30::ClockSelection implementation;auto initial=ble(Start,false);implementation.select(initial,lora(Start),Start);
 uint64_t next_publish=Start+publish_period;
 const auto begin=Start+inject,end=begin+duration;
 // Closed-form reference obligations follow the injected event schedule, not
 // the production timer's private counters. Scope/revocation or an unobserved
 // main-owner gap requires a new complete qualification interval.
 const auto recovery=event==0||event==6||event==7?Start+10000:
  event==2||event==3?begin:end;
 for(uint64_t now=Start+1000;now<=Start+70000000;now+=1000){
  const bool injected=now>=begin&&now<end;
  const bool no_observation=(event==4||event==8)&&injected;
  const bool denied=(event==1||event==5||event==9)&&injected;
  const bool ready=now>=Start+10000&&!denied;
  const bool changed=(event==2||event==3)&&now>=begin;
  const uint64_t root=changed&&event==2?13:3;
  const uint32_t publication=changed?10:9,quality=changed?8:7;
  if(!no_observation)implementation.observe(ready,root,44,5,root,publication,quality,now,ready?0:event==5?1:64);
  if(now<next_publish)continue;
  auto m=ble(now,ready);m.ble.root=root;m.precision.scope=root;m.precision.quality_scope=quality;
  if(denied){m.valid=m.locked=m.ble.valid=m.ble.following=false;m.until_us=m.ble.until_us=now;m.state=event==5?n30::State::Off:n30::State::Hold;m.source=n30::Source::None;}
  auto l=lora(now);const bool live_lora=!((event==6||event==7||event==8)&&now>=begin);
  if(!live_lora){l.until_us=now;if(event==7||event==8){l.watch_valid=true;l.watch_sampled_us=now;l.watch_utc_us=l.utc_us;}}
  implementation.select(m,l,now,ready);
  if(denied&&m.source==n30::Source::Ble&&m.locked){
   std::printf("MODEL_SAFETY_FAILURE event=%u at_us=%llu expired_or_disabled_BLE_selected\n",event,(unsigned long long)now);return false;
  }
  const bool should_return=ready&&(!live_lora||now>=recovery+Dwell+2*publish_period);
  if(should_return&&m.source!=n30::Source::Ble){
   std::printf("MODEL_LIVENESS_FAILURE event=%u paint_us=%llu inject_us=%llu duration_us=%llu at_us=%llu ready=1 lora_live=%u expected=BLE\n",event,(unsigned long long)publish_period,(unsigned long long)inject,(unsigned long long)duration,(unsigned long long)now,unsigned(live_lora));return false;
  }
  // The model never accepts a synthetic physical grant beyond its original
  // deadline. LoRa presentation cannot renew or overwrite that BLE deadline.
  if(m.ble.valid){uint64_t utc=0;assert(!n30::ble_time(m,m.ble.until_us,utc));}
  next_publish=now+publish_period;
 }
 return true;
}
// Repeated short permission gaps are a different liveness population from a
// one-off outage followed by30 perfectly uninterrupted seconds. Qualification
// may pause across a bounded gap; it can never authorize output in that gap.
bool recurring_permission(uint64_t period,uint64_t duration,bool partial){
 n30::ClockSelection implementation;
 auto initial=ble(Start,false);implementation.select(initial,lora(Start),Start);
 uint64_t qualified_us=0;bool returned=false;
 for(uint64_t now=Start+1000;now<=Start+70000000;now+=1000){
  const bool denied=(now-Start)%period<duration;
  const bool ready=!denied;
  if(ready)qualified_us+=1000;
  implementation.observe(ready,ready?3:0,ready?44:0,ready?5:0,ready||partial?3:0,ready?9:0,ready||partial?7:0,now,ready?0:216);
  auto m=ble(now,ready);
  if(denied){m.valid=m.locked=m.ble.valid=m.ble.following=false;m.until_us=m.ble.until_us=now;m.state=n30::State::Hold;m.source=n30::Source::None;}
  implementation.select(m,lora(now),now,ready);
  if(denied&&m.source==n30::Source::Ble){std::puts("MODEL_SAFETY_FAILURE permission gap emitted BLE");return false;}
  if(ready&&qualified_us>=Dwell+1000000&&m.source!=n30::Source::Ble){
   std::printf("COUNTEREXAMPLE recurring_permission period_us=%llu gap_us=%llu qualified_us=%llu at_us=%llu expected=BLE actual=LOR\n",
    (unsigned long long)period,(unsigned long long)duration,(unsigned long long)qualified_us,(unsigned long long)now);return false;
  }
  returned|=m.source==n30::Source::Ble;
 }
 return returned;
}
bool permission_edges(uint32_t refusal,uint64_t duration,unsigned replacement){
 n30::ClockSelection implementation;
 auto initial=ble(Start,false);implementation.select(initial,lora(Start),Start);
 constexpr uint64_t Begin=Start+29000000;
 const auto end=Begin+duration;
 const bool may_resume=refusal==216&&duration<1499000&&!replacement;
 for(uint64_t now=Start+1000;now<=Start+65000000;now+=1000){
  const bool denied=now>=Begin&&now<end;
  const bool changed=replacement&&now>=end;
  const auto root=changed&&replacement==1?13u:3u;
  const auto quality=changed&&replacement==2?8u:7u;
  implementation.observe(!denied,denied?0:root,denied?0:44,denied?0:5,denied?3:root,denied?0:9,quality,now,denied?refusal:0);
  auto m=ble(now,!denied);m.ble.root=root;m.precision.scope=root;m.precision.quality_scope=quality;
  if(denied){m.valid=m.locked=m.ble.valid=m.ble.following=false;m.until_us=m.ble.until_us=now;m.state=n30::State::Hold;m.source=n30::Source::None;}
  implementation.select(m,lora(now),now,!denied);
  if(denied&&m.source==n30::Source::Ble)return false;
  if(!may_resume&&now>=end&&now<end+Dwell&&m.source==n30::Source::Ble)return false;
  if(may_resume&&now>=Start+Dwell+duration+10000&&m.source!=n30::Source::Ble)return false;
 }
 return true;
}
}
int main(){
 unsigned schedules=0,failures=0;
 for(uint64_t period:{10000u,125000u,249000u,250000u,500000u,1000000u}){
  ++schedules;if(!schedule(period,0,0))++failures;
 }
 // Main/UI delays repeat more often than the return dwell. No BLE grant,
 // physical progress or same-scope observation is missing in this sequence.
 for(uint64_t jitter:{10000u,126000u,260000u,600000u}){
  ++schedules;if(!schedule(125000,10000000,jitter))++failures;
 }
 for(unsigned event=0;event<10;++event)for(uint64_t period:{10000u,125000u,500000u,1000000u})
  for(uint64_t at:{20000000u,29000000u})for(uint64_t duration:{1000u,300000u,2000000u}){
   ++schedules;if(!coupled(event,period,at,duration))++failures;
  }
 for(uint64_t period:{1000000u,6000000u,20000000u})for(uint64_t duration:{15000u,30000u,100000u,150000u,248000u,300000u,374000u,1200000u})for(bool partial:{false,true}){
  if(duration>=period)continue;
  ++schedules;if(!recurring_permission(period,duration,partial))++failures;
 }
 for(uint32_t refusal:{216u,1u,2u,4u,32u,256u,512u,1024u,2048u,728u})for(uint64_t duration:{1000u,150000u,300000u,374000u,1200000u,1498000u,1499000u,1500000u,2000000u}){
  ++schedules;if(!permission_edges(refusal,duration,0))++failures;
 }
 for(unsigned replacement:{1u,2u}){
  ++schedules;if(!permission_edges(216,30000,replacement))++failures;
 }
 std::printf("DUAL_TIMING_STATE_MODEL schedules=%u failures=%u hardware_access=0 population=SYNTHETIC_SOURCE_UI_INTERLEAVINGS\n",schedules,failures);
 return failures?1:0;
}
