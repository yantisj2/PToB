// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace v12 {
// One short passive management window per UTC quarter frame, between actual
// 62.5ms PA receptions. No fresh capture/model means no additional scanning.
class GmControlScan {
  uint64_t start_=0,until_=0,capture_=0,frame_=0,last_=0;
 public:
  void clear(){*this={};}
  bool allows(uint64_t now)const{return start_&&now>=start_&&now<until_;}
  bool observed(uint64_t at)const{return start_&&at>=start_&&at<until_;}
  bool poll(uint64_t now,bool eligible,uint64_t capture,uint64_t age,uint64_t utc){
    if(now<last_||!eligible){clear();last_=now;return false;}last_=now;
    if(now<until_)return allows(now);
    if(!capture||capture==capture_||age>=25000||!utc||now>UINT64_MAX-250000||utc>UINT64_MAX-250000)return false;
    const auto phase=utc%250000;
    constexpr uint64_t Begin=32000,End=50000;
    if(phase>=Begin||age+Begin-phase<8000||age+End-phase>56000||utc-phase+End<=frame_)return false;
    capture_=capture;frame_=utc-phase+End;start_=now+Begin-phase;until_=now+End-phase;return false;
  }
};
}
