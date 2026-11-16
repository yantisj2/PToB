// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#define W12_DISPLAY_PREVIEW 1
#include "../legacy/display.cpp"
#include "display_second_deadline.hpp"
#include <array>
#include <cassert>
using P=w12::LoRaUiPhase;
int main(){
 w12_lora_view v{};uint64_t now=1000000;
 auto phase=[&](bool configured=true,bool live=false){return w12::lora_ui_phase(v,configured,now,live);};
 assert(phase(false)==P::Disabled);assert(phase()==P::Off);
 v.requested=true;assert(phase()==P::Starting);v.active=true;v.sampled_us=now;
 assert(phase()==P::WaitMeta);v.metadata_known=true;assert(phase()==P::Withdrawn);
 v.time_valid=true;assert(phase()==P::WaitPair);v.edge_tick=16;v.age_ms=2500;
 assert(phase()==P::Training);v.fit_span_ms=179999;assert(phase()==P::Training);
 v.fit_span_ms=180000;assert(phase()==P::FitWait);assert(phase(true,true)==P::Live);
 v.age_ms=120000;assert(phase()==P::PairOld);v.age_ms=0;
 v.sampled_us=now-250000;assert(phase()==P::Stale);v.sampled_us=now+1;assert(phase()==P::Stale);v.sampled_us=now;
 assert(w12::lora_ui_phase(v,true,now,true,true)==P::Blocked);
 v.mode=2;assert(phase(true,true)==P::Off);v.fault=true;assert(phase(true,true)==P::Fault);
 // Actual framebuffer: independent LoRa page never copies BLE UTC, labels or lamps.
 DisplayState s;s.page=display_pages::LoraRx;s.r2_dashboard=true;s.unit_label="C-4";s.lora_emitter="TC-01";
 s.lora_ui_phase=P::Live;s.lora_utc_us=1794844421000000;s.lora_link_samples=8;s.lora_link_rms_ns=715;s.lora_pair_age_ms=2500;
 std::array<uint8_t,sizeof fb> baseline{};display_show(s);std::memcpy(baseline.data(),fb,sizeof fb);
 s.valid=true;s.utc=123;s.followed_clock="TC-04";s.pps_blink=s.d1_rx=s.d2_rx=true;display_show(s);assert(!std::memcmp(baseline.data(),fb,sizeof fb));
 s.lora_utc_us+=1000000;display_show(s);assert(std::memcmp(baseline.data(),fb,sizeof fb));
 for(unsigned p=0;p<unsigned(P::Live);++p){
  s.lora_ui_phase=P(p);s.lora_utc_us=1794844421000000;s.lora_fit_span_ms=90000;s.lora_link_samples=4;display_show(s);std::memcpy(baseline.data(),fb,sizeof fb);
  s.lora_utc_us+=450000000;display_show(s);assert(!std::memcmp(baseline.data(),fb,sizeof fb));
 }
 // Live LoRa repaint phase follows its own UTC; page switches cannot reset phase.
 w12::DisplaySecondDeadline d;d.painted(now,true,1794844421250000);assert(d.deadline()==now+750000);
 d.painted(now+500000,true,1794844421750000);assert(d.deadline()==now+750000);
 d.painted(now+750000,false,0);assert(d.deadline()==now+1750000);
}
