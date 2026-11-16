// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual portable source admission and servo; all observations are synthetic.
#include "g1_corrected_clock.hpp"
#include "watch.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
namespace {
constexpr int64_t Utc=INT64_C(1750000000000000000);
G1RetainedEstimate held(){
 G1RetainedEstimate value;const G1ClockPrediction model{20000000,1000000000,700,4321,Utc};
 assert(value.retain(model,111,55,2,20000000,1000000,true));return value;
}
struct Direct {
 G1CorrectedClock clock;G1RetainedEstimate old=held();
 G1Anchor anchor{true,222,66,0,0,0,3,4,1000};
 uint32_t publication=1,epoch=5;uint64_t previous=0;uint16_t event=0;uint8_t channel=0;
 int64_t phase=0,offset=Utc-9000000000;
 bool feed(uint64_t us,bool physical=true,bool correction=true){
  const auto tick=us*20;const auto remote=uint64_t(int64_t(us*1000)+9000000000+phase+int64_t(std::llround(double(us)*4321/1000000)));
  r2_relay_packet p{};const auto created=remote/1000000000*1000000000;
  p.origin={0,1,anchor.boot,anchor.source_epoch,created,created+1650000000,0,offset,0,0};
  p.route={0,0,0,0,anchor.boot,anchor.publication,publication,10,anchor.publication,UINT32_MAX};
  if(previous&&correction){p.count=1;p.correction[0]={publication-1,event,channel,previous*2/125,anchor.publication};}
  periodic_rx::Stamp s{};s.actual=s.association_observed=physical;s.tick20=tick;s.available20=tick+200;
  s.epoch=epoch;s.sequence=publication;s.event=uint16_t(publication);s.channel=uint8_t(publication%4);s.map_enclosure_ns=1000;
  anchor.at=us;anchor.until=us+1500000;anchor.utc=uint64_t(offset)+remote;
  const bool accepted=clock.receive(p,s,s.available20,us+10,anchor,nullptr,&old,epoch);
  previous=remote;event=s.event;channel=s.channel;++publication;return accepted;
 }
};
constexpr uint64_t Root=111,RootBoot=55,Parent=333,ParentBoot=77,Self=555;
r2_group_config group(){r2_group_config g{};for(unsigned i=0;i<32;++i)g.psk[i]=uint8_t(i+1);g.id=17;g.epoch=9;return g;}
struct Relay {
 r2_group_config g=group();Rc7Client client;G1CorrectedClock clock;G1RetainedEstimate old=held();
 G1Anchor anchor{true,Root,RootBoot,0,60000000,61500000,2,3,1000};
 uint64_t original=1200000000,available=1201370000,remote=UINT64_C(80000000000);
 uint32_t epoch=5,publication=11;uint8_t depth=1;int64_t shift_ns=20000000000;
 Relay(uint8_t hops=1,int64_t shift=20000000000,uint64_t start_us=60000000):depth(hops),shift_ns(shift){
  original=start_us*20;available=original+1370000;anchor.at=start_us;anchor.until=start_us+1500000;
  remote=original/20*1000+uint64_t(std::llround(double(original/20)*4321/1000000))+shift_ns;
  rc7_path path{};path.depth=depth;path.source_epoch=2;path.node[0]={Root,RootBoot,3,3,0,0};
  if(depth==2)path.node[1]={222,66,2,2,1,1};path.node[depth]={Parent,ParentBoot,4,4,depth,1};
  rc7_control cap{};cap.type=RC7_CAPABILITY;cap.path=path;cap.sender=Parent;cap.sender_boot=ParentBoot;cap.next_challenge=55;
  uint8_t wire[RC_DYNAMIC_CONTROL_BYTES];rc7_control decoded{};
  assert(!rc7_control_encode(wire,&cap,&g)&&!rc7_control_decode(&decoded,wire,sizeof wire,&g));
  const auto bt=original-2000000,now=bt/20;assert(client.make_request(decoded,Self,88,66,now-1000,false));
  auto response=client.request;response.type=RC7_RESPONSE;response.sender=Parent;response.target=Self;
  response.sender_boot=ParentBoot;response.target_boot=88;response.next_challenge=56;response.ready=true;response.publication_floor=10;
  response.root_lo_ns=remote-105000000;response.root_hi_ns=remote-95000000;response.source_expires_ns=response.root_hi_ns+1650000000;
  assert(!rc7_control_encode(wire,&response,&g)&&!rc7_control_decode(&decoded,wire,sizeof wire,&g));assert(client.bind(decoded,now,bt));
 }
 periodic_rx::Stamp stamp(uint64_t tick,uint64_t at,uint32_t pub,uint16_t event,uint8_t ch){
  periodic_rx::Stamp s{};s.actual=s.association_observed=true;s.tick20=tick;s.available20=at;s.epoch=epoch;
  s.sequence=pub;s.event=event;s.channel=ch;s.map_enclosure_ns=1000;return s;
 }
 void pair(){
  rc7_packet p{};p.origin={Root,RootBoot,2,remote-1440000000,remote+210000000,Utc,62500000,0,0};
  p.depth=depth;p.slot=depth;p.sender=Parent;p.boot=ParentBoot;p.train=p.epoch=4;p.publication=publication;
  assert(!rc7_origin_sign(p.proof,&p.origin,&g));uint8_t wire[190];rc7_packet decoded{};
  const auto decode=[&](){assert(!rc7_encode(wire,&p,&client.receiver.path,&g)&&!rc7_decode(&decoded,wire,sizeof wire,&client.receiver.path,&g));};
  const auto event=uint16_t(100+publication);const auto channel=uint8_t(publication%4);
  decode();auto rx=stamp(original,original+20,publication,event,channel);assert(!client.receive(decoded,rx,rx.available20));
  p.publication=publication+1;p.count=1;p.correction[0]={publication,event,channel,7,300000,remote,UINT32_MAX};
  decode();rx=stamp(available-20,available,publication+1,uint16_t(event+1),uint8_t(channel+1));assert(client.receive(decoded,rx,available));
 }
 void next(){publication+=2;original+=1250000;available=original+20000;remote=original/20*1000+uint64_t(std::llround(double(original/20)*4321/1000000))+shift_ns;}
 G1CorrectedClock::Rc7Update acquire(){return clock.receive_admitted_rc7(client,available,available/20,anchor,g,&old);}
};
}
int main(){
 for(const int64_t change:{-INT64_C(2000000000),INT64_C(2000000000)}){
  Direct d;d.phase=change;watch::PresentationClock presentation;watch::Model input;
  input.time_valid=true;input.local_time=input.holding=true;input.source=watch::Source::BLE;
  input.sampled_us=60000000;input.until_us=60250000;input.utc_us=uint64_t(d.old.clock.at(input.sampled_us*20))/1000;
  auto shown=presentation.present(input,input.sampled_us);const auto prior=shown;
  assert(!d.feed(61000000));
  assert(!d.clock.prediction().tick&&!d.clock.until());assert(d.feed(61062500));
  auto model=d.clock.prediction();assert(model.rate==4321&&model.phase==0&&model.utc_offset==d.offset);
  assert(std::llabs(model.at(model.tick)-d.old.clock.at(model.tick))>1000000000);
  assert(!d.clock.frequency_trained()&&d.clock.fit_points()==1&&!d.clock.predictions());
  assert(!d.clock.quality(61062510,model.tick).locked(model.tick,model));
  input.local_time=input.holding=false;input.sampled_us=model.tick/20;input.until_us=input.sampled_us+250000;input.utc_us=uint64_t(model.at(model.tick))/1000;
  shown=presentation.present(input,input.sampled_us);const auto elapsed=shown.sampled_us-prior.sampled_us;
  assert(shown.utc_us>=prior.utc_us+elapsed-elapsed/20&&shown.utc_us<=prior.utc_us+elapsed+elapsed/20);
  assert(shown.slewing&&!shown.locked&&!shown.hold_valid); // Delivered phase cannot jump the watch face backward.
  // A new capture owner or source must restart its physical fit. It may use
  // only the oscillator, despite arbitrary old UTC coordinate/phase changes.
  for(unsigned generation=0;generation<4;++generation){
   d.clock.clear();++d.anchor.boot;++d.anchor.publication;++d.epoch;d.previous=0;
   const auto start=62000000+generation*1000000;
   assert(!d.feed(start)&&d.feed(start+62500));
   assert(d.clock.prediction().rate==4321&&!d.clock.frequency_trained()&&d.clock.fit_points()==1);
  }
  for(unsigned i=2;i<160;++i)d.feed(65000000+i*62500);
  assert(d.clock.frequency_trained()&&std::abs(d.clock.prediction().rate-4321)<10);
 }
 Direct missing;assert(!missing.feed(61000000,false)&&!missing.feed(61062500,false));assert(!missing.clock.until()&&!missing.clock.prediction().tick);
 Direct cold;cold.old.trained=false;assert(!cold.feed(61000000)&&cold.feed(61062500));assert(cold.clock.prediction().rate==0);
 auto old=held();assert(watch::roaming_rate(&old,UINT64_C(864000000000),UINT64_C(864000000200))==4321); // No admission/poll slides an age deadline.
 assert(watch::roaming_rate(&old,old.clock.tick-1,old.clock.tick)==0);
 assert(watch::roaming_rate(&old,old.clock.tick,old.clock.tick-1)==0);
 old.clock.rate=INFINITY;assert(watch::roaming_rate(&old,40000000,40000200)==0);
 old=held();old.clock.rate=50001;assert(watch::roaming_rate(&old,40000000,40000200)==0);
 old=held();old.clock.receiver_phase_ns=1;assert(watch::roaming_rate(&old,40000000,40000200)==0);
 old=held();old.original20=old.clock.tick+1;assert(watch::roaming_rate(&old,40000000,40000200)==0);
 for(uint8_t depth:{1,2}){
  Relay r(depth);assert(r.acquire().kind==G1CorrectedClock::Rc7Kind::Refused&&!r.clock.prediction().tick);
  r.pair();const auto accepted=r.acquire();assert(accepted.kind==G1CorrectedClock::Rc7Kind::Mapped);
  const auto model=r.clock.prediction();assert(model.rate==4321&&!model.phase&&!r.clock.frequency_trained());
  assert(r.clock.fit_points()==1&&!r.clock.predictions()&&!r.clock.quality(r.available/20,r.available).locked(r.available,model));
  assert(std::llabs(model.at(model.tick)-r.old.clock.at(model.tick))>1000000000); // New path's phase, never old root phase.
  assert(r.clock.finalize(r.available));const auto until=r.clock.until();
  assert(r.acquire().kind==G1CorrectedClock::Rc7Kind::Refused&&r.clock.until()==until);
  Relay invalid(depth);invalid.pair();invalid.client.receiver.clock.reference.proof[0]^=1;
  assert(invalid.acquire().kind==G1CorrectedClock::Rc7Kind::Refused&&!invalid.clock.prediction().tick);
 }
 // Explicit RC1 -> direct BLE -> RC1 regression on the real signed RC7
 // receiver and GR3 servo. The physical source is synthetic, exactly4321ppb.
 Relay rc1(1,0);rc1.available=rc1.original+20000;uint64_t rc1_trained_at=0;
 for(unsigned i=0;i<64;++i){
  rc1.pair();assert(rc1.acquire().kind==G1CorrectedClock::Rc7Kind::Mapped);assert(rc1.clock.finalize(rc1.available));rc1.client.utc_known=true;
  if(!rc1_trained_at&&rc1.clock.frequency_trained())rc1_trained_at=rc1.available;
  if(i!=63)rc1.next();
 }
 assert(rc1_trained_at&&std::abs(rc1.clock.prediction().rate-4321)<1);
 Direct direct;direct.anchor.root=Root;direct.anchor.boot=RootBoot;direct.anchor.source_epoch=2;direct.anchor.publication=3;
 assert(direct.old.retain(rc1.clock.prediction(),Root,RootBoot,2,rc1.clock.original20(),rc1.available/20,true));
 auto cold_direct=direct;cold_direct.old.trained=false;const auto start=(rc1.available/20)+1000000;
 uint64_t direct_ready=0,direct_trained=0;uint32_t warm_peak=0,cold_peak=0;
 for(unsigned i=0;i<64;++i){
  const auto now=start+i*62500;assert(direct.feed(now)==cold_direct.feed(now));
  if(i){
   if(!direct_ready)direct_ready=now;
   const auto at=direct.clock.prediction().tick;
   const auto truth=Utc+int64_t(at)*50+int64_t(std::llround(double(at)/20000000*4321));
   if(i<32){warm_peak=std::max(warm_peak,uint32_t(std::llabs(direct.clock.prediction().at(at)-truth)));cold_peak=std::max(cold_peak,uint32_t(std::llabs(cold_direct.clock.prediction().at(at)-truth)));}
   if(!direct_trained&&direct.clock.frequency_trained())direct_trained=now;
  }
 }
 assert(direct_ready-start==62500&&direct_trained-start>=2000000&&direct_trained-start<2500000);
 assert(warm_peak<cold_peak&&warm_peak<125);
 // Return to the previously used RC1 requires a fresh challenge admission.
 Relay returned(1,0,start+5000000);returned.available=returned.original+20000;
 assert(returned.old.retain(direct.clock.prediction(),Root,RootBoot,2,direct.clock.original20(),(start+63*62500),true));
 auto return_cold=returned;return_cold.old.trained=false;uint64_t return_trained=0;uint32_t return_warm_peak=0,return_cold_peak=0;
 for(unsigned i=0;i<64;++i){
  returned.pair();return_cold.pair();assert(returned.acquire().kind==G1CorrectedClock::Rc7Kind::Mapped&&return_cold.acquire().kind==G1CorrectedClock::Rc7Kind::Mapped);
  const auto model=returned.clock.prediction();assert(returned.clock.finalize(returned.available)&&return_cold.clock.finalize(return_cold.available));
  returned.client.utc_known=return_cold.client.utc_known=true;
  if(!i)assert(!returned.clock.frequency_trained()&&returned.clock.fit_points()==1&&!returned.clock.predictions()&&std::abs(model.rate-4321)<1);
  if(i<32){const auto truth=Utc+int64_t(returned.available)*50+int64_t(std::llround(double(returned.available)/20000000*4321));
   return_warm_peak=std::max(return_warm_peak,uint32_t(std::llabs(returned.clock.prediction().at(returned.available)-truth)));
   return_cold_peak=std::max(return_cold_peak,uint32_t(std::llabs(return_cold.clock.prediction().at(return_cold.available)-truth)));}
  if(!return_trained&&returned.clock.frequency_trained())return_trained=returned.available;
  if(i!=63){returned.next();return_cold.next();}
 }
 assert(return_trained&&return_warm_peak<125&&return_warm_peak<return_cold_peak);
 std::printf("BIDIRECTIONAL_MODEL rc1_training_us=%llu direct_pair_us=%llu direct_training_us=%llu direct_warm_peak_ns=%u direct_cold_peak_ns=%u return_pair_us=1000 return_training_us=%llu return_warm_peak_ns=%u return_cold_peak_ns=%u rate_ppb=%.6f\n",
  (unsigned long long)((rc1_trained_at-1200020000)/20),(unsigned long long)(direct_ready-start),(unsigned long long)(direct_trained-start),warm_peak,cold_peak,
  (unsigned long long)(return_trained/20-(start+5000000)),return_warm_peak,return_cold_peak,returned.clock.prediction().rate);
 std::puts("PASS watch GR3/RC7 roaming: fresh phase after signed new physical pair, warm rate beyond30s, two relay depths, repeated untrained scopes, cold/domain/auth/replay guards, new quality and no imported lock; synthetic only");
}
