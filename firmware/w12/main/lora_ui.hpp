// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "lora_view.h"
namespace w12 {
// Presentation only. LIVE is supplied by the unchanged functional clock gate.
enum class LoRaUiPhase : uint8_t { Disabled, Off, Starting, Fault, Stale, WaitMeta, Withdrawn, WaitPair, PairOld, Training, FitWait, Live, Blocked };
inline LoRaUiPhase lora_ui_phase(const w12_lora_view& v,bool configured,uint64_t now,bool live,bool ble_fault=false){
 if(!configured)return LoRaUiPhase::Disabled;
 if(v.fault)return LoRaUiPhase::Fault;
 if(ble_fault)return LoRaUiPhase::Blocked;
 if(v.mode==2||!v.requested)return LoRaUiPhase::Off;
 if(!v.active)return LoRaUiPhase::Starting;
 if(now<v.sampled_us||now-v.sampled_us>=250000)return LoRaUiPhase::Stale;
 if(live)return LoRaUiPhase::Live;
 if(!v.metadata_known)return LoRaUiPhase::WaitMeta;
 if(!v.time_valid)return LoRaUiPhase::Withdrawn;
 if(!v.edge_tick)return LoRaUiPhase::WaitPair;
 if(v.age_ms>=120000)return LoRaUiPhase::PairOld;
 if(v.fit_span_ms<180000)return LoRaUiPhase::Training;
 return LoRaUiPhase::FitWait;
}
inline const char* lora_ui_label(LoRaUiPhase phase){
 static const char* const names[]={"RX DISABLED","RX OFF","RX STARTING","RADIO FAULT","STATUS STALE","WAIT META","TX HAS NO TIME","WAIT PAIR","PAIR EXPIRED","TRAINING","FIT NOT READY","LORA UTC LIVE","BLE FAULT: RX OFF"};
 return names[unsigned(phase)];
}
}
