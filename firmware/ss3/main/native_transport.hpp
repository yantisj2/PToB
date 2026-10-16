#pragma once
#include "client_lifecycle.hpp"
#include "console_protocol.hpp"
#include "periodic_rx_model.hpp"
namespace v12 {
struct Report {
  uint64_t callback_us=0;uint32_t cycle=0;uint16_t handle=0;
  uint16_t actual_length=0;
  uint8_t kind=0,status=0,length=0,sid=0,refusal=0;int8_t rssi=127;
  uint8_t address_type=0,address_le[6]{};
  // Mask bit0: public extended-discovery fields below are present. Other
  // callback kinds must not interpret their default values as observations.
  uint8_t metadata_mask=0,discovery_props=0,legacy_event_type=0;
  uint8_t primary_phy=0,secondary_phy=0;int8_t advertised_tx_power=127;
  uint16_t periodic_interval=0; // Public controller units: 1.25ms, 0=no PA.
  uint8_t bytes[MaxData]{};
  periodic_rx::Stamp rx_stamp{};
};
struct TransportStatus {
  ClientLifecycle client{};OwnerLease lease{};uint32_t reports=0,incomplete=0,oversize=0,late=0;
  uint32_t api_errors=0,resets=0,queue_drops=0,queue_peak=0;
  uint64_t last_api_us=0,max_api_us=0,logged_bytes=0;
  bool host_ready=false,radio_requested=false,physical_quiet=false,host_work_busy=false,command_pending=false,off_acknowledged=true;
};
bool transport_init();
// Finite receive-only diagnostic treatments; no precision or source authority.
bool transport_request(Mode mode,uint32_t duration_ms,uint64_t request_id=0);
void transport_stop();
// Bounded main-task deadline observer, no HCI or console work.
void transport_observe_lease(uint64_t now_us);
bool transport_pop(Report& report);
#ifdef V12_R2_ENABLED
// Complete bounded callback evidence, consumed by the existing console worker.
void transport_record(const Report& report);
#if V12_R2_CONFIG == 13
// One fixed host-event tail entry; never another task or radio owner.
void transport_g1_cleanup_schedule();
#endif
#endif
TransportStatus transport_status();
}
