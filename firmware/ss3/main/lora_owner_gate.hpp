#pragma once
#include <cstdint>
namespace v12 {
// Sole-worker OFF acknowledgement. Call under the native adapter's lock.
struct LoRaOwnerGate {
  uint32_t generation=0,off_ack=0;bool requested=false,busy=false,stuck=false;
  bool request(bool enable){if(generation==UINT32_MAX)return false;
    requested=enable;++generation;return true;}
  uint32_t begin(){busy=true;return generation;}
  void finish(uint32_t consumed,bool rails_off,bool all_spi_returned){
    if(!all_spi_returned){stuck=true;return;}busy=false;
    if(consumed==generation&&!requested&&rails_off)off_ack=consumed;}
  bool drained()const{return !requested&&!busy&&!stuck&&off_ack==generation;}
  bool idle_uninitialized(bool initialized)const{return !initialized&&drained();}
};
}
