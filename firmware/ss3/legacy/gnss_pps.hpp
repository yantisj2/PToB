#pragma once
#include <cstdint>
#include "driver/gptimer.h"
#include "ptob_gps_shadow_clock.hpp"
#include "ptob_l76k_aid.hpp"
namespace gnss_pps {
struct Observation {
 uint64_t tick=0,anchor_available=0;uint32_t capture=0,number=0,map_bracket_ns=0;
 bool mapped=false;
 uint64_t raw_extended=0;
};
using Consumer=void (*)(const Observation&);
using NmeaConsumer=void (*)(const uint8_t*,unsigned);
// Optional bounded main-task observer of the actual UART-delivered chunk.
// No timestamp/UTC association is inferred and no ISR invokes this observer.
void set_nmea_consumer(NmeaConsumer consumer);
// Pins are disabled unless a verified board profile explicitly enables them.
// RX-only UART and input-only PPS; reviewed board power is a separate build gate.
bool init(gptimer_handle_t clock,Consumer consumer);
void poll(uint64_t tick);
// Main-task, once per MCU boot, fresh authenticated BLE time only.
void aid_time(const ptob_l76k::TimeAid&,uint64_t now_us);
bool ble_aided();
unsigned aid_state();
void status(uint64_t tick);
// One-shot C-3 Meshnology GPIO config/input readings; no writes/rail proof.
bool pins(uint64_t tick);
// C-3 observer only: switch UART's input matrix38/39; never change outputs.
bool select_rx(int gpio);
// L76K documented rates; C-3 Meshnology only, RX driver settings only.
bool select_baud(uint32_t baud);
// One fixed non-setting L76K CFG-PRT GET; temporary TX38 then high-Z.
bool query_port(uint64_t tick);
// Main-task activity only; lamps do not imply UTC/PPS association or precision.
struct Activity {uint64_t packet20=0,pps20=0;uint32_t packets=0;bool fix=false;};
Activity activity(uint64_t tick);
struct Reference {uint64_t rmc20=0;uint32_t second=0,epoch=0,coherent=0;bool fix=false;};
Reference reference();
// Independent local GPS clock; never alters the selected BLE consumer.
ptob_gps::Shadow::View shadow_view(uint64_t tick);
bool shadow_enabled();
bool source_models_ready();
bool pps_recent(uint64_t tick);
// Finite local mapping/observer treatments; power, UART TX and BLE unchanged.
bool select_map(bool guarded);
bool shadow_logging(bool enabled);
}
