// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ble_acquisition_recovery.hpp"
#include <cassert>
#include <cstdio>
int main(){
 using Policy=n30::BleAcquisitionRecovery;
 constexpr uint64_t start=1000000,T=Policy::StallUs;
 Policy loss;
 assert(!loss.observe(true,true,10,start));
 // A departed GM's locator expires before the recovery deadline. Absence
 // must not keep resetting that deadline (the installed policy's failure).
 assert(!loss.observe(true,false,10,start+10000000));
 assert(!loss.observe(true,false,10,start+T-1));
 assert(loss.observe(true,false,10,start+T));
 loss.queued();assert(loss.rebinds()==1);
 assert(!loss.observe(true,false,10,start+T+1));
 assert(!loss.observe(true,false,10,start+T+59999999));
 assert(loss.observe(true,false,10,start+T+60000000));
 Policy cold;
 for(unsigned i=0;i<180;++i)assert(!cold.observe(true,false,0,start+i*1000000ULL));
 Policy live;
 for(unsigned i=1;i<1000;++i)assert(!live.observe(true,false,i,start+i*62500ULL));
 assert(!live.rebinds());
 assert(!live.observe(false,true,999,start+100000000));
 assert(!live.observe(false,false,999,start+200000000));
 assert(!live.observe(true,false,999,start+200000001));
 assert(!live.observe(true,false,1000,start+215000001));
 assert(!live.observe(true,false,1000,1)); // Monotonic discontinuity re-arms.
 assert(!live.observe(true,false,1000,T));
 assert(live.observe(true,false,1000,T+1));
 std::puts("PASS watch source loss without discovery, bounded retries, cold absence, physical progress, OFF and monotonic restart");
}
