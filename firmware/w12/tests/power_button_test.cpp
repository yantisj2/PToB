// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "w12_button.hpp"
#include "power_off.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using B=w12::MainButton;
constexpr uint64_t Hz=20000000;
struct Press {
 B b{false};uint8_t page;uint64_t start=Hz;
 explicit Press(uint8_t p):page(p){assert(b.update(true,start,page)==B::None);assert(b.update(true,start+B::DebounceTicks,page)==B::None);}
 auto held(uint64_t elapsed){return b.update(true,start+elapsed,page);}
 auto release(uint64_t elapsed){assert(b.update(false,start+elapsed,page)==B::None);return b.update(false,start+elapsed+B::DebounceTicks,page);}
};
int main(){
 for(auto page:{display_pages::Precision,display_pages::Gps}){
  Press p(page);assert(p.held(B::SleepTicks-1)==B::None);assert(p.held(B::SleepTicks)==B::Sleep);
  assert(p.held(10*Hz)==B::None);assert(p.release(11*Hz)==B::None);
  Press fallback(page);assert(fallback.release(B::SleepTicks)==B::Sleep);
  Press short_hold(page);assert(short_hold.release(B::SleepTicks-1)==B::NextPage);
 }
 Press main(display_pages::Front);assert(main.held(B::ToggleTicks-1)==B::None);assert(main.held(B::ToggleTicks)==B::ToggleRelayOnly);
 assert(main.held(B::SleepTicks)==B::None);assert(main.held(20*Hz)==B::None);assert(main.release(21*Hz)==B::None);
 Press lora(display_pages::LoraRx);assert(lora.held(B::ToggleTicks)==B::ToggleTx);assert(lora.held(B::SleepTicks-1)==B::None);
 assert(lora.held(B::SleepTicks)==B::Sleep);assert(lora.release(5*Hz)==B::None);
 Press lora_short(display_pages::LoraRx);assert(lora_short.release(2*Hz)==B::ToggleTx);
 Press lora_long(display_pages::LoraRx);assert(lora_long.release(5*Hz)==B::Sleep);
 // A physical release just before the threshold must not grow into shutdown.
 Press edge(display_pages::Precision);assert(edge.release(B::SleepTicks-1)==B::NextPage);
 Press tap(display_pages::Front);assert(tap.release(Hz/10)==B::NextPage);
 Press changed(display_pages::Precision);changed.page=display_pages::Front;
 assert(changed.held(B::SleepTicks)==B::None);assert(changed.release(5*Hz)==B::None);
 Press backwards(display_pages::Precision);assert(backwards.b.update(true,0,backwards.page)==B::None);
 assert(backwards.held(B::SleepTicks)==B::None);assert(backwards.release(5*Hz)==B::None);
 B boot(true);assert(boot.update(true,10*Hz,display_pages::Precision)==B::None);
 boot.update(false,11*Hz,display_pages::Precision);assert(boot.update(false,11*Hz+B::DebounceTicks,display_pages::Precision)==B::None);
 boot.update(true,12*Hz,display_pages::Precision);boot.update(true,12*Hz+B::DebounceTicks,display_pages::Precision);
 assert(boot.update(true,16*Hz,display_pages::Precision)==B::Sleep);
 B zero(false);zero.update(true,0,display_pages::Precision);zero.update(true,B::DebounceTicks,display_pages::Precision);
 assert(zero.update(true,B::SleepTicks,display_pages::Precision)==B::Sleep);
 B bounce(false);bounce.update(true,Hz,display_pages::Precision);bounce.update(false,Hz+1,display_pages::Precision);
 assert(bounce.update(false,Hz+B::DebounceTicks+1,display_pages::Precision)==B::None);
 v12::UpdateGate drain;w12::PowerOff off;
 assert(!off.ready(v12::UpdateStage::Ready,false,1));assert(off.request(drain,100));assert(!off.request(drain,200));
 assert(drain.deadline==2000100);v12::Services busy;busy.host=true;drain.poll(1000,busy);
 assert(!off.ready(drain.stage,false,1000));assert(!off.ready(drain.stage,false,50000));
 drain.poll(60000,{});assert(drain.stage==v12::UpdateStage::Ready);
 assert(!off.ready(drain.stage,true,60000));assert(!off.ready(drain.stage,false,70000));
 assert(!off.ready(drain.stage,false,109999));assert(off.ready(drain.stage,false,110000));
 assert(!off.ready(drain.stage,true,110001));assert(!off.ready(drain.stage,false,120000));
 assert(!off.ready(drain.stage,false,100000));assert(!off.ready(drain.stage,false,139999));assert(off.ready(drain.stage,false,140000));
 v12::UpdateGate timed;w12::PowerOff late;assert(late.request(timed,1));timed.poll(2000001,{});
 assert(timed.stage==v12::UpdateStage::Fault&&!late.ready(timed.stage,false,3000000));
 v12::UpdateGate failed;w12::PowerOff fault;assert(fault.request(failed,1));v12::Services bad;bad.fault=true;failed.poll(2,bad);
 assert(!fault.ready(failed.stage,false,100000));
 v12::UpdateGate maintenance;assert(maintenance.begin(1));w12::PowerOff refuse;
 assert(!refuse.request(maintenance,2)&&!refuse.requested());
 std::puts("PASS native button boundaries, main/LoRa holds, boot/release/bounce/page/time fences and original drain/release shutdown gate");
}
