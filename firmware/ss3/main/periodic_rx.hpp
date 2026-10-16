#pragma once
#include "periodic_rx_model.hpp"
namespace v12::periodic_rx {
struct RfReference {bool valid=false;uint8_t phy=255,extra_us=0;uint32_t epoch=0;};
#ifdef V12_R2_RX_SHADOW
bool start();void poll();uint32_t scope(uint16_t handle,bool acquiring=false);bool adopt(uint16_t handle,uint32_t epoch);bool stop();
bool match(uint16_t handle,const uint8_t* bytes,size_t size,uint64_t callback20,Stamp& stamp);
Stats status();
RfReference rf_reference();
#else
inline bool start(){return false;}inline void poll(){}inline uint32_t scope(uint16_t,bool=false){return 0;}inline bool adopt(uint16_t,uint32_t){return false;}inline bool stop(){return true;}
inline bool match(uint16_t,const uint8_t*,size_t,uint64_t,Stamp& stamp){stamp={};return false;}
inline Stats status(){return {};}
inline RfReference rf_reference(){return {};}
#endif
}
