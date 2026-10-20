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
#define V12_R2_ENABLED 1
#define V12_R2_CONFIG 13
uint64_t now=1000;unsigned radio_changes=0,pages=0,statuses=0;unsigned selected=0;
bool owner_active=true;
bool mode=false,action_pending=false;
namespace v12 {
void capture_stop(){}
bool capture_begin(uint64_t,uint32_t){return true;}
bool capture_rolling_begin(uint64_t,uint32_t){return true;}
struct LoRa {bool enabled=false,requested=false,worker_busy=false,off_acknowledged=true,failed=false;};
LoRa lora_status(){return {};}
bool r2_client_start(uint64_t){++radio_changes;return true;}
bool r2_set_relay_only(bool value){mode=value;++radio_changes;return true;}
struct Activity {bool relay_only=false;};
Activity r2_activity(){return {mode};}
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
void status(uint64_t,uint64_t){++statuses;}
bool r2_command_off_ack(){return !owner_active;}
bool n30_display_page(unsigned n){if(n>1)return false;selected=n;++pages;return true;}
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
 send(3,"ui page 2");assert(!last_command.accepted&&pages==2);
 send(4,"ui page 1","02:00:00:00:30:02");assert(!last_command.accepted&&pages==2);
 send(5,"ui page 1",factory_text,4);assert(!last_command.accepted&&pages==2);
 send(6,"ui page 1",factory_text,3,true);assert(!last_command.accepted&&pages==2);
 send(7,"rx full 1000");assert(!last_command.accepted&&std::string(last_command.reason)=="R2_OWNS_BLE"&&radio_changes==0);
 poll_display_action();assert(!radio_changes);
 action_pending=true;poll_display_action();assert(mode&&radio_changes==1&&!action_pending);
 poll_display_action();assert(radio_changes==1);
 action_pending=true;poll_display_action();assert(!mode&&radio_changes==2);
 update_gate.begin(now);send(8,"ui page 1");assert(!last_command.accepted&&pages==2&&radio_changes==2);
 action_pending=true;poll_display_action();assert(!mode&&radio_changes==2&&!action_pending);
 std::puts("PASS actual dispatch: active-radio pages, main-owner upstream toggle, maintenance consumption and identity/ticket fences");
}
'''
(a.out/'command-test.cpp').write_text(prefix+command+poll+suffix)
cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O1','-Wall','-Wextra','-Werror','-I'+str(APP.parent/'w12/main'),str(a.out/'command-test.cpp'),'-o',str(a.out/'command-test')]
subprocess.run(cmd,check=True,timeout=20);subprocess.run([a.out/'command-test'],check=True,timeout=10)
