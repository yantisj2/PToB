// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "g1_clock_prediction.hpp"
#include <array>
namespace watch {
struct HoldoverView {
 uint64_t last20=0;
 uint32_t duration_ms=0,probe_ms=12000,started=0,completed=0,aborted=0,gaps=0;
 uint16_t trials=0,bad=0,unknown=0;
 bool ready=false,lower_bound=false;
};
// Adapted from W12HoldoverHorizon. This is a separate watch observer: it
// freezes the delivered BLE software clock and scores later authenticated
// physical pairs. It never steers the clock, extends permission, or estimates
// independent UTC/RF accuracy. Overlapping trials are a sampled population,
// not independent experiments or a 99%-confidence guarantee.
class EmpiricalHoldover {
 public:
 static constexpr uint64_t Hz=20000000,Gap=Hz/4,MaximumGap=Hz/2;
 static constexpr unsigned Capacity=128,Minimum=100,Slots=48;
 static constexpr std::array<uint32_t,26> Ages{250,500,750,1000,1250,1500,1750,2000,2250,2500,2750,3000,3250,3500,3750,4000,6000,8000,12000,16000,24000,32000,48000,64000,96000,120000};
 enum Outcome:uint8_t {Pass=1,Fail=2,Unknown=3};
 private:
 struct Bin {
  uint8_t data[32]{},head=0,n=0,bad=0,unknown=0;
  void add(Outcome outcome){
   const unsigned byte=head/4,shift=(head%4)*2;
   const auto old=uint8_t((data[byte]>>shift)&3);
   if(n==Capacity){if(old!=Pass)--bad;if(old==Unknown)--unknown;}else ++n;
   data[byte]=uint8_t((data[byte]&~(3u<<shift))|(unsigned(outcome)<<shift));
   if(outcome!=Pass)++bad;
   if(outcome==Unknown)++unknown;
   head=uint8_t((head+1)%Capacity);
  }
  bool ready()const{return unsigned(n)-unknown>=Minimum;}
  bool passes()const{return ready()&&unsigned(bad)*100<=n;}
 };
 struct Run {
  uint64_t start=0,last=0;
  int64_t epoch=0;double rate=0;
  uint32_t probe_ms=0,peak=0;int32_t phase=0;uint8_t next=0;
 };
 std::array<Bin,Ages.size()> bins_{};
 std::array<Run,Slots> runs_{};
 uint8_t lane_=0;
 uint64_t next_=0,last_=0;
 uint32_t probe_=12000,started_=0,completed_=0,aborted_=0,gaps_=0;
 static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
 void finish(Run& run,bool aborted){
  for(;run.next<Ages.size()&&Ages[run.next]<=run.probe_ms;++run.next)
   bins_[run.next].add(run.peak>=1000?Fail:Unknown);
  if(aborted)inc(aborted_);else inc(completed_);
  run.start=0;
 }
 public:
 void clear(){cancel();for(auto& bin:bins_)bin={};next_=last_=0;probe_=12000;lane_=0;}
 void cancel(){for(auto& run:runs_)if(run.start)finish(run,true);}
 void tick(uint64_t at){for(auto& run:runs_)if(run.start&&at>=run.start&&at-run.start>uint64_t(run.probe_ms+1000)*20000)finish(run,true);}
 bool start(const v12::G1ClockPrediction& clock,uint64_t at){
  if(!at||at<next_||at<clock.tick||clock.output_correction_ns||clock.receiver_phase_ns||
     !std::isfinite(clock.rate)||std::abs(clock.rate)>50000)return false;
  const auto value=clock.relative_at(at),phase=clock.phase_remaining(at);
  if(value<=0||std::abs(double(phase))>=1000)return false;
  for(auto& run:runs_)if(!run.start){
   run={};run.start=run.last=at;run.epoch=value;run.rate=clock.rate;
   run.phase=int32_t(phase);run.probe_ms=lane_?2000:probe_;
   lane_=uint8_t((lane_+1)%8);inc(started_);
   constexpr uint64_t divisor=8*(Slots-4);
   const auto spacing_us=std::max<uint64_t>(62500,(uint64_t(14000+probe_)*1000+divisor-1)/divisor);
   next_=at+spacing_us*20;
   return true;
  }
  return false;
 }
 void observe(uint64_t local,uint64_t reference,uint64_t available){
  if(!local||available<local||reference>uint64_t(INT64_MAX)||local<=last_)return;
  last_=local;
  for(auto& run:runs_)if(run.start&&local>run.start){
   const auto gap=local-run.last;run.last=local;if(gap>Gap)inc(gaps_);
   const auto dt=local-run.start;
   const v12::G1ClockPrediction frozen{run.start,run.epoch,run.phase,run.rate,0};
   const auto predicted=frozen.relative_at(local);
   const auto error=predicted>0?uint64_t(std::llabs(predicted-int64_t(reference))):UINT64_MAX;
   run.peak=std::max(run.peak,uint32_t(std::min<uint64_t>(error,UINT32_MAX)));
   // A long physical witness outage cannot retrospectively certify its
   // unobserved interior. Retain an already observed crossing as failure;
   // otherwise unfinished ages are UNKNOWN, never optimistic passes.
   if(gap>MaximumGap){finish(run,true);continue;}
   for(;run.next<Ages.size()&&Ages[run.next]<=run.probe_ms&&dt>=uint64_t(Ages[run.next])*20000;++run.next)
    bins_[run.next].add(run.peak>=1000?Fail:Pass);
   if(dt>=uint64_t(run.probe_ms)*20000)finish(run,false);
  }
  const auto current=view();
  if(current.ready&&current.duration_ms>=probe_&&probe_<120000)probe_=std::min<uint32_t>(120000,probe_*2);
 }
 HoldoverView view(uint64_t at=0)const{
  HoldoverView result;result.last20=last_;result.probe_ms=probe_;result.started=started_;
  result.completed=completed_;result.aborted=aborted_;result.gaps=gaps_;
  result.trials=bins_[0].n;result.bad=bins_[0].bad;result.unknown=bins_[0].unknown;
  bool bounded=false;
  for(unsigned i=0;i<Ages.size();++i){const auto& bin=bins_[i];if(!bin.ready())break;
   if(!bin.passes()){
    if(unsigned(bin.bad-bin.unknown)*100>bin.n){bounded=true;result.ready=true;}
    break;
   }
   result.ready=true;result.duration_ms=Ages[i];result.trials=bin.n;
   result.bad=bin.bad;result.unknown=bin.unknown;
  }
  result.lower_bound=result.ready&&!bounded;
  if(at&&(!last_||at<last_||at-last_>5*Hz))result.ready=false;
  return result;
 }
};
static_assert(sizeof(EmpiricalHoldover)<=4096,"Watch sampled holdover exceeds4KiB");
}
