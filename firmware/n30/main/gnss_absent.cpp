// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Compatibility API only. No GNSS driver, UART, GPIO, capture, model or aid.
#include "gnss_pps.hpp"
namespace gnss_pps {
void set_nmea_consumer(NmeaConsumer){}
bool init(gptimer_handle_t,Consumer){return false;}
void poll(uint64_t){}
void aid_time(const ptob_l76k::TimeAid&,uint64_t){}
bool ble_aided(){return false;}
unsigned aid_state(){return 0;}
void status(uint64_t){}
bool pins(uint64_t){return false;}
bool select_rx(int){return false;}
bool select_baud(uint32_t){return false;}
bool query_port(uint64_t){return false;}
Activity activity(uint64_t){return {};}
Reference reference(){return {};}
ptob_gps::Shadow::View shadow_view(uint64_t){return {};}
bool shadow_enabled(){return false;}
bool source_models_ready(){return false;}
bool pps_recent(uint64_t){return false;}
bool select_map(bool){return false;}
bool shadow_logging(bool){return false;}
}
