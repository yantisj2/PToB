// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "ble_power.hpp"
#include "radio_watchdog.hpp"
namespace watch {
inline bool power_fault_active(const BlePower& p){return p.operating_fault||p.cleanup_error||p.stage==BlePower::Fault;}
inline void power_fault_record(RadioFaultRecord& f,const BlePower& p){
 // Watch-local reason 6 distinguishes lifecycle failures from G1 admission.
 f.reason=6;f.api=0;f.rc=p.cleanup_error?p.cleanup_error:p.last_error;
 f.stage=p.cleanup_error?p.cleanup_failed_stage:p.last_failed_stage;f.site=p.last_fault_site;
 f.error_at_us=p.cleanup_error?p.cleanup_error_at_us:p.last_error_at_us;
 f.episode=p.cleanup_error?p.cleanup_generation:p.last_fault_generation;f.generation=p.generation;
 f.boot_first_api=p.fault_site;f.boot_first_rc=p.error;f.boot_first_at=p.error_at_us;
 f.last_api=p.sync_site;f.last_rc=p.sync_error;f.last_error_at_us=p.last_error_at_us;
 f.stop_us=p.cleanup_started_us;f.deadline_us=p.cleanup_deadline_us;f.completed_us=p.cleanup_completed_us;
 f.owner_flags=unsigned(p.requested)|(unsigned(p.host_running)<<1)|(unsigned(p.worker_running)<<2)|
   (unsigned(p.off_ack())<<3)|(unsigned(p.cleanup_active)<<4)|(unsigned(p.operating_fault)<<5)|(unsigned(p.controller)<<8);
}
}
