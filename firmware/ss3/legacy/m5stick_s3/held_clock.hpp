#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
namespace sticks3 {
// Presentation only. No Clock/GM/controller state, source authority, candidacy
// or qualification is returned by this cache. Epochs enter only after the
// caller's unchanged live protocol gates; loss never substitutes raw uptime.
class HeldClock {
public:
 static constexpr uint64_t Hz=20000000;
 static constexpr int64_t NoticeCorrectionNs=1000000;
 enum State:uint8_t {Waiting,Live,Held,Stale,Rejoining,Fault};
 // ns is projected UTC at observe()'s tick; reference retains the real
 // authorized mapping age and is never replaced by the display tick.
 struct Input {int64_t ns=0;uint64_t reference=0,available=0;uint32_t root=0,session=0,provider=0;int32_t rate_mppb=0;bool authorized=false,local=true;};
 struct Value {int64_t ns=0,correction_ns=0;uint32_t root=0,session=0,provider=0,age_s=UINT32_MAX;State state=Waiting;bool valid=false;};
 bool observe(const Input& in,uint64_t tick) {
  if(!in.authorized || in.local || !in.root || !in.session || in.ns<1000000000000000000LL ||
     in.ns/1000000000LL>=UINT32_MAX || in.reference>tick || in.available>tick ||
     in.available<in.reference || (have_ && tick<last_tick_))return false;
  if(have_ && in.root==source_.root && in.session==source_.session && in.reference<=source_.reference)return false;
  int64_t before=in.ns;
  if(have_ && !predict(tick,before))return false;
  const bool changed=have_ && (in.root!=source_.root || in.session!=source_.session);
  correction_=in.ns-before;backward_ns_=have_ && correction_<0?-correction_:0;
  anchor_ns_=std::max(before,in.ns);anchor_tick_=tick;source_=in;
  // A backward epoch correction is approached with an explicit slow display
  // slew (0.5%); it never steps backward or changes the protocol clock/model.
  notice_until_=(changed || (have_ && correction_>NoticeCorrectionNs))?tick+std::min<uint64_t>(2*Hz,UINT64_MAX-tick):0;
  have_=true;last_tick_=tick;last_ns_=anchor_ns_;if(accepted_!=UINT32_MAX)++accepted_;return true;
 }
 Value value(uint64_t tick,bool fresh,bool fault=false) {
  Value v{};if(!have_){v.state=fault?Fault:Waiting;return v;}
  int64_t ns=last_ns_;const bool coherent=tick>=last_tick_ && predict(tick,ns);
  if(coherent){last_ns_=std::max(ns,last_ns_);last_tick_=tick;}
  v.valid=true;v.ns=last_ns_;v.root=source_.root;v.session=source_.session;v.provider=source_.provider;v.correction_ns=correction_;
  v.age_s=tick>=source_.reference?uint32_t(std::min<uint64_t>((tick-source_.reference)/Hz,UINT32_MAX-1)):UINT32_MAX;
  // Ordinary sub-ms negative adjustments still slew monotonically. They
  // must not turn every received anchor into a whole-screen warning.
  const bool correcting=coherent && backward_ns_>=NoticeCorrectionNs &&
    int64_t((tick-anchor_tick_)*50)/200<backward_ns_;
  v.state=fault || !coherent?Fault:correcting || tick<notice_until_?Rejoining:!fresh?(v.age_s>=30?Stale:Held):Live;
  return v;
 }
 uint32_t accepted()const{return accepted_;}
 bool current(const Input& in,uint64_t tick)const {
  return have_ && in.authorized && !in.local && tick>=last_tick_ &&
    in.reference<=tick && in.available<=tick && in.ns>=1000000000000000000LL &&
    in.ns/1000000000LL<UINT32_MAX && in.root==source_.root && in.session==source_.session &&
    in.provider==source_.provider && in.reference==source_.reference && in.available==source_.available;
 }
private:
 Input source_{};int64_t anchor_ns_=0,last_ns_=0,correction_=0,backward_ns_=0;
 uint64_t anchor_tick_=0,last_tick_=0,notice_until_=0;bool have_=false;uint32_t accepted_=0;
 bool predict(uint64_t tick,int64_t& ns)const {
  if(tick<anchor_tick_)return false;
  const uint64_t ticks=tick-anchor_tick_;
  if(ticks>uint64_t(INT64_MAX/50))return false;
  const int64_t nominal=int64_t(ticks*50);
  const int64_t seconds=int64_t(ticks/Hz),part=int64_t((ticks%Hz)*50);
  const int64_t rate=std::max<int64_t>(-1000000000,std::min<int64_t>(1000000000,source_.rate_mppb));
  if(seconds && std::abs(rate)>INT64_MAX/seconds)return false;
  const int64_t adjust=seconds*rate/1000+part*rate/1000000000000LL;
  const int64_t slowed=nominal-std::min(backward_ns_,nominal/200);
  if((adjust>0 && slowed>INT64_MAX-adjust) || (adjust<0 && slowed<INT64_MIN-adjust))return false;
  const int64_t delta=slowed+adjust;
  if(delta<0 || anchor_ns_>INT64_MAX-delta || (anchor_ns_+delta)/1000000000LL>=UINT32_MAX)return false;
  ns=anchor_ns_+delta;return true;
 }
};
}
