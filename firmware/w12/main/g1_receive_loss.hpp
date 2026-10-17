#pragma once
#include <cstdint>
namespace v12 {
// Passive worker observations, not RF opportunity/DONE/CRC measurements.
// One frozen eight-entry history at actual corrected-model expiry. A new
// accepted physical correction begins another lifecycle. No timer ownership.
struct G1ReceiveLoss {
  enum Outcome:uint32_t {Report=1,Stale=2,Authenticated=4,Associated=8,Mapped=16,Refused=32};
  uint64_t frozen_at=0;
  uint32_t generation=0,sequence=0,count=0,frozen=0;
  struct Entry {uint64_t at=0;uint32_t outcome=0,epoch=0;} entries[8]{};
  void begin(){if(generation!=UINT32_MAX)++generation;sequence=count=frozen=0;frozen_at=0;for(auto& e:entries)e={};}
  void observe(uint64_t now,uint32_t outcome,uint32_t epoch){
    if(frozen||sequence==UINT32_MAX)return;
    entries[sequence%8]={now,outcome,epoch};++sequence;if(count<8)++count;
  }
  void freeze(uint64_t now){if(!generation||frozen)return;frozen=1;frozen_at=now;}
};
static_assert(sizeof(G1ReceiveLoss)==152,"Passive W12 receive capsule allocation");
}
