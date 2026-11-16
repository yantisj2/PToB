// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace n30 {
// Watch-local implementation of the shared receiver's policy interface.
// Include order selects this header only for T-Watch. After actual corrected
// progress, discovery silence must not disable the acquisition watchdog:
// the last locator expires at10s, before the15s stall deadline. Display-only
// retention is deliberately independent and must not prevent radio recovery.
class BleAcquisitionRecovery {
 uint64_t since_=0,last_=0,retry_at_=0;
 uint32_t accepted_=0,rebinds_=0;
 bool watching_=false,established_=false;
 public:
 static constexpr uint64_t StallUs=15000000,PeerFreshUs=10000000,RetryUs=60000000;
 bool observe(bool enabled,bool nearby,uint32_t accepted,uint64_t now){
  if(now<last_){watching_=false;retry_at_=0;}
  const bool progress=accepted!=accepted_;
  // Accepted is the owner's cumulative physically corrected-pair counter;
  // neither a locator, JOIN, retained clock nor RSSI counts as progress.
  if(enabled&&accepted&&progress)established_=true;
  if(!enabled||(!nearby&&!established_)){
   watching_=false;last_=now;accepted_=accepted;return false;
  }
  if(!watching_||progress){since_=now;watching_=true;}
  if(progress)retry_at_=0;
  last_=now;accepted_=accepted;
  return now>=since_&&now-since_>=StallUs&&now>=retry_at_&&rebinds_!=UINT32_MAX;
 }
 void queued(){
  watching_=false;
  retry_at_=last_>UINT64_MAX-RetryUs?UINT64_MAX:last_+RetryUs;
  if(rebinds_!=UINT32_MAX)++rebinds_;
 }
 uint32_t rebinds()const{return rebinds_;}
};
}
