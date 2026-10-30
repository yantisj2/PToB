// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "g1_clock_prediction.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
namespace v12 {
struct W12HorizonView {
  uint64_t last20=0;uint32_t duration_ms=0,probe_ms=12000,started=0,completed=0,aborted=0,gaps=0;
  uint16_t trials=0,bad=0,unknown=0;bool ready=false,lower_bound=false;
};
// Empirical P99 of sampled prefix maxima, inverted at the fixed 1000ns limit.
// Forty-eight frozen clocks; packed rolling128 outcomes per age. No rate forecast,
// thermal steering, future corrections, or claim about unobserved gap interiors.
class W12HoldoverHorizon {
 public:
  static constexpr uint64_t Hz=20000000,Gap=Hz/4;
  static constexpr unsigned Capacity=128,Minimum=100,Slots=48;
  static constexpr std::array<uint32_t,26> Ages{250,500,750,1000,1250,1500,1750,2000,2250,2500,2750,3000,3250,3500,3750,4000,6000,8000,12000,16000,24000,32000,48000,64000,96000,120000};
  enum Outcome:uint8_t {Pass=1,Fail=2,Unknown=3};
  struct Start {uint32_t id=0,probe_ms=0;uint64_t at20=0;int64_t epoch=0;double rate=0;int32_t phase=0;};
  struct End {uint32_t id=0,probe_ms=0,peak_ns=0,gaps=0;uint64_t start20=0,last20=0,outcomes=0;};
  using StartSink=void(*)(const Start&);using EndSink=void(*)(const End&);
 private:
  struct Bin {
    uint8_t data[32]{},head=0,n=0,bad=0,unknown=0;
    void add(Outcome o){
      const unsigned byte=head/4,shift=(head%4)*2;const auto old=uint8_t((data[byte]>>shift)&3);
      if(n==Capacity){if(old!=Pass)--bad;if(old==Unknown)--unknown;}else ++n;
      data[byte]=uint8_t((data[byte]&~(3u<<shift))|(unsigned(o)<<shift));
      if(o!=Pass){++bad;}
      if(o==Unknown){++unknown;}
      head=uint8_t((head+1)%Capacity);
    }
    bool ready()const{return unsigned(n)-unknown>=Minimum;}
    bool passes()const{return ready()&&unsigned(bad)*100<=n;} // Q99(prefix peak)<1000ns.
  };
  struct Run {
    uint64_t start=0,last=0,outcomes=0;int64_t epoch=0;double rate=0;
    uint32_t id=0,probe_ms=0,peak=0,gaps=0;int32_t phase=0;uint8_t next=0;
  };
  std::array<Bin,Ages.size()> bins_{};std::array<Run,Slots> runs_{};
  uint8_t lane_=0;uint64_t next_=0,last_=0;uint32_t probe_=12000,started_=0,completed_=0,aborted_=0,gaps_=0;
  StartSink start_sink_=nullptr;EndSink end_sink_=nullptr;
  static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
  void outcome(Run& r,unsigned i,Outcome o){bins_[i].add(o);r.outcomes|=uint64_t(o)<<(2*i);}
  void finish(Run& r,bool aborted){
    for(;r.next<Ages.size()&&Ages[r.next]<=r.probe_ms;++r.next)outcome(r,r.next,r.peak>=1000?Fail:Unknown);
    if(aborted)inc(aborted_);else inc(completed_);
    if(end_sink_)end_sink_({r.id,r.probe_ms,r.peak,r.gaps,r.start,r.last,r.outcomes});
    r.start=0;
  }
 public:
  void sinks(StartSink a,EndSink b){start_sink_=a;end_sink_=b;}
  void clear(){for(auto& r:runs_)if(r.start)finish(r,true);for(auto& b:bins_)b={};
    next_=last_=0;probe_=12000;lane_=0;}
  void cancel(){for(auto& r:runs_)if(r.start)finish(r,true);}
  void tick(uint64_t at){for(auto& r:runs_)if(r.start&&at>=r.start&&at-r.start>uint64_t(r.probe_ms+1000)*20000)finish(r,true);}
  bool due(uint64_t at)const{return at&&at>=next_;}
  bool start(const G1ClockPrediction& clock,uint64_t at){
    if(!at||at<next_||clock.output_correction_ns||clock.thermal.beta!=0||!std::isfinite(clock.rate)||std::abs(clock.rate)>50000)return false;
    const auto value=clock.relative_at(at)-clock.receiver_phase_ns,phase=clock.phase_remaining(at);
    if(value<=0||std::abs(double(phase))>=1000)return false;
    for(auto& r:runs_)if(!r.start){
      r={};r.start=r.last=at;r.epoch=value;r.rate=clock.rate;r.phase=int32_t(phase);r.probe_ms=lane_?2000:probe_;lane_=uint8_t((lane_+1)%8);inc(started_);r.id=started_;
      // Seven short lanes feed early P99 while one lane keeps extending the
      // long horizon. Weighted occupancy leaves four slots for witness delay.
      constexpr uint64_t divisor=8*(Slots-4);
      const auto spacing_us=std::max<uint64_t>(62500,(uint64_t(14000+probe_)*1000+divisor-1)/divisor);
      next_=at+spacing_us*20;
      if(start_sink_){start_sink_({r.id,r.probe_ms,at,r.epoch,r.rate,r.phase});}
      return true;
    }
    return false;
  }
  void observe(uint64_t local,uint64_t reference,uint64_t available){
    if(!local||available<local||reference>uint64_t(INT64_MAX)||local<=last_)return;
    last_=local;
    for(auto& r:runs_)if(r.start&&local>r.start){
      const auto gap=local-r.last;r.last=local;if(gap>Gap){inc(r.gaps);inc(gaps_);}
      const auto dt=local-r.start;const double t=double(dt)/Hz;
      const auto slew=std::copysign(std::min(std::abs(double(r.phase)),t*1000),double(r.phase));
      const auto prediction=r.epoch+int64_t(dt)*50+int64_t(std::llround(t*r.rate+slew));
      const auto error=uint64_t(std::llabs(prediction-int64_t(reference)));
      r.peak=std::max(r.peak,uint32_t(std::min<uint64_t>(error,UINT32_MAX)));
      for(;r.next<Ages.size()&&Ages[r.next]<=r.probe_ms&&dt>=uint64_t(Ages[r.next])*20000;++r.next){
        // The first actual witness at/after an age includes the full sampled
        // prefix and any overshoot. Gaps stay explicit; no gap is interpolated.
        // Only expiry without a confirming witness leaves an age unknown.
        outcome(r,r.next,r.peak>=1000?Fail:Pass);
      }
      if(dt>=uint64_t(r.probe_ms)*20000)finish(r,false);
    }
    const auto v=view();
    if(v.ready&&v.duration_ms>=probe_&&probe_<120000)probe_=std::min<uint32_t>(120000,probe_*2);
  }
  W12HorizonView view(uint64_t at=0)const{
    W12HorizonView v;v.last20=last_;v.probe_ms=probe_;v.started=started_;v.completed=completed_;v.aborted=aborted_;v.gaps=gaps_;
    bool bounded=false;v.trials=bins_[0].n;v.bad=bins_[0].bad;v.unknown=bins_[0].unknown;
    for(unsigned i=0;i<Ages.size();++i){const auto& b=bins_[i];if(!b.ready())break;
      if(!b.passes()){
        // Missing evidence can limit a lower bound, but cannot invent an early
        // accuracy failure or a numeric horizon when no age has qualified.
        if(unsigned(b.bad-b.unknown)*100>b.n){bounded=true;v.ready=true;}
        break;
      }
      v.ready=true;v.duration_ms=Ages[i];v.trials=b.n;v.bad=b.bad;v.unknown=b.unknown;
    }
    v.lower_bound=v.ready&&!bounded;
    if(at&&(!last_||at<last_||at-last_>5*Hz))v.ready=false;
    return v;
  }
};
static_assert(sizeof(W12HoldoverHorizon)<=4096,"Bounded sampled hold-duration RAM");
}
