// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace n30 {
// Receiver scheduling only. LoRa availability must not hide a stalled BLE
// acquisition. A new physical corrected pair is progress; packets, JOINs and
// display polling are not. The owner still performs its original bounded
// drain and fresh authentication before any replacement clock can be used.
class BleAcquisitionRecovery {
 uint64_t since_=0,last_=0;uint32_t accepted_=0,rebinds_=0;bool watching_=false;
 public:
 static constexpr uint64_t StallUs=15000000,PeerFreshUs=10000000;
 bool observe(bool enabled,bool nearby,uint32_t accepted,uint64_t now){
  if(!enabled||!nearby){watching_=false;last_=now;accepted_=accepted;return false;}
  if(!watching_||now<last_||accepted!=accepted_){since_=now;watching_=true;}
  last_=now;accepted_=accepted;
  return now>=since_&&now-since_>=StallUs&&rebinds_!=UINT32_MAX;
 }
 void queued(){watching_=false;if(rebinds_!=UINT32_MAX)++rebinds_;}
 uint32_t rebinds()const{return rebinds_;}
};
}
