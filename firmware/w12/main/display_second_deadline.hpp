// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace w12 {
// Schedule the next paint from the displayed clock's fractional second. A
// page change or a slow paint never becomes a new one-second clock phase.
class DisplaySecondDeadline {
 uint64_t deadline_=0;
 public:
 bool due(uint64_t now)const{return !deadline_||now>=deadline_;}
 uint64_t deadline()const{return deadline_;}
 void painted(uint64_t sampled_us,bool valid,uint64_t utc_us){
  const uint64_t left=valid?1000000-utc_us%1000000:1000000;
  deadline_=sampled_us>UINT64_MAX-left?UINT64_MAX:sampled_us+left;
 }
};
}
