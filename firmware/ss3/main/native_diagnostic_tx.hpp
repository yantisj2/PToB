#pragma once
#include <cstdint>
#include "console_protocol.hpp"
namespace v12 {
struct DiagnosticTxStatus {bool requested=false,periodic=false,discovery=false,fault=false;
  OwnerLease lease{};bool owner_busy=false,off_acknowledged=true;uint32_t run=0;
  uint16_t bytes=0;uint32_t api_errors=0,completed=0;uint64_t until_us=0;
  // Public configure response/local identity only; neither proves emission.
  int8_t selected_tx_power=127;bool selected_power_available=false,public_address_available=false;
  uint8_t public_address_le[6]{};bool completion_reason_available=false;
  int completion_reason=0;uint8_t reported_completed_events=0;
  // num_ext_adv_events is invalid when max_events=0 (our finite duration path).
  bool completed_events_available=false;
};
// Public native controller diagnostic only. Payload kindsF0/F1 explicitly
// carry no correction/source/permission/key authority and are never V12 timing.
void diagnostic_tx_identity(const uint8_t factory[6],uint64_t session);
void diagnostic_tx_init();
void diagnostic_tx_poll();
void diagnostic_tx_reset();
bool diagnostic_tx_request(uint16_t bytes,uint32_t duration_ms,uint64_t request_id=0);
void diagnostic_tx_stop();
void diagnostic_tx_observe_lease(uint64_t now_us);
DiagnosticTxStatus diagnostic_tx_status();
}
