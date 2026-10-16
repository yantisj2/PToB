#pragma once
#include <cstdint>
namespace v12 {
// Passive re-observation only. The normal client windows and controller owner
// still govern scanning; every subsequent GM admission remains authenticated.
struct G1GmDiscovery {
  static constexpr uint64_t IntervalUs=30000000,WindowUs=1500000;
  uint64_t next=0,until=0,last=0;
  bool observe(uint64_t now,bool eligible){
    if(!eligible||now<last){next=until=0;last=now;return false;}
    last=now;
    if(!next)next=now>UINT64_MAX-IntervalUs?UINT64_MAX:now+IntervalUs;
    if(now>=next){
      until=now>UINT64_MAX-WindowUs?UINT64_MAX:now+WindowUs;
      next=now>UINT64_MAX-IntervalUs?UINT64_MAX:now+IntervalUs;
    }
    return until&&now<until;
  }
  bool active(uint64_t now)const{return until&&now>=last&&now<until;}
};
}
