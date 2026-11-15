// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace watch {
// Admission follows the console's factory/session/fresh-ticket checks. The
// separate ACK deadline never extends the original maintenance drain.
struct Bootrom {
 uint64_t nonce=0,deadline=0;
 bool request(uint64_t id,uint64_t now,bool drained){
  if(!drained||nonce||!id||now>UINT64_MAX-1500000)return false;
  nonce=id;deadline=now+1500000;return true;
 }
 bool poll(uint64_t now,bool drained,uint64_t completed,bool usb_drained){
  if(!nonce)return false;
  if(now>=deadline||!drained){nonce=0;return false;}
  if(completed!=nonce||!usb_drained)return false;
  nonce=0;return true;
 }
};
}
namespace v12 {uint64_t watch_completed_frame();}
