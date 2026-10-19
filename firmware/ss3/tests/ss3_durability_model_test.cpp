// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "fault_watchdog_liveness.hpp"
#include "fault_watchdog_model.hpp"
#include "td_survey_lifecycle.hpp"
#include <cassert>
#include <cstdio>
int main(){
 using namespace v12;
 TdSurveyLifecycle phase;assert(!phase.begin_close(0,false));assert(phase.begin_close(10,true));
 assert(!phase.begin_close(100,true)&&phase.closing_us==10);assert(!phase.expired(2000009)&&phase.expired(2000010));
 phase.advance();assert(!phase.finishing&&phase.generation==1&&!phase.expired(UINT64_MAX));
 FaultWatchdogModel model;
 const uint64_t start=UINT64_C(60)*86400000000; // Beyond32-bit milliseconds.
 assert(!fault_watchdog_stalled(start,start,start,true,0,false));
 assert(!fault_watchdog_stalled(start,start,start,true,start,true));
 auto mask=fault_watchdog_stalled(start+2000000,start+2000000,start+2000000,true,start,true);
 assert(mask==FaultDisplayStalled);model.observe(start+2000000,mask);
 assert(!model.take_restart(start+121999999)&&model.take_restart(start+122000000));
 FaultWatchdogModel recovery;recovery.observe(start,FaultDisplayStalled);recovery.observe(start+1000000,0);
 assert(!recovery.active()&&!recovery.take_restart(start+120000000));
 assert(fault_watchdog_stalled(UINT64_MAX,UINT64_MAX,UINT64_MAX,true,UINT64_MAX-2000000,true)==FaultDisplayStalled);
 assert(fault_watchdog_stalled(1,1,1,true,UINT64_MAX,true)==0); // Regression cannot prove age.
 puts("PASS immutable survey drain deadline and independent LCD supervision through long uptimes and recovery");
}
