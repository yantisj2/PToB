#include "native_lora.hpp"
#ifdef V12_LORA_FORCED_OFF
namespace v12 {
namespace {LoRaStatus inactive;}
bool lora_start_worker(){return true;} // This reviewed C3 BLE-only image owns no LoRa task/SPI/pins.
bool lora_request(bool enable,uint32_t){
 if(enable||inactive.request_generation==UINT32_MAX)return false;
 ++inactive.request_generation;inactive.off_ack_generation=inactive.request_generation;return true;
}
LoRaStatus lora_status(){return inactive;}
}
#endif
