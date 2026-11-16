// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace watchimu {
struct Status {
 bool ready=false,armed=false,irq=false,orientation_ok=false,return_active=false,pose_prepared=false;
 uint8_t stage=0,available=0,interrupt_ctrl=0,last_meta=0,meta_sensor=0,meta_value=0;
 int32_t bus_rc=0;uint16_t bus_len=0;uint8_t bus_reg=0;bool bus_write=false;uint64_t bus_at_us=0;uint32_t bus_work_us=0,bus_max_us=0;
 int8_t error=0;uint16_t kernel=0;int16_t x=0,y=0,z=0;
 uint32_t bus_retries=0,bus_recovered=0,pose_rejected=0;
 uint32_t events=0,errors=0,samples=0,lowered=0,bus_errors=0,budget_errors=0;
 uint32_t polls=0,services=0,irq_high=0,stale_polls=0,fifo_overflows=0,sensor_errors=0,resets=0;
 uint32_t arms=0,disarms=0,config_reads=0,rate_millihz=0,accel_millihz=0;
 uint32_t ignored_awake=0,ignored_disarmed=0,ignored_refractory=0,ignored_stale=0,tilt_events=0,wake_events=0;
 uint32_t max_poll_gap_us=0,max_service_us=0,sample_age_ms=UINT32_MAX;
 uint64_t sampled_us=0,sensor_us=0,last_event_us=0,last_sample_us=0;
};
enum class Motion {None,Raise,Lower};
bool init();Motion poll(uint64_t now,bool asleep,bool glancing);Status status();
}
