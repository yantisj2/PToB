#pragma once
#include <cstdint>
namespace v12 {
// Passive worker observations, not RF opportunity/DONE/CRC measurements.
// Preserve the first frozen eight-entry history for this boot, even through
// recovery before a reader can request it. Later expiries/reacquisitions are
// counted, without silently replacing their missing detailed history.
struct G1ReceiveLoss {
  enum Outcome:uint32_t {Report=1,Stale=2,Authenticated=4,Associated=8,Mapped=16,Refused=32};
  uint64_t frozen_at=0;
  uint32_t generation=0,sequence=0,count=0,frozen=0,expiries=0,reacquisitions=0;
  struct Entry {uint64_t at=0;uint32_t outcome=0,epoch=0;} entries[8]{};
  void begin(){if(!generation)generation=1;}
  void reacquired(){if(frozen&&reacquisitions!=UINT32_MAX)++reacquisitions;}
  void observe(uint64_t now,uint32_t outcome,uint32_t epoch){
    if(frozen||sequence==UINT32_MAX)return;
    entries[sequence%8]={now,outcome,epoch};++sequence;if(count<8)++count;
  }
  void freeze(uint64_t now){if(!generation)return;if(expiries!=UINT32_MAX)++expiries;if(frozen)return;frozen=1;frozen_at=now;}
};
static_assert(sizeof(G1ReceiveLoss)==160,"Passive W12 receive capsule allocation");
}
