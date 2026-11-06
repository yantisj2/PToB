// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Couple the actual native publication/read-through adapter, main eligibility,
// UI status adapter and source selector. Synthetic model inputs are not RF or
// precision acceptance; the source deadlines below remain immutable per grant.
#include "sync_watchdog.hpp"
#include "status_adapter.hpp"
void dual_coupled_case(){
 constexpr uint64_t Start=100000000,Period=6000000,Gap=150000;
 fake_now=Start;
 v12::anchor={true,501,601,UINT64_C(1700000000000000)+fake_now,fake_now,fake_now+1500000,2,3,1000};
 v12::joined_peer=v12::sync_identity=501;v12::joined_boot=601;v12::member=true;v12::synced=true;v12::sync_handle=9;
 v12::Rc7PermissionInspect::seed(v12::corrected_clock,v12::anchor,fake_now);
 n30::ClockSelection selector;n30::Model initial;
 n30_lora_view l{};l.requested=l.active=true;l.state=2;l.sampled_us=fake_now;l.utc_us=1700000000000000+fake_now;l.until_us=Start+120000000;
 selector.select(initial,l,fake_now);
 unsigned good=0,gaps=0,returned=0;uint32_t masks=0;
 for(uint64_t elapsed=1000;elapsed<=70000000;elapsed+=1000){
  fake_now=Start+elapsed;const auto phase=elapsed%Period;
  if(phase<Period-Gap&&elapsed%62500<1000){
   const auto until=std::min(fake_now+1500000,Start+elapsed-phase+Period-Gap);
   v12::anchor.until=until;v12::Rc7PermissionInspect::stream(v12::corrected_clock,v12::anchor,fake_now,until);
  }
  if(elapsed%10000==0)v12::publish(fake_now);
  const auto s=v12::r2_status();const auto refusal=n30::ble_lock_refusal(s,fake_now);masks|=refusal;
  selector.observe(!refusal,s.root_identity,s.root_boot,s.source_epoch,s.g1_clock_origin,s.publication,s.g1_relative_quality.scope_generation,fake_now,refusal);
  DisplayState d{};d.valid=true;d.r2_relative_lock=!refusal;d.rms_ns=100;
  auto m=n30::model(s,d,fake_now);l.sampled_us=fake_now;l.utc_us=1700000000000000+fake_now;
  selector.select(m,l,fake_now,!refusal);
  if(refusal)++gaps;else ++good;
  if(m.source==n30::Source::Ble){assert(!refusal&&s.following&&fake_now<s.service_until_us&&fake_now<s.g1_corrected_until_us);++returned;}
  if(elapsed>40000000&&!refusal&&m.source!=n30::Source::Ble){
   std::printf("COUPLED_COUNTEREXAMPLE at=%llu refusal=%u masks=%u expected=BLE actual=LOR good=%u gaps=%u\n",(unsigned long long)fake_now,refusal,masks,good,gaps);assert(false);
  }
 }
 assert(good>60000&&gaps>100&&returned>10000);
 std::printf("PASS actual native/read-through/eligibility/UI/source coupling good=%u denied=%u BLE=%u masks=%u; original grants enforced\n",good,gaps,returned,masks);
}
