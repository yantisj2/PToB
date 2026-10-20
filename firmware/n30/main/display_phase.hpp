// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "ui.hpp"
#include <cstring>
namespace n30 {
struct FramePhase {
 uint64_t slot,edge,next;uint32_t period;bool utc;
};
inline FramePhase frame_phase(const Model& m,const View& v,uint64_t now){
 const uint32_t period=v.page==1&&!v.saver?125000:250000;
 uint64_t clock=now;const bool valid=time(m,now,clock);
 if(!valid)clock=now; // Unavailable time never acquires a UTC phase.
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
 void rendered(const FramePhase& p){shown_=p;seen_=true;}
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
