// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "native_lora.hpp"
#include "n30_lora.hpp"
namespace v12 {
bool lora_start_worker(){return true;}
bool lora_request(bool,uint32_t){return false;}
LoRaStatus lora_status(){LoRaStatus s;s.initialized=true;return s;}
}
void n30_lora_control(bool,uint64_t,bool){}
bool n30_lora_mode(n30::LoRaMode){return false;}
bool n30_lora_cycle(){return false;}
void n30_lora_snapshot(n30_lora_view& v){v={};v.off_acknowledged=v.spi_retired=v.capture_released=v.reset_asserted=true;}
void n30_lora_diagnostics(v12::ConsoleFrame& f){f.format("CORESE_RADIO LoRa=ABSENT GPS=ABSENT relay=FORBIDDEN client=ONLY\n");}
