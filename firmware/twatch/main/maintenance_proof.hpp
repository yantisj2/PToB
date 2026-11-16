// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "update_gate.hpp"
namespace watch {
// Retirement receipts, not historical operating-error counters, establish
// update safety. No missing proof can be replaced by a quiet-looking snapshot.
struct MaintenanceProof {
 bool ble_off=false,lora_off=false,journal_idle=false,fine_off=false,display_off=false;
 bool ble_cleanup_fault=false,lora_cleanup_fault=false,fine_cleanup_fault=false;
 bool consumers_closed()const{return ble_off&&lora_off&&journal_idle;}
 v12::Services services(bool include_display=true)const{
  v12::Services s;
  s.host=!ble_off||!journal_idle||!fine_off||(include_display&&!display_off);
  s.lora=!lora_off;
  s.fault=ble_cleanup_fault||lora_cleanup_fault||fine_cleanup_fault;
  return s;
 }
};
}
