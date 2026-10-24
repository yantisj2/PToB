// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "g1_clock_prediction.hpp"
#ifdef PTOB_W12_SYNC_WATCHDOG
#include "w12_phase_offsets.hpp"
#endif
namespace v12 {
// RC7 corrections use the ADDRESS endpoint. The pinned publisher schedules a
// BLE1M packet at half-slot fine=0, before its 8-bit preamble and 32-bit address.
// This is a NOMINAL endpoint conversion, not a measured RF/controller delay.
struct Rc7TxReference {
  static constexpr uint8_t Ble1MPhy=1; // Public HCI PHY enumeration.
  static constexpr uint64_t AddressDelta20=800; // (8+32)us at20MHz.
  struct Projection {
    uint64_t address20=0;int64_t raw_ns=0,address_ns=0,emitted_ns=0;
    int32_t base_ns=0,residual_ns=0,total_ns=0;
  };
  static bool project(const G1ClockPrediction& map,uint64_t programmed20,
                      uint64_t available20,uint8_t phy,bool legacy_profile,
                      int32_t legacy_total_ns,Projection& out){
    out={};
    // Do not extend this1M conversion to another PHY by assumption.
    if(phy!=Ble1MPhy||!programmed20||programmed20>UINT64_MAX-AddressDelta20||
       programmed20+AddressDelta20>available20)return false;
    const auto raw=map.relative_at(programmed20);
    const auto address20=programmed20+AddressDelta20;
    const auto address=map.relative_at(address20);
    if(raw<=0||address<=raw||address-raw>100000||
       (legacy_profile&&(!legacy_total_ns||legacy_total_ns< -100000||legacy_total_ns>100000)))return false;
    const auto base=int32_t(address-raw);
    // Schema1 profiles describe TOTAL correction from the old programmed
    // endpoint. Preserve that meaning and exact output; never add40us twice.
    const auto total=legacy_profile?legacy_total_ns:base
#ifdef PTOB_W12_SYNC_WATCHDOG
      +W12PhaseOffsets::RelayNs
#endif
      ;
    const auto residual=total-base;
    if((total>0&&raw>INT64_MAX-total)||(total<0&&raw<=-int64_t(total)))return false;
    out={address20,raw,address,raw+total,base,residual,total};return true;
  }
};
}
