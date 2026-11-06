// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "ui.hpp"
namespace n30 {
// Boot-session presentation preference. Only newly authenticated metadata enters
// here; retaining a label never renews time, admission, source or fit authority.
class DisplayTimeZone {
 r2_tz value_{};uint64_t lora_at_=0,peer_=0,boot_=0;
 uint16_t ble_serial_=0;bool ble_seen_=false;
 r2_tz ble_value_{};
 public:
 void update(Model& m,uint16_t serial,uint64_t peer,uint64_t boot,
             const n30_lora_view& l,uint64_t now){
  if(now>=l.sampled_us&&now-l.sampled_us<SnapshotMaxUs&&l.metadata_known&&
     l.metadata_us&&l.metadata_us<=now&&l.metadata_us!=lora_at_){
   lora_at_=l.metadata_us;
   const r2_tz tz{l.tz_id,l.tz_offset_min};
   if(r2_tz_valid(tz))value_=tz;
  }
  if(now<m.tz_until_us&&r2_tz_valid(m.tz)&&
     (!ble_seen_||serial!=ble_serial_||peer!=peer_||boot!=boot_||
      m.tz.id!=ble_value_.id||m.tz.offset_min!=ble_value_.offset_min)){
   value_=ble_value_=m.tz;ble_serial_=serial;peer_=peer;boot_=boot;ble_seen_=true;
  }
  m.tz=value_;m.tz_until_us=r2_tz_valid(value_)?UINT64_MAX:0;
 }
};
}
