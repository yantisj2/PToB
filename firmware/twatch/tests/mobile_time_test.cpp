// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "mobile_time.hpp"
#include "radio_candidates.hpp"
#include <cassert>
#include <cstdio>
using namespace watch;
static Candidate candidate(Source source,uint64_t now,bool fresh=true){Candidate c;c.clock.time_valid=true;c.clock.source=source;c.clock.utc_us=1700000000000000ULL+now;c.clock.sampled_us=now;c.clock.until_us=now+250000;c.clock.rms_ns=100;c.clock.locked=true;c.root=1;c.boot=2;c.epoch=3;c.fresh=fresh;c.received_us=now;return c;}
int main(){
 MobileSelection policy;Candidate absent;uint64_t at=1000000;
 auto b=candidate(Source::RC1,at),l=candidate(Source::LOR,at);
 assert(policy.select(b,l,absent,at).source==Source::RC1);
 // Original source is displayed through a sub500ms gap, with quality withdrawn.
 for(unsigned i=0;i<4;i++){at+=100000;l=candidate(Source::LOR,at);auto m=policy.select(absent,l,absent,at);assert(m.source==Source::RC1&&m.time_valid&&!m.locked&&m.rms_ns==UINT32_MAX);}
 at+=100000;b=candidate(Source::RC1,at);l=candidate(Source::LOR,at);assert(policy.select(b,l,absent,at).source==Source::RC1);
 at+=100000;policy.select(absent,candidate(Source::LOR,at),absent,at);
 at+=500000;assert(policy.select(absent,candidate(Source::LOR,at),absent,at).source==Source::LOR);
 // Repeated intermittent BLE cannot displace a warm LoRa clock.
 for(unsigned i=0;i<50;i++){at+=100000;b=candidate(Source::RC1,at);l=candidate(Source::LOR,at);assert(policy.select(i%3?b:absent,l,absent,at).source==Source::LOR);}
 for(unsigned i=0;i<301;i++){at+=100000;b=candidate(Source::RC1,at);l=candidate(Source::LOR,at);policy.select(b,l,absent,at);}
 assert(policy.source()==Source::RC1);
 // Field regression: corrected permission can expire and be repaired in the
 // same owner poll. Cumulative expiry counters must not veto live BLE pairs.
 MobileSelection repaired;uint64_t base=200000000;
 repaired.select(absent,candidate(Source::LOR,base),absent,base);
 for(unsigned i=1;i<=950;++i){
  const auto t=base+i*100000ULL;auto live=candidate(Source::BLE,t);live.pairs=i;live.losses=i/100;
  const auto out=repaired.select(live,candidate(Source::LOR,t),absent,t);
  assert(out.source==(i<301?Source::LOR:Source::BLE));
 }
 assert(repaired.ignored_failures()==9&&repaired.return_resets()==0&&repaired.return_block()==0);
 // Recent pairs cannot hide a new failure once current authority is invalid
 // or retained. Only currently usable fresh BLE can classify an expiry repaired.
 for(unsigned retained_case=0;retained_case<2;++retained_case){
  MobileSelection safety;const auto t=base+100000000;
  auto live=candidate(Source::BLE,t);live.pairs=1;safety.select(live,candidate(Source::LOR,t),absent,t);
  auto failed=candidate(Source::BLE,t+100000);failed.pairs=1;failed.received_us=t;failed.losses=1;
  if(retained_case)failed.fresh=false;else failed.clock.time_valid=false;
  safety.select(failed,candidate(Source::LOR,t+100000),absent,t+100000);
  assert(safety.ignored_failures()==0&&safety.return_resets()==1&&safety.return_reset_reason()==1);
 }
 // Isolated sub500ms gaps pause qualification rather than restarting it;
 // frequent gaps and >=500ms outages must still prefer stable LoRa.
 for(unsigned pattern=0;pattern<3;++pattern){
  MobileSelection qualify;base+=200000000;
  qualify.select(absent,candidate(Source::LOR,base),absent,base);
  bool returned=false;
  for(unsigned i=1;i<=900;++i){
   const auto t=base+i*100000ULL;auto live=candidate(Source::BLE,t);live.pairs=i;
   const bool gap=pattern==0?(i%100==50||i%100==51):pattern==1?(i%100>=50&&i%100<=55):(i%3==0);
   auto out=qualify.select(gap?absent:live,candidate(Source::LOR,t),absent,t);
   if(out.source==Source::BLE)returned=true;
  }
  assert(returned==(pattern==0));
 }
 // Coarse authenticated relay JOIN remains usable without a corrected PLL map.
 v12::R2Status s{};DisplayState d{};s.requested=s.clock_valid=s.following=true;s.root_identity=1;s.root_boot=2;s.source_epoch=3;s.publication=4;s.follower_utc_us=1700000000000000ULL;s.follower_snapshot_us=s.follower_received_us=at;s.service_until_us=s.g1_anchor_deadline_us=at+1000000;s.rc7_bound=true;s.rc7_depth=1;
 auto coarse=ble_candidate(s,d,at,false);assert(usable(coarse,at)&&coarse.fresh&&coarse.clock.source==Source::RC1&&!coarse.clock.locked&&coarse.clock.rms_ns==UINT32_MAX);
 s.fault=true;assert(!ble_candidate(s,d,at,false).clock.time_valid);s.fault=false;s.rc7_depth=2;assert(ble_candidate(s,d,at,false).clock.source==Source::RC2);
 // No synthetic refresh extends a stale radio snapshot or source permission.
 assert(!usable(coarse,at+250000));s.service_until_us=at;assert(!ble_candidate(s,d,at,false).fresh);
 n30_lora_view rx{};rx.requested=rx.active=true;rx.sampled_us=at;rx.watch_pairs=1;rx.watch_age_ms=100;
 assert(lora_candidate(rx,at).clock.lora_receiving);
 rx.watch_age_ms=35000;assert(!lora_candidate(rx,at).clock.lora_receiving);
 rx.watch_age_ms=100;assert(!lora_candidate(rx,at+250000).clock.lora_receiving);
 rx.fault=true;assert(!lora_candidate(rx,at).clock.lora_receiving);
 // Repainted BLE snapshots with still-valid leases cannot renew RF progress.
 MobileSelection stale;const uint64_t start=1000000;
 auto steady=candidate(Source::BLE,start);auto warm=candidate(Source::LOR,start);
 assert(stale.select(steady,warm,absent,start).source==Source::BLE);
 for(unsigned sec=1;sec<=30;sec++){
  const uint64_t t=start+sec*1000000ULL;
  steady=candidate(Source::BLE,t);steady.received_us=start;
  warm=candidate(Source::LOR,t);
  const auto result=stale.select(steady,warm,absent,t);
  assert(result.source==(sec<30?Source::BLE:Source::LOR));
 }
 // Physical corrected pairs also renew availability; a sparse unchanged
 // coarse anchor must not force LoRa while the precision stream advances.
 MobileSelection pairs;
 for(unsigned sec=0;sec<=90;sec++){
  const uint64_t t=start+sec*1000000ULL;
  steady=candidate(Source::BLE,t);steady.received_us=start;steady.pairs=sec+1;
  assert(pairs.select(steady,candidate(Source::LOR,t),absent,t).source==Source::BLE);
 }
 // Repeated failures, not one refused frame, choose warm LoRa after debounce.
 MobileSelection failures;
 for(unsigned i=0;i<=4;i++){
  const uint64_t t=start+i*1000000ULL;steady=candidate(Source::BLE,t);
  steady.sync_failures=std::min(i,3u);steady.received_us=start;warm=candidate(Source::LOR,t);
  const auto result=failures.select(steady,warm,absent,t);
  assert(result.source==(i<4?Source::BLE:Source::LOR));
 }
 // BLE resumes after 30s of uninterrupted healthy reception even if LoRa's
 // reported precision is better. Neither selection changes receiver intent.
 for(unsigned i=5;i<=70;i++){
  const uint64_t t=start+i*1000000ULL;steady=candidate(Source::BLE,t);
  steady.sync_failures=3;steady.clock.rms_ns=1000000;warm=candidate(Source::LOR,t);
  const auto result=failures.select(steady,warm,absent,t);
  assert(result.source==(i<33?Source::LOR:Source::BLE));
 }
 // A retained estimate never advertises fresh source permission, even if
 // a mixed snapshot still contains the previous following/lease fields.
 s.service_until_us=s.g1_anchor_deadline_us=at+1000000;s.g1_retained_estimate=true;
 auto retained=ble_candidate(s,d,at,false);
 assert(retained.clock.time_valid&&!retained.fresh&&retained.clock.holding);
 s.g1_retained_estimate=false;
 // No valid LoRa time: preserve the watch's last clock instead of inventing
 // a radio source merely because its receiver is enabled.
 MobileSelection no_lora;steady=candidate(Source::BLE,start);
 no_lora.select(steady,absent,absent,start);
 steady=candidate(Source::BLE,start+31000000);steady.received_us=start;
 auto held=no_lora.select(steady,absent,absent,start+31000000);
 assert(held.source==Source::BLE&&held.time_valid&&held.local_time&&!held.locked);
 PresentationClock clock;auto m=candidate(Source::BLE,1000000).clock;auto shown=clock.present(m,1000000);const auto initial=shown.utc_us;
 shown=clock.present({},1000000+10ULL*86400000000);assert(shown.time_valid&&shown.local_time&&shown.utc_us==initial+10ULL*86400000000);
 const uint64_t now=1000000+10ULL*86400000000+100000;m=candidate(Source::LOR,now).clock;m.utc_us=shown.utc_us-5000000;
 auto next=clock.present(m,now);assert(next.utc_us>shown.utc_us&&next.utc_us-shown.utc_us==95000&&next.slewing&&!next.locked);
 assert(!clock.present({},1).time_valid);
 // Every pointer obeys its elapsed-time speed; missing RMS rises, no red park.
 InstrumentMotion motion;Model face;face.time_valid=true;face.rms_ns=100;face.battery=80;face.hold_valid=true;face.hold_trials=100;face.hold_ms=50000;face.source=Source::BLE;
 motion.apply(face,1000000);for(unsigned i=1;i<=100;i++)motion.apply(face,1000000+i*50000);
 const double before=motion.rms;face.local_time=true;
 for(unsigned i=101;i<=720;i++){const double old=motion.rms;motion.apply(face,1000000+i*50000);assert(motion.rms>=old&&motion.rms-old<.001667);if(i<=110)assert(motion.rms==before);}
 assert(before<.02&&motion.rms==1&&motion.hold==0);
 // The clock stays in a 35px safe orbit and opposes the sun at every phase.
 for(int degrees=-180;degrees<=180;++degrees){
  const auto q=solar_opposition(degrees);const double a=degrees*3.14159265358979323846/180;
  assert(std::abs(std::hypot(q.x,q.y)-35)<1e-8);
  assert(q.x*std::sin(a)-q.y*std::cos(a)<-34.999);
  assert(std::hypot(q.x,q.y)+83<143-10); // Medallion clears the hour numerals.
 }
 InstrumentMotion sky_motion;Model sky;sky.time_valid=true;sky.calendar.astronomy_valid=true;sky.calendar.today.valid=true;sky.utc_us=100000000;sky.calendar.sampled_utc=100;
 sky.calendar.solar_hour_angle_deg=180;sky_motion.apply(sky,1000000);
 for(unsigned i=1;i<=200;++i){const auto old=sky.solar_clock;sky_motion.apply(sky,1000000+i*50000);
  assert(std::hypot(sky.solar_clock.x-old.x,sky.solar_clock.y-old.y)<=.600001);
  assert(std::hypot(sky.solar_clock.x,sky.solar_clock.y)<=35.000001);
 }
 const auto settled=sky.solar_clock;sky.calendar.astronomy_valid=false;sky_motion.apply(sky,12000000);
 assert(sky.solar_clock.x==settled.x&&sky.solar_clock.y==settled.y); // Missing sky cannot teleport it.
 puts("PASS mobile selection:30s actual BLE progress timeout,3 failures/60s,stable BLE return, relay coarse time,500ms gaps, warmLoRa hysteresis, stale/fault fences,10day hold, monotonic return and bounded analog motion");
}
