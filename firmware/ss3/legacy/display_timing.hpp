#pragma once
#include <cstdint>

// SSD1315 D5: frequency selector in the high nibble, divisor - 1 below.
// The highest setting is supported, but does not specify an exact Hz multiplier.
class DisplayTiming {
public:
 enum Profile {Fast,Normal,Slow};
 static constexpr uint64_t Hz=20000000;
 static constexpr uint8_t clock(Profile p){return p==Fast?0xf0:p==Slow?0x81:0x80;}
 static constexpr uint64_t interval(Profile p){return p==Normal?Hz/4:p==Slow?Hz:Hz/2;}
 void profile(Profile p){profile_=p;frozen_=false;last_=0;}
 void freeze(bool value){frozen_=value;if(!value)last_=0;}
 bool frozen() const{return frozen_;}
 bool due(uint64_t now){
  if(frozen_ || (last_ && now>=last_ && now-last_<interval(profile_)))return false;
  last_=now;return true;
 }
private:
 Profile profile_=Fast;
 bool frozen_=false;
 uint64_t last_=0;
};
