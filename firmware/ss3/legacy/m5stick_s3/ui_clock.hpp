#pragma once
#include "held_clock.hpp"
namespace sticks3 {
// Core0 exports values only. Core1 owns every presentation clock/filter.
// A packet's ns belongs to its tick, never to its later queue consumption.
struct TimingSnapshot {
 HeldClock::Input input{};
 uint64_t tick=0,fresh_until=0,trace_until=0;
 uint32_t selected_root=0;
 bool fresh=false,fault=false,permission=false,ready=false,authority=false;
 bool absolute=false,precision=false,model=false,published=false;
};
class UIClock {
public:
 static constexpr uint64_t ProducerLife=HeldClock::Hz;
 void consume(const TimingSnapshot& p,uint64_t dispatch) {
  packet_=p;have_=true;
  if(dispatch<p.tick || dispatch-p.tick>=ProducerLife)return;
  auto input=p.input;
  const uint64_t ticks=dispatch-p.tick;
  const int64_t nominal=int64_t(ticks*50);
  const int64_t rate=std::max<int64_t>(-1000000000,std::min<int64_t>(1000000000,input.rate_mppb));
  const int64_t delta=nominal+nominal*rate/1000000000000LL;
  if(input.ns<0 || input.ns>INT64_MAX-delta)return;
  input.ns+=delta;
  (void)clock_.observe(input,dispatch);
 }
 HeldClock::Value value(uint64_t tick,bool timer_ok=true) {
  const bool coherent=have_ && timer_ok && tick>=packet_.tick;
  const bool fresh=coherent && tick-packet_.tick<ProducerLife && packet_.fresh &&
   tick<packet_.fresh_until && clock_.current(packet_.input,tick);
  return clock_.value(tick,fresh,!timer_ok || (have_ && (!coherent || packet_.fault)));
 }
 const TimingSnapshot& packet()const{return packet_;}
 uint32_t accepted()const{return clock_.accepted();}
private:
 HeldClock clock_;TimingSnapshot packet_{};bool have_=false;
};
}
