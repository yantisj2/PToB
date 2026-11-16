// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Watch software-path regression. Synthetic corrected publications are not RF,
// independently measured output, or a transfer of another board's calibration.
#include "g1_corrected_clock.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
namespace {
constexpr uint64_t Utc = UINT64_C(1750000000000000000);
struct Stream {
 G1CorrectedClock clock;
 G1Anchor anchor{true,111,55,Utc/1000,1000000,2500000,2,3,40000};
 uint32_t publication=1,remote_epoch=9;
 uint64_t old_remote=0;
 uint16_t old_event=0;
 uint8_t old_channel=0;
 int64_t injection=0;
 bool feed(uint64_t us,bool physical=true,bool correction=true){
  const uint8_t channel=uint8_t((publication*11+publication/17)%37);
  // Known oscillator rate and unknown fixed channel phase. The channel delay
  // is deliberately left in causal quality, as it is in production.
  const auto remote=uint64_t(int64_t(us*1000+us/2000+channel*3)+injection);
  r2_relay_packet p{};
  p.origin={0,1,55,2,(us/1000000)*1000000000,(us/1000000)*1000000000+1650000000,0,int64_t(Utc),0,0};
  p.route={0,0,0,0,55,3,publication,remote_epoch,3,UINT32_MAX};
  if(old_remote&&correction){p.count=1;p.correction[0]={publication-1,old_event,old_channel,old_remote*2/125,3};}
  periodic_rx::Stamp stamp{};
  stamp.actual=stamp.association_observed=physical;
  stamp.tick20=us*20;stamp.available20=stamp.tick20+200;
  stamp.epoch=1;stamp.sequence=publication;stamp.event=uint16_t(publication);
  stamp.channel=channel;stamp.map_enclosure_ns=1000;
  anchor.at=us;anchor.until=us+1500000;
  const auto accepted=clock.receive(p,stamp,stamp.available20,us+10,anchor);
  old_remote=remote;old_event=stamp.event;old_channel=channel;++publication;
  return accepted;
 }
};
}
int main(){
 // N30's software fine mode is also required on the watch's baseline counter.
 // Both signs preserve the already delivered value at the mode boundary.
 for(const int sign:{-1,1})for(const int phase:{1001,1500,1999}){
  G1ClockPrediction model{20000000,INT64_C(40000000000),sign*phase,500,0};
  if(model.phase_slew_ns_s()!=1000){std::puts("LEGACY_FINE_SLEW: watch requires the N30 software path");return 3;}
  assert(model.relative_at(model.tick)==model.epoch);
  assert(model.relative_at(model.tick+1250000)==model.epoch+62500000+int64_t(std::llround(31.25+sign*62.5)));
 }
 Stream stream;uint64_t now=0;
 for(unsigned i=0;i<1200;++i){
  now=1000000+uint64_t(i)*62500;
  stream.feed(now);
 }
 auto model=stream.clock.prediction();
 auto quality=stream.clock.quality(now+10,model.tick);
 assert(stream.clock.frequency_trained()&&stream.clock.fit_points()==64);
 // A regression to legacy250ms post-acquisition decimation makes this fail.
 assert(stream.clock.fit_span_ms()>=3900&&stream.clock.fit_span_ms()<=4000);
 assert(std::abs(model.rate-500)<20);
 assert(quality.samples>=240&&quality.rms_ns<200);
 assert(quality.locked(model.tick,model));
 const auto clean_span=stream.clock.fit_span_ms(),clean_rms=quality.rms_ns;
 const auto clean_samples=quality.samples;
 // A fresh unseen physical error enters the pre-update population before it
 // can train the fit; lowering displayed RMS by hiding tails must fail here.
 const auto before=stream.clock.predictions();
 stream.injection=4000;now+=62500;stream.feed(now);
 now+=62500;stream.feed(now);
 model=stream.clock.prediction();quality=stream.clock.quality(now+10,model.tick);
 assert(stream.clock.predictions()==before+2);
 assert(quality.max_ns>3500&&!quality.locked(model.tick,model));
 // Correction/payload alone cannot create physical precision or renew time.
 Stream missing;
 assert(!missing.feed(1000000,false)&&!missing.feed(1062500,false));
 assert(!missing.clock.prediction().tick&&!missing.clock.until());
 Stream uncorrected;
 assert(!uncorrected.feed(1000000,true,false)&&!uncorrected.feed(1062500,true,false));
 assert(!uncorrected.clock.prediction().tick&&!uncorrected.clock.until());
 // The baseline remains uncalibrated even when the software estimate locks.
 uint64_t utc=0;
 const auto original_until=stream.clock.until();stream.clock.hold();
 assert(stream.clock.until()==original_until);
 assert(!stream.clock.covers(original_until,original_until+1));
 auto changed=stream.anchor;++changed.boot;stream.clock.scope(changed);
 assert(!stream.clock.prediction().tick&&!stream.clock.frequency_trained());
 assert(!stream.clock.deliver(now*20,now+10,utc));
 std::printf("PASS watch N30 software servo: dense_span_ms=%u synthetic_clean_rms_ns=%u samples=%u; tails/physical/scope/permission fences retained; RF/output/UTC unqualified\n",
  clean_span,clean_rms,clean_samples);
}
