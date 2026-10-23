#!/usr/bin/env python3
"""Compile actual N30 command dispatch against explicit hardware-owner fakes."""
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import argparse,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
src=(APP/'main/native_main.cpp').read_text();command=src[src.index('void command(const char* text){'):src.index('void input(){')]
poll=src[src.index('[[gnu::noinline]] void poll_display_action(){'):src.index('[[gnu::noinline]] void clock_fault_progress(){')]
prefix=r'''
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include "console_protocol.hpp"
#include "update_gate.hpp"
#include "lora_policy.hpp"
#include "timing_command.hpp"
#include "window_command.hpp"
#define V12_R2_ENABLED 1
#define V12_R2_CONFIG 13
uint64_t now=1000;unsigned radio_changes=0,pages=0,statuses=0,lora_changes=0,diag_frames=0;bool lora_diagnostic_response=false,lora_action=false,rc9_diagnostic_response=false;unsigned selected=0;
bool owner_active=true;
bool window_diagnostic_response=false,window_edge_response=false,window_late_response=false;uint32_t window_after=0;unsigned window_starts=0,window_acks=0;
bool n30_window_start(uint64_t,uint64_t,uint64_t,uint32_t,uint32_t){++window_starts;return true;}
void n30_window_stop(){}
bool n30_window_ack(uint32_t){++window_acks;return true;}
bool fine_diagnostic_response=false;uint32_t fine_after=0;uint64_t fine_factory=0x020000003001ULL;unsigned fine_starts=0,fine_stops=0,fine_probes=0;
bool n30_fine_start(uint64_t,uint64_t,uint64_t,uint32_t ms,uint32_t every){++fine_starts;return ms>=1000&&ms<=600000&&every>=1&&every<=64;}
bool n30_fine_probe(uint64_t,uint64_t,uint64_t,uint32_t spacing,uint32_t edges){++fine_probes;return spacing>=50&&spacing<=1000&&edges>=4&&edges<=24&&edges%4==0;}
void n30_fine_stop(){++fine_stops;}
bool fine_backend=false;bool n30_fine_backend_selected(){return fine_backend;}
bool timing_diagnostic_response=false;uint32_t timing_after=0;unsigned timing_starts=0,timing_stops=0;
bool n30_timing_start(uint64_t,uint64_t,uint32_t ms,uint32_t every){++timing_starts;return ms>=1000&&ms<=600000&&every>=1&&every<=64;}
void n30_timing_stop(){++timing_stops;}
bool mode=false,action_pending=false;
namespace v12 {
unsigned tx_starts=0;
void capture_stop(){}
bool n30_capture_tx_begin(uint64_t,uint32_t ms){++tx_starts;return ms>=1000&&ms<=10000;}
bool capture_begin(uint64_t,uint32_t){return true;}
bool capture_rolling_begin(uint64_t,uint32_t){return true;}
struct LoRa {bool enabled=false,requested=false,worker_busy=false,off_acknowledged=true,failed=false;};
LoRa lora_status(){return {};}
bool r2_client_start(uint64_t){++radio_changes;return true;}
bool r2_set_relay_only(bool value){mode=value;++radio_changes;return true;}
struct Activity {bool relay_only=false;};
Activity r2_activity(){return {mode};}
bool r2_rc9_parent(uint64_t){++radio_changes;return true;}
bool r2_rc_parent(uint64_t){++radio_changes;return true;}
bool r2_gm_parent(uint64_t){++radio_changes;return true;}
bool r2_client_start_profile(uint64_t,unsigned){++radio_changes;return true;}
enum class R2Mode {Passive,Active,Off};
bool r2_request(R2Mode,uint32_t,uint64_t){++radio_changes;return true;}
}
v12::ConsoleTicket ticket;v12::UpdateGate update_gate;
struct Result {uint64_t nonce=0,admitted_us=0;bool accepted=false;const char* action="",*reason="";} last_command;
char factory_text[]="02:00:00:00:30:01";bool identity=true;uint64_t session=3,last_nonce=0;uint32_t commands_rejected=0;
uint64_t micros(){return now;}
void stop_all(){++radio_changes;}
void respond_status(uint64_t,uint64_t){++statuses;if(lora_diagnostic_response)diag_frames++;}
bool n30_lora_mode(n30::LoRaMode){lora_changes++;return true;}
bool n30_lora_cycle(){lora_changes++;return true;}
bool n30_display_take_lora_toggle(uint64_t){bool take=lora_action;lora_action=false;return take;}
bool r2_command_off_ack(){return !owner_active;}
bool n30_display_page(unsigned n){if(n>3)return false;selected=n;++pages;return true;}
bool n30_display_take_upstream_toggle(uint64_t){bool take=action_pending;action_pending=false;return take;}
'''
suffix=r'''
void send(uint64_t nonce,const char* action,const char* mac=factory_text,uint64_t boot=3,bool expire=false){
 const auto t=ticket.issue(now);if(expire)now+=2000000;
 char text[200];std::snprintf(text,sizeof text,"v12 do %016" PRIx64 " %016" PRIx64 " %s %016" PRIx64 " %s",t,nonce,mac,boot,action);command(text);
}
int main(){
 // An active BLE owner must not block independent presentation, and page
 // requests must not stop/restart or reconfigure that owner.
 send(1,"ui page 1");assert(last_command.accepted&&selected==1&&pages==1&&radio_changes==0&&owner_active);
 send(2,"ui page 0");assert(last_command.accepted&&selected==0&&pages==2&&radio_changes==0);
 send(2,"ui page 1");assert(!last_command.accepted&&pages==2); // nonce replay
 send(3,"ui page 4");assert(!last_command.accepted&&pages==2);
 send(4,"ui page 1","02:00:00:00:30:02");assert(!last_command.accepted&&pages==2);
 send(5,"ui page 1",factory_text,4);assert(!last_command.accepted&&pages==2);
 send(6,"ui page 1",factory_text,3,true);assert(!last_command.accepted&&pages==2);
 send(7,"rx full 1000");assert(!last_command.accepted&&std::string(last_command.reason)=="R2_OWNS_BLE"&&radio_changes==0);
 poll_display_action();assert(!radio_changes);
 action_pending=true;poll_display_action();assert(mode&&radio_changes==1&&!action_pending);
 poll_display_action();assert(radio_changes==1);
 action_pending=true;poll_display_action();assert(!mode&&radio_changes==2);
 send(101,"capture tx 5000");assert(last_command.accepted&&v12::tx_starts==1&&std::string(last_command.action)=="CAPTURE_TX");
 for(const char* bad:{"capture tx 999","capture tx 10001","capture tx 4294968296","capture tx 5000 extra","capture tx -1"}){send(++now,bad);assert(!last_command.accepted&&v12::tx_starts==1);}
 send(20,"ui page 2");assert(last_command.accepted&&selected==2&&pages==3&&radio_changes==2);
 send(21,"ui page 0");assert(last_command.accepted&&pages==4&&radio_changes==2);
 send(22,"lora on");assert(last_command.accepted&&lora_changes==1&&radio_changes==2);
 send(23,"lora off");assert(last_command.accepted&&lora_changes==2);
 send(24,"lora auto");assert(last_command.accepted&&lora_changes==3);
 send(25,"lora diag");assert(last_command.accepted&&diag_frames==1&&!lora_diagnostic_response);
 send(30,"timing start 600000 4");assert(last_command.accepted&&timing_starts==1&&!timing_diagnostic_response&&radio_changes==2);
 send(31,"timing read 7");assert(last_command.accepted&&timing_after==7&&timing_starts==1&&radio_changes==2);
 send(32,"timing stop");assert(last_command.accepted&&timing_stops==1&&radio_changes==2);
 send(33,"timing start 600001 1");assert(!last_command.accepted);
 send(34,"timing start 2000 0");assert(!last_command.accepted);
 send(35,"timing start 2000 4 extra");assert(!last_command.accepted);
 send(36,"timing start 2000 4",factory_text,4);assert(!last_command.accepted&&timing_starts==1);
 send(38,"timing start 4294968296 1");assert(!last_command.accepted&&timing_starts==1);
 send(39,"timing read 4294967296");assert(!last_command.accepted);
 send(40,"timing read -1");assert(!last_command.accepted);
 lora_action=true;poll_display_action();assert(lora_changes==4&&!lora_action&&radio_changes==2);
 send(50,"fine start 2000 4");assert(last_command.accepted&&fine_starts==1&&!fine_diagnostic_response&&radio_changes==2);
 send(51,"fine read 3");assert(last_command.accepted&&fine_after==3);
 send(52,"fine stop");assert(last_command.accepted&&fine_stops==1);
 send(53,"fine start 600001 1");assert(!last_command.accepted&&fine_starts==1);
 const std::string longzero="fine start "+std::string(100,'0')+"2000 4";send(56,longzero.c_str());assert(!last_command.accepted&&fine_starts==1);
 send(57,"fine probe 100 16");assert(last_command.accepted&&fine_probes==1);
 send(58,"fine probe 100 15");assert(!last_command.accepted&&fine_probes==1);
 send(59,"fine probe 49 16");assert(!last_command.accepted&&fine_probes==1);
 send(54,"fine start 2000 4",factory_text,4);assert(!last_command.accepted&&fine_starts==1);
 send(212,"ui page 3");assert(last_command.accepted&&selected==3&&pages==5&&radio_changes==2);
 send(213,"ui page 0");assert(last_command.accepted&&pages==6&&radio_changes==2);
 send(214,"window start 600000 128");assert(last_command.accepted&&window_starts==1&&radio_changes==2);
 send(215,"window packet");assert(last_command.accepted&&!window_diagnostic_response);
 send(216,"window edges 4294967295");assert(last_command.accepted&&window_after==UINT32_MAX);
 send(219,"window late 128");assert(last_command.accepted&&window_late_response&&window_after==128);
 send(217,"window ack 1");assert(last_command.accepted&&window_acks==1);
 for(const char* bad:{"window start 600001 8","window start 1000 129","window ack 0","window edges -1","window start 1000 8 extra"}){send(++now,bad);assert(!last_command.accepted&&window_starts==1);}
 fine_backend=true;send(++now,"fine stop");assert(!last_command.accepted&&fine_stops==1);send(++now,"window stop");assert(!last_command.accepted);fine_backend=false;
 update_gate.begin(now);send(218,"window start 1000 8");assert(!last_command.accepted&&window_starts==1);send(8,"ui page 1");assert(!last_command.accepted&&pages==6&&radio_changes==2);
 send(60,"fine probe 100 16");assert(!last_command.accepted&&fine_probes==1);
 send(55,"fine start 2000 4");assert(!last_command.accepted&&fine_starts==1);
 send(37,"timing start 2000 4");assert(!last_command.accepted&&timing_starts==1);
 action_pending=true;lora_action=true;poll_display_action();assert(!mode&&radio_changes==2&&!action_pending&&!lora_action&&lora_changes==4);
 std::puts("PASS actual dispatch: active-radio pages, main-owner upstream toggle, maintenance consumption and identity/ticket fences");
}
'''
(a.out/'command-test.cpp').write_text(prefix+command+poll+suffix)
cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O1','-Wall','-Wextra','-Werror','-I'+str(APP/'main'),'-I'+str(APP.parent/'w12/main'),str(a.out/'command-test.cpp'),'-o',str(a.out/'command-test')]
subprocess.run(cmd,check=True,timeout=20);subprocess.run([a.out/'command-test'],check=True,timeout=10)
