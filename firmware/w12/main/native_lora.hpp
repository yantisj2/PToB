#pragma once
#include <cstdint>
namespace v12 {
struct LoRaStatus {bool initialized=false,enabled=false,failed=false,requested=false,worker_busy=false,off_acknowledged=true;
  uint32_t request_generation=0,off_ack_generation=0;
  uint32_t packets=0,crc_errors=0,malformed=0,radio_errors=0;uint64_t work_max_us=0;int16_t status=0;};
bool lora_start_worker();
bool lora_request(bool enable,uint32_t duration_ms);
LoRaStatus lora_status();
}
