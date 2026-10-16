#pragma once
#include <cstdint>
namespace sticks3 {
// USB maintenance only between completed scan procedures. A missing host never
// authorizes unlimited resets, any RF transmission, or a clock-quality claim.
class USBRecoveryPolicy {
public:
 void attached(uint64_t ms){last_=ms;down_=0;tracking_=false;++attempts_;}
 bool due(uint64_t ms,bool connected,bool radio_idle) {
  if(connected){tracking_=false;return false;}
  if(!tracking_){down_=ms;tracking_=true;}
  return radio_idle && attempts_<4 && ms>=last_ && ms-last_>=10000 &&
         ms>=down_ && ms-down_>=3000;
 }
 unsigned attempts()const{return attempts_;}
private:
 uint64_t last_=0,down_=0;unsigned attempts_=0;bool tracking_=false;
};
}
