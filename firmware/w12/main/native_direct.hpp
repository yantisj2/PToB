#pragma once
#include "direct_auth.hpp"
namespace v12 {
enum class DirectStage:uint8_t {Off,Connecting,Security,Mtu,Service,Characteristics,Descriptors,
  WorkerHello,WriteHello,ReadHello,WorkerConfirm,WriteConfirm,ReadConfirm,Subscribe,Established,Closing,Fault};
struct DirectStatus {
  DirectStage stage=DirectStage::Off;LinkRole role=LinkRole::None;uint32_t incarnation=0;
  uint16_t handle=UINT16_MAX,mtu=0;bool credentials=false,encrypted=false,confirmed=false;
  uint32_t refusals=0,api_errors=0,queue_drops=0,queue_peak=0,crypto_errors=0;
  uint32_t control_refused=0,grant_verified=0,grant_freshness_unknown=0;
  uint64_t crypto_max_us=0,connected_until=0;
};
bool direct_init(const uint8_t factory_mac[6],uint8_t self);
// Native host calls these on its event queue. No formatting or crypto occurs.
void direct_host_poll();
void direct_host_reset();
// Explicit finite diagnostic ACL: exact role, target address, original lease.
// Peer address is a route, never silicon/key authentication.
bool direct_request(LinkRole role,uint8_t address_type,const uint8_t address_le[6],
                    uint64_t root_permit,uint32_t duration_ms);
void direct_stop();
DirectStatus direct_status();
}
