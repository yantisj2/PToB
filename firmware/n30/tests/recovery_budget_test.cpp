// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "g1_parent_selection.hpp"
#include "lora_policy.hpp"
#include <cassert>
#include <cstdio>
#include <string>
int main(int argc,char** argv){
 assert(argc==2);const std::string which=argv[1];constexpr uint64_t start=1000000,step=62500;
 v12::G1RecoveryBudget zero;zero.observe(true,0,60000000);zero.observe(true,60000000,60000000);assert(zero.stable(60000000,60000000));
 v12::G1RecoveryBudget b;b.observe(false,start,60000000);
 b.observe(false,start+59999999,60000000);assert(b.lost()==59999999);
 b.observe(false,start+60000000,60000000);assert(b.lost()==60000000);
 b.observe(true,start+60000001,60000000);b.observe(true,start+120000000,60000000);assert(b.lost()!=0);
 b.observe(true,start+120000001,60000000);assert(b.lost()==0&&b.stable(start+120000001,60000000));
 b.observe(false,start+120000002,60000000);assert(b.lost());b.observe(false,1,60000000);assert(!b.lost());
 b.observe(false,UINT64_MAX,60000000);b.observe(false,UINT64_MAX,60000000);assert(b.lost()==UINT64_MAX-1);

 if(which=="parent"){
  v12::G1ParentSelection p{60000000};p.seen(11,false,-95,start);p.bind(11,111,1,start);
  uint64_t relay_at=0;
  for(uint64_t dt=0;dt<=600000000;dt+=step){
   const auto now=start+dt;const bool brief_lock=dt%20000000<4000000;
   p.locked(brief_lock,now);p.seen(11,false,-95,now);p.seen(22,true,-40,now,true);
   if(p.needs_alternative()&&p.choose(now)==22){relay_at=dt;break;}
  }
  std::printf("INTERMITTENT_GM relay_attempt_opportunity_us=%llu\n",(unsigned long long)relay_at);std::fflush(stdout);
  assert(relay_at&&relay_at<=90000000);
 }else{
  n30::LoRaPolicy p;uint64_t on_at=0;
  for(uint64_t dt=0;dt<=600000000;dt+=step){
   p.update(dt%20000000<4000000,start+dt);
   if(p.requested()){on_at=dt;break;}
  }
  std::printf("INTERMITTENT_GM lora_auto_on_us=%llu\n",(unsigned long long)on_at);std::fflush(stdout);
  assert(on_at&&on_at<=240000000);
 }
}
