// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Exact firmware renderer, synthetic states; no device observations.
#define W12_DISPLAY_PREVIEW 1
#define PTOB_W12_DUAL_UI 1
#include "../legacy/display.cpp"
#include <fstream>
#include <string>
void save(const DisplayState& s,const std::string& dir,const char* name){display_show(s);std::ofstream o(dir+"/"+name+".pbm");o<<"P1\n128 64\n";for(int y=0;y<64;y++){for(int x=0;x<128;x++)o<<bool(fb[y/8*128+x]&(1u<<(y%8)))<<' ';o<<'\n';}}
int main(int argc,char** argv){if(argc!=2)return 2;std::string d=argv[1];DisplayState s;s.unit_label="C-4";s.r2_dashboard=true;s.followed_clock="TC-02";s.r2_root_clock="TC-02";s.r2_connection="GM";s.valid=true;s.utc=1792761600;s.role="LOCK_EST";s.pps_blink=s.d1_rx=s.d2_rx=true;s.r2_relative_lock=true;s.r2_lock_seconds=3661;s.precision_fine=s.precision_live=true;s.rms_ns=49;s.samples=64;s.precision_rate_ppb=-122;s.page=display_pages::Precision;save(s,d,"precision-locked");s.precision_live=s.r2_relative_lock=false;s.r2_lock_seconds=0;save(s,d,"precision-stale");s.precision_live=true;s.rms_ns=4294967294;s.precision_rate_ppb=-50000;save(s,d,"precision-large");s.rms_ns=49;s.page=display_pages::LoraRx;s.lora_configured=true;s.lora_transition_ms=180000;save(s,d,"lora-auto-off");s.lora_active=true;s.lora_time_valid=true;s.lora_accepted=42;s.lora_crc=2;s.lora_mode=1;save(s,d,"lora-on");s.lora_active=s.lora_time_valid=false;s.lora_mode=2;save(s,d,"lora-off");s.lora_fault=true;save(s,d,"lora-fault");s.lora_fault=s.lora_configured=false;save(s,d,"lora-unbound");s.page=display_pages::Front;save(s,d,"clock");}
