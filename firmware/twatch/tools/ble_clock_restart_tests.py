#!/usr/bin/env python3
"""Actual BLE mapping reset/poll bodies with explicit clock fakes; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1];W12=APP.parent/'w12'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
source=W12/'main/periodic_rx.cpp';text=source.read_text()
def body(name):
 start=text.index(name+'(');begin=text.rfind('\n',0,start)+1;brace=text.index('{',start);depth=1;i=brace+1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[begin:i]
assert 'watch_reset_controller_mapping();' in body('start')
fixture='''#include <cassert>
#include <cstdlib>
#include <cstdio>
#include "native_transport.hpp"
#include "periodic_rx.hpp"
#include "clock_ble_mapping.hpp"
uint64_t fake_now=100000;adv_rx::BleTime ble_time{320,0};
bool clock_reference_read(uint64_t* v){*v=fake_now*20;return true;}
bool clock_reference_bt_bracket(uint64_t* b,uint64_t* e,adv_rx::BleTime* t){*b=fake_now*20;*e=*b+2;*t=ble_time;return true;}
int xPortGetCoreID(){return 0;}int esp_clk_cpu_freq(){return 240000000;}int64_t esp_timer_get_time(){return fake_now;}
namespace v12 {void transport_record(const Report&){} }
namespace v12::periodic_rx {
adv_rx::BleAnchor anchor{};adv_rx::BleMapping clock_mapping{};RfReference last_reference{};
bool installed=true,armed=false,acquiring=false,refinement_ready=false;int8_t refinement_ticks=0;
uint16_t handle=UINT16_MAX;uint32_t fault=0;uint64_t anchor_us=0,audit_us=0;
void barrier(){}bool pointers(){return true;}
'''
fixture+=body('watch_reset_controller_mapping')+'\n'+body('poll')+'\n}\n'
fixture+='''int main(){using namespace v12::periodic_rx;
 poll();assert(!fault&&anchor.before);fake_now+=30000;ble_time.half_slots+=96;poll();assert(!fault);
 fake_now+=30000;ble_time={0,0};poll();assert(fault==5); // Retained BLE anchor rejects restarted controller.
 fault=0;refinement_ready=true;refinement_ticks=2;last_reference={true,1,4,7};
 const auto continuous_counter=fake_now*20;watch_reset_controller_mapping();
 assert(!anchor.before&&!anchor_us&&!audit_us&&!refinement_ready&&!refinement_ticks&&!last_reference.valid);
 assert(fake_now*20==continuous_counter&&!fault);poll();assert(!fault&&anchor.before&&anchor.time.half_slots==0);
 fake_now+=30000;ble_time.half_slots=96;poll();assert(!fault);
 puts("PASS actual BLE mapping adapter bodies: retained-anchor negative control fault5; restarted-controller mapping reset reacquires on unchanged GPT clock; no RF precision claim");
}
'''
(out/'test.cpp').write_text(fixture)
cc='/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++'
flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DV12_R2_ENABLED=1','-DV12_R2_RX_SHADOW=1']
flags+=['-I'+str(q)for q in (W12/'tests/console_fakes',W12/'main',W12/'legacy/reference')]
with (out/'compile.log').open('w') as f:subprocess.run([cc,*flags,str(out/'test.cpp'),'-o',str(out/'test')],check=True,stdout=f,stderr=subprocess.STDOUT)
r=subprocess.run([str(out/'test')],capture_output=True,text=True);(out/'test.log').write_text(r.stdout+r.stderr);r.check_returncode()
files=[source,W12/'legacy/reference/clock_ble_mapping.hpp',Path(__file__)]
(out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'ACTUAL_ADAPTER_BODIES_SYNTHETIC_BLE_AND_GPT_CLOCKS_ASAN_UBSAN','hardware_access':False,'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in files}},indent=2)+'\n');print(r.stdout.strip())
