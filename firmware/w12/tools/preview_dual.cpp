// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Exact firmware renderer, synthetic states; no device observations.
#define W12_DISPLAY_PREVIEW 1
#define PTOB_W12_DUAL_UI 1
#include "../legacy/display.cpp"
#include <fstream>
#include <string>
void save(const DisplayState& s,const std::string& dir,const char* name){display_show(s);std::ofstream o(dir+"/"+name+".pbm");o<<"P1\n128 64\n";for(int y=0;y<64;y++){for(int x=0;x<128;x++)o<<bool(fb[y/8*128+x]&(1u<<(y%8)))<<' ';o<<'\n';}}
int main(int argc,char** argv){
 if(argc!=2)return 2;std::string d=argv[1];DisplayState s;s.unit_label="C-4";s.r2_dashboard=true;s.followed_clock="TC-04";
 s.valid=true;s.utc=1794844421;s.role="LOCK_EST";s.pps_blink=s.d1_rx=s.d2_rx=true;
 s.page=display_pages::LoraRx;s.lora_emitter="TC-01";s.lora_rx_done=27;
 for(unsigned p=0;p<=unsigned(w12::LoRaUiPhase::Live);++p){s.lora_ui_phase=w12::LoRaUiPhase(p);
  s.lora_utc_us=1794844421000000;s.lora_fit_span_ms=89999;s.lora_link_samples=4;s.lora_pair_age_ms=2500;s.lora_meta_age_ms=34000;
  s.lora_link_rms_ns=715;if(s.lora_ui_phase==w12::LoRaUiPhase::Live)s.lora_link_samples=8;
  std::string n="lora-"+std::to_string(p);save(s,d,n.c_str());}
 s.lora_ui_phase=w12::LoRaUiPhase::Training;s.lora_fit_span_ms=179999;s.lora_link_samples=7;s.lora_crc=s.lora_rx_done=UINT32_MAX;save(s,d,"lora-max");
 s.followed_clock="TC-01";s.page=display_pages::Precision;s.precision_lora=s.precision_live=s.lora_time_valid=true;s.timing_source=DisplayState::Lora;s.rms_ns=715;s.lora_link_samples=8;save(s,d,"precision-lora");
 s.precision_live=s.lora_time_valid=false;s.rms_ns=UINT32_MAX;save(s,d,"precision-expired");
 s.followed_clock="TC-04";s.precision_lora=false;s.precision_live=true;s.timing_source=DisplayState::Ble;s.rms_ns=49;s.precision_fine=true;save(s,d,"precision-ble");
 s.page=display_pages::Front;save(s,d,"clock-ble");
}
