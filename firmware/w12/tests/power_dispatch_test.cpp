// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "w12_button.hpp"
#include "power_off.hpp"
#include <cassert>
#include <cstdio>
enum {GPIO_NUM_0=0};
bool pressed=false,relay=false;unsigned stops=0,captures=0,blanks=0,cycles=0,sleeps=0,faults=0;
uint64_t current_us=1000000,last_ui=0;uint8_t page=display_pages::Front;
w12::MainButton buttons(false);v12::UpdateGate update_gate;w12::PowerOff power_off;
int gpio_get_level(int){return !pressed;}
uint64_t micros(){return current_us;}
void stop_all(){++stops;}
void display_blank(){++blanks;}
void w12_lora_cycle(){++cycles;}
namespace v12 {
void capture_stop(){++captures;}
struct Radio {bool relay_only;};
Radio r2_activity(){return {relay};}
void r2_set_relay_only(bool value){relay=value;}
void fault_watchdog_fatal(){++faults;}
}
namespace w12 {int power_off(){++sleeps;return -99;}}
void dispatch(){const auto button_now=current_us*20;
#include "actual_button_dispatch.inc"
}
void power_poll(){const auto now=current_us;
#include "actual_power_dispatch.inc"
}
void sample(bool down,uint64_t elapsed){pressed=down;current_us=1000000+elapsed;dispatch();}
void reset(uint8_t target){page=target;buttons=w12::MainButton(false);update_gate={};power_off={};pressed=relay=false;stops=captures=blanks=cycles=sleeps=faults=0;}
int main(){
 reset(display_pages::Front);sample(true,0);sample(true,40000);sample(true,1500000);assert(relay);
 sample(true,4000000);sample(false,5000000);sample(false,5040000);
 assert(page==display_pages::Front&&!power_off.requested()&&!stops&&!blanks);
 reset(display_pages::Precision);sample(true,0);sample(true,40000);sample(true,3999999);assert(!stops);
 sample(true,4000000);assert(stops==1&&captures==1&&blanks==1&&power_off.requested());
 assert(update_gate.deadline==7000000);sample(true,4100000);assert(stops==1);
 power_poll();assert(!sleeps);update_gate.poll(current_us,{});power_poll();assert(!sleeps);
 sample(false,4200000);sample(false,4240000);assert(page==display_pages::Precision&&stops==1);
 power_poll();assert(!sleeps);current_us+=40000;power_poll();assert(sleeps==1&&faults==1&&update_gate.stage==v12::UpdateStage::Fault);
 power_poll();assert(sleeps==1); // A returning sleep API cannot be retried as success.
 reset(display_pages::LoraRx);sample(true,0);sample(true,40000);sample(true,1500000);assert(cycles==1);
 sample(true,4000000);assert(cycles==1&&stops==1);sample(false,4100000);sample(false,4140000);assert(page==display_pages::LoraRx);
 reset(display_pages::Precision);assert(update_gate.begin(1));sample(true,0);sample(true,40000);sample(true,4000000);
 assert(!power_off.requested()&&!stops&&!blanks); // Serial maintenance retains its own meaning.
 reset(display_pages::Front);sample(true,0);sample(true,40000);sample(false,100000);sample(false,140000);
 assert(page==display_pages::Precision);
 std::puts("PASS actual native button/shutdown dispatch: one drain/blank, consumed release, main/LoRa actions, maintenance and sleep failure");
}
