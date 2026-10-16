#pragma once
#include <cstdint>
namespace v12 {
enum class UpdateStage:uint8_t {Idle,Draining,Ready,Fault};
struct Services {bool scan=false,pending=false,sync=false,periodic=false,discovery=false,acl=false,lora=false,host=false,fault=false;};
struct UpdateGate {
  UpdateStage stage=UpdateStage::Idle;uint64_t deadline=0;uint32_t timeouts=0;
  bool begin(uint64_t now){if(stage!=UpdateStage::Idle||now>UINT64_MAX-2000000)return false;
    stage=UpdateStage::Draining;deadline=now+2000000;return true;}
  void poll(uint64_t now,const Services& s){if(stage!=UpdateStage::Draining)return;
    if(now>=deadline||s.fault){stage=UpdateStage::Fault;if(timeouts!=UINT32_MAX)++timeouts;return;}
    if(!s.scan&&!s.pending&&!s.sync&&!s.periodic&&!s.discovery&&!s.acl&&!s.lora&&!s.host)stage=UpdateStage::Ready;}
  bool latched()const{return stage!=UpdateStage::Idle;}
};
}
