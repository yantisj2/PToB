#pragma once
#include <cstdint>
namespace v12 {
// Contract requested of a supported controller-owned extension. The pinned
// controller has no provider. A GAP callback must never manufacture this row.
constexpr uint32_t CaptureAbi=0x56313201;
enum class Edge:uint8_t { None, TxAddressEnd, RxAddressEnd };
struct Capture {
  uint32_t abi=0;Edge edge=Edge::None;uint8_t channel=0,phy=0;
  uint64_t boot=0,train=0,tick=0;uint32_t event=0,generation=0;
  uint8_t selected_payload_sha256[32]{};
  uint32_t counter_hz=0,capture_bound_ns=UINT32_MAX,calibration_bound_ns=UINT32_MAX;
  uint64_t provider_incarnation=0,clock_incarnation=0,resource_contract=0;
  bool actual=false,complete_single_pdu=false,controller_owned=false;
};
struct CaptureProvider {
  bool registered=false;uint64_t provider=0,clock=0,resources=0;uint32_t hz=0;
};
inline bool precision_capture(const Capture& c,const CaptureProvider& p=CaptureProvider{}){
  uint8_t hash=0;for(uint8_t byte:c.selected_payload_sha256)hash|=byte;
  return c.abi==CaptureAbi&&c.actual&&c.controller_owned&&c.complete_single_pdu&&
    (c.edge==Edge::TxAddressEnd||c.edge==Edge::RxAddressEnd)&&c.boot&&c.train&&c.tick&&c.generation&&c.event!=UINT32_MAX&&
    c.channel<=36&&c.phy==1&&c.counter_hz&&
    hash&&p.registered&&p.provider&&p.clock&&p.resources&&p.hz&&
    c.provider_incarnation==p.provider&&c.clock_incarnation==p.clock&&c.resource_contract==p.resources&&c.counter_hz==p.hz&&
    c.capture_bound_ns!=UINT32_MAX&&c.calibration_bound_ns!=UINT32_MAX;
}
struct PhysicalSupport {
  bool capture=false,event_binding=false,payload_selection=false,phase=false;
  bool mapped_deadlines=false,measured_stop=false,source=false,permission=false;
  bool admit_publication()const{
    return capture&&event_binding&&payload_selection&&phase&&mapped_deadlines&&
      measured_stop&&source&&permission;
  }
};
}
