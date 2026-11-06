// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "fine_counter.hpp"
#include <limits>
namespace n30 {
// Group0's independent APB80 counter. Coarse time checks health only; neither
// the BLE model nor GPTimer phase/frequency contributes to this coordinate.
// The existing mapped-path predecessor/enclosure guard remains mandatory.
class LoRaNativeClock {
 fine::Counter counter_;
 uint64_t epoch_=0,origin_=0,latest_=0;uint32_t raw_=0;
 public:
 bool current(uint64_t epoch,uint32_t raw,uint64_t coarse,uint64_t& tick16){
  tick16=0;
  if(!epoch||epoch>UINT32_MAX)return false;
  if(!epoch_){
   if(!counter_.begin(uint32_t(epoch),raw,coarse,80000000))return false;
   epoch_=epoch;origin_=raw;
  }
  if(epoch!=epoch_)return false;
  fine::Time t;if(!counter_.current(uint32_t(epoch),raw,coarse,t))return false;
  raw_=raw;latest_=t.raw80;
  tick16=UINT64_C(16000000)+(latest_-origin_)/5;return true;
 }
 bool event(uint64_t epoch,uint32_t raw,uint64_t& tick16)const{
  tick16=0;if(!epoch_||epoch!=epoch_||!counter_.live())return false;
  // LoRa's original eight-second HeaderValid/FIFO-consumption limit. It is
  // strictly below the half-wrap ambiguity bound; BLE's250ms limit is separate.
  const uint32_t age=raw_-raw;
  if(age>UINT64_C(8000000)*80||age>latest_||latest_-age<origin_)return false;
  tick16=UINT64_C(16000000)+(latest_-age-origin_)/5;return true;
 }
 void reset(){*this={};}
 uint8_t fault()const{return uint8_t(counter_.fault());}
};
}
