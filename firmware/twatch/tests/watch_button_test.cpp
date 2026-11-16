// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "watch_button.hpp"
#include "power_off.hpp"
#include <cassert>
using watch::ButtonAction;
int main(){
 watch::WatchButton b;
 auto sample=[&](bool pressed,uint64_t t,ButtonAction expected=ButtonAction::None){assert(b.update(pressed,t)==expected);};
 sample(false,0);sample(true,100000);sample(false,110000);sample(true,120000);
 sample(true,160000);sample(false,200000);sample(false,240000);
 sample(false,690000);sample(false,690001,ButtonAction::Single);sample(false,800000);
 // Two fully debounced short releases; no intermediate launcher action.
 sample(true,900000);sample(true,940000);sample(false,1000000);sample(false,1040000);
 sample(true,1150000);sample(true,1190000);sample(false,1240000);sample(false,1280000,ButtonAction::PowerOff);
 sample(false,2000000);
 // Startup-held BOOT and timer rollback cannot trigger a click or shutdown.
 b={};sample(true,0);sample(false,100000);sample(false,140000);sample(false,1000000);
 sample(true,1100000);sample(true,1140000);sample(false,1200000);sample(false,1240000);
 sample(false,0);sample(false,1000000);
 // Long press cannot be interpreted as the second short click.
 b={};sample(false,0);sample(true,100000);sample(true,140000);sample(false,180000);sample(false,220000);
 sample(true,300000);sample(true,340000);sample(false,1100000);sample(false,1140000,ButtonAction::Single);
 // Existing drain is mandatory; active owners, faults and held button prevent off.
 v12::UpdateGate gate;w12::PowerOff off;assert(off.request(gate,100));
 assert(!off.ready(gate.stage,false,1000));v12::Services busy;busy.host=true;gate.poll(200,busy);
 assert(!off.ready(gate.stage,false,300));gate.poll(400,{});
 assert(!off.ready(gate.stage,true,500));assert(!off.ready(gate.stage,false,600));
 assert(!off.ready(gate.stage,false,40599));assert(off.ready(gate.stage,false,40600));
 v12::UpdateGate failed;w12::PowerOff blocked;assert(blocked.request(failed,0));failed.poll(2000000,{});
 assert(!blocked.ready(failed.stage,false,3000000));
 // Two ordinary 70ms presses fall entirely between 200ms render samples.
 // Independent 5ms sampling detects exactly one double-click, even while
 // the UI consumer is blocked throughout the complete gesture.
 auto replay=[](uint64_t cadence){watch::WatchButton key;unsigned doubles=0,singles=0;
  for(uint64_t t=0;t<=1000000;t+=cadence){const bool raw=(t>=100000&&t<170000)||(t>=250000&&t<320000);
   auto result=key.update(raw,t);doubles+=result==ButtonAction::PowerOff;singles+=result==ButtonAction::Single;}
  assert(singles==0);return doubles;};
 assert(replay(200000)==0);assert(replay(5000)==1);
}
