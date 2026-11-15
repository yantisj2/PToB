// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "bootrom.hpp"
#include "update_gate.hpp"
#include <cassert>
int main(){
 watch::Bootrom b;v12::UpdateGate gate;v12::Services active;active.host=true;
 assert(!b.request(1,100,false));assert(gate.begin(100));
 gate.poll(200,active);assert(gate.stage==v12::UpdateStage::Draining);
 assert(!b.request(1,200,gate.stage==v12::UpdateStage::Ready));
 gate.poll(300,{});assert(gate.stage==v12::UpdateStage::Ready);
 assert(!b.request(0,300,true));assert(b.request(1,300,true));
 assert(!b.request(2,400,true));assert(!b.poll(400,true,2,true));
 assert(!b.poll(500,true,1,false));assert(b.poll(600,true,1,true));
 assert(!b.poll(700,true,1,true)); // exactly once
 assert(b.request(2,1000,true));assert(!b.poll(1501000,true,2,true));
 assert(!b.poll(1501001,true,2,true)); // late ACK cannot reboot
 assert(b.request(3,2000000,true));assert(!b.poll(2000001,false,3,true));
 assert(!b.poll(2000002,true,3,true)); // drain loss cancels
 assert(!b.request(4,UINT64_MAX-1,true));
 v12::UpdateGate timeout;assert(timeout.begin(1));timeout.poll(2000001,{});
 assert(timeout.stage==v12::UpdateStage::Fault);assert(!timeout.begin(2000002));
 assert(!b.request(4,2000002,timeout.stage==v12::UpdateStage::Ready));
}
