// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual watch owner, deterministic SX1262 SPI/BUSY/IRQ boundaries.
static uint8_t sleep_context[16]={1};
static void observed_pair(unsigned i,uint64_t edge){
 watch_lora_shadow_event(3,sleep_context,1,i*2,edge,edge,true);
 watch_lora_shadow_event(4,sleep_context,1,i*2,edge+2500000,edge,true);
}
static void ready_sleep(){
 cold();fake::asleep=fake::wake_fail=fake::sleep_cancel=fake::late_header=false;
 fake::us=377000000;force_on();step(true);step(true);
 assert(!failed&&qualified_map.available);
 shadow={};shadow.tick(1000000);watch_lora_shadow_event(2,sleep_context,1,0,1000000,0,true);
 for(unsigned i=0;i<12;i++)observed_pair(i,8000000+30000000ULL*i);
 watch_lora_shadow_event(2,sleep_context,1,24,361000000,0,true);
 observed_pair(12,368000000);shadow.tick(fake::us);
 assert(shadow.v.tracking&&!shadow.receive_due(fake::us));
}
static void until(uint64_t end){while(fake::us<end)step(true,std::min<uint64_t>(50000,end-fake::us));}
int main(){
 ready_sleep();const auto epoch=capture_epoch;const auto gen=core_generation;step(true);
 assert(radio_sleeping&&fake::asleep&&fake::gpio[48]&&!failed);
 assert(sleep_state.sleeps==1&&owned.enabled&&timer_started&&current_enabled);
 until(395900000);assert(radio_sleeping);
 until(396100000);assert(!radio_sleeping&&!fake::asleep&&!failed&&sleep_state.wakes==1);
 assert(capture_epoch==epoch&&core_generation==gen&&owned.enabled);
 // Entire expected window is listened to; no authenticated pair arrives.
 until(406100000);assert(!shadow.v.tracking&&shadow.v.reason==4&&!radio_sleeping);
 assert(!shadow.v.meta_phase&&shadow.v.qualified==0&&shadow.v.misses==1);
 until(438000000);assert(!radio_sleeping&&sleep_state.sleeps==1);
 observed_pair(14,438000000);assert(!shadow.v.tracking); // one packet cannot restore sleep
 // Complete data ends the window immediately, without waiting for the tail.
 ready_sleep();step(true);until(398000000);observed_pair(13,398000000);
 until(400500000);assert(radio_sleeping&&!failed&&shadow.v.tracking);
 // Physical OFF while asleep retires the radio/capture, never wakes or resets the MCU.
 ready_sleep();step(true);assert(radio_sleeping);assert(v12::lora_request(false,0));worker_step(true);
 assert(owner.drained()&&!radio_sleeping&&!owned.enabled&&!timer_started&&!fake::gpio[47]);
 // Missing wake BUSY completion remains a contained radio fault.
 ready_sleep();step(true);fake::wake_fail=true;shadow.lose(4);step(true);
 assert(failed&&!owned.enabled&&!radio_sleeping&&hardware.error==-ETIMEDOUT);
 // OFF during SetSleep cannot acknowledge a live owner or revive it afterward.
 ready_sleep();fake::sleep_cancel=true;step(true);
 assert(!owner.drained());worker_step(true); // Acknowledge the new OFF generation only.
 assert(!owned.enabled&&!radio_sleeping&&owner.drained()&&!fake::gpio[47]);
 // Header arrival races the standby command: continuous RX and relearning.
 ready_sleep();fake::late_header=true;step(true);
 assert(!radio_sleeping&&!shadow.v.tracking&&shadow.v.reason==9&&sleep_state.refused==1&&!failed);
 // A stalled worker or a lost capture mapping cannot continue scheduled sleep.
 ready_sleep();step(true);step(true,300000);assert(!radio_sleeping&&!shadow.v.tracking&&shadow.v.reason==9);
 ready_sleep();step(true);map_clear();last_map=fake::us*20;step(true);
 assert(!radio_sleeping&&!shadow.v.tracking&&shadow.v.reason==10);
 // Never interrupt a pending payload/FOLLOW-side receive owner.
 ready_sleep();header_pending=true;header_until=fake::us+1000000;step(true);
 assert(!radio_sleeping&&header_pending&&!failed);
 // A spurious physical IRQ while asleep withdraws the forecast as well.
 ready_sleep();step(true);fake::irq=0x10;fake::gpio[14]=1;
 mcpwm_capture_event_data_t event{fake::capture()};on_edge(edge_channel,&event,fake::physical_cookie);step(true);
 assert(!radio_sleeping&&!shadow.v.tracking&&shadow.v.reason==9&&!failed);
 // Bad gain restoration is a fault, not a successful wake.
 ready_sleep();step(true);fake::gain_bad_readback=true;shadow.lose(4);step(true);
 assert(failed&&!owned.enabled&&hardware.error==-EIO);
 puts("PASS actual SX1262 sleep/wake: BUSY-high sleep, NSS wake, gain, wide windows, missed pair continuous recovery, epoch preservation, OFF race, IRQ race, mapping/stall and wake-fault containment");
}
