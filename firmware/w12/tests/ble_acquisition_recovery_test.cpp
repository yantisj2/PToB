// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "w12_ble_acquisition_recovery.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
 using w12::BleAcquisitionRecovery;constexpr uint64_t start=1000000,T=BleAcquisitionRecovery::StallUs;
 BleAcquisitionRecovery p;
 assert(!p.observe(true,true,0,start));
 assert(!p.observe(true,true,0,start+T-1));assert(p.observe(true,true,0,start+T));
 p.queued();assert(p.rebinds()==1&&!p.observe(true,true,0,start+T));
 assert(!p.observe(true,true,1,start+2*T)); // Physical progress wins at boundary.
 assert(!p.observe(true,true,1,start+3*T-1));assert(p.observe(true,true,1,start+3*T));
 for(bool nearby:{false,true}){
  p.observe(false,nearby,1,start+4*T);assert(!p.observe(true,nearby,1,start+10*T));
 }
 assert(!p.observe(true,true,2,start+11*T));assert(!p.observe(true,true,2,start));
 assert(!p.observe(true,true,2,start+T-1));assert(p.observe(true,true,2,start+T));
 // Ordinary physical progress, even without precision lock, never recycles
 // acquisition. Discovery/JOIN presence alone never counts as progress.
 BleAcquisitionRecovery steady;
 for(uint32_t i=0;i<10000;++i)assert(!steady.observe(true,true,i,start+uint64_t(i)*62500));
 assert(!steady.rebinds());
 std::puts("PASS BLE stall exact boundary, physical progress, startup, OFF/absence, backward clock and no quality-triggered resets");
}
