// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "ui.hpp"
#include <cstring>
namespace n30 {
constexpr uint64_t FrameWorkUs=50000;
struct FramePhase {
 uint64_t slot,edge,next;uint32_t period;bool utc;
};
inline FramePhase frame_phase(const Model& m,const View& v,uint64_t now){
 const uint32_t period=v.saver&&v.minimal?1000000:v.page==1&&!v.saver?125000:250000;
 uint64_t clock=now;const bool valid=time(m,now,clock);
 if(!valid)clock=now; // Unavailable time never acquires a UTC phase.
 if(v.saver&&v.minimal){
  // Two edges, not a2Hz grid:250ms lit,750ms with no routine render/I2C.
  const auto phase=clock%1000000;
  const bool dark=phase>=250000;
  const auto elapsed=phase-(dark?250000:0),wait=(dark?1000000:250000)-phase;
  return {2*(clock/1000000)+unsigned(dark),now>=elapsed?now-elapsed:0,
          now<=UINT64_MAX-wait?now+wait:UINT64_MAX,period,valid};
 }
 const auto phase=clock%period,wait=period-phase;
 return {clock/period,now>=phase?now-phase:0,
         now<=UINT64_MAX-wait?now+wait:UINT64_MAX,period,valid};
}
// Remember the slot actually rendered, not a chain of relative sleeps. New
// snapshots rephase the next wake; late frames skip directly to the live slot.
class FrameCadence {
 FramePhase shown_{};bool seen_=false;
 public:
 uint64_t deadline(const FramePhase& p,uint64_t now)const{
  if(!seen_||p.utc!=shown_.utc||p.period!=shown_.period)return now;
  return p.slot==shown_.slot?p.next:p.edge;
 }
 // Coalesce a forced page/wake frame near the next clock edge. Starting a
 // whole OLED transfer just before that edge would block its scheduled frame.
 uint64_t forced_deadline(const FramePhase& p,uint64_t now)const{return p.utc&&p.next>now&&p.next-now<FrameWorkUs?p.next:now;}
 void rendered(const FramePhase& p){shown_=p;seen_=true;}
 void completed(const FramePhase& started,const FramePhase& ended){
  // A late/forced transfer may occupy a newer slot. Drop that missed slot;
  // never issue an immediate catch-up burst or pretend it began on the edge.
  rendered(started.utc==ended.utc&&started.period==ended.period?ended:started);
 }
};
// N30-specific ordering: send the native pages containing the visual PPS and
// rightmost seconds digits first, in either portrait rotation. Like W12's
// display_flush, commit the shadow only after every changed span succeeds.
template<class Send> bool flush_clock_first(const uint8_t* frame,uint8_t* shadow,bool& valid,bool cw,Send send){
 for(unsigned i=0;i<8;++i){
  const unsigned page=cw?7-i:i;unsigned first=0,last=127;
  if(valid){
   while(first<128&&frame[page*128+first]==shadow[page*128+first])++first;
   if(first==128)continue;
   while(last>first&&frame[page*128+last]==shadow[page*128+last])--last;
  }
  if(!send(page,first,frame+128*page+first,last-first+1)){valid=false;return false;}
 }
 std::memcpy(shadow,frame,FrameBytes);valid=true;return true;
}
}
