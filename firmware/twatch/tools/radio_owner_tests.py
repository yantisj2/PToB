#!/usr/bin/env python3
"""Offline watch SX1262 owner recovery with deterministic peripheral boundaries.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1];N30=APP.parent/'n30';W12=APP.parent/'w12'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 # Reuse the public peripheral fake, adapting only the documented watch
 # SPI/reset routes. The production watch owner itself is included verbatim.
 source=N30/'tests/lora_owner_test.cpp';fake=source.read_text()
 fake='#include \"lora_shadow_input.h\"\nextern \"C\" void watch_lora_shadow_set_observer(watch_lora_shadow_observer){}\n'+fake
 fake=fake.replace('return f==2&&t==1;', 'return f==2&&t==0;') # Dynamic RX has no TX pin.
 fake=fake.replace('pin==12','pin==47').replace('gpio[12]','gpio[47]')
 fake=fake.replace('c->mosi_io_num==10&&c->miso_io_num==11&&c->sclk_io_num==9','c->mosi_io_num==34&&c->miso_io_num==33&&c->sclk_io_num==35')
 # Keep N30's complete race/boundary assertions and reset the watch-only
 # recovery and shadow state between cases as well as the shared owner state.
 assert fake.count('assert(configured_receiver());')==1
 fake=fake.replace('assert(configured_receiver());','capture_ready=recover_pending=false;recover_requested_at=0;recover_owner_generation=recover_cancel_serial=0;shadow={};shadow_published={};radio_sleeping=false;sleep_state={};sleep_published={};assert(configured_receiver());')
 (out/'driver').mkdir();gpio=N30/'tests/lora_fakes/driver/gpio.h'
 (out/'driver/gpio.h').write_text('#pragma once\n#include "'+str(gpio)+'"\nconstexpr int GPIO_NUM_36=36,GPIO_NUM_47=47,GPIO_NUM_48=48;\n')
 (out/'baseline.cpp').write_text(fake)
 (out/'recovery.cpp').write_text(fake[:fake.rindex('#ifdef PTOB_N30_TEST_REAL_RC8')]+'\n#include "lora_fault_recovery_native.inc"\n')
 # Model the Semtech sleep handshake explicitly: BUSY remains high until NSS
 # falls, and no SPI command may be issued while asleep.
 sleepy=fake[:fake.rindex('#ifdef PTOB_N30_TEST_REAL_RC8')]
 sleepy='namespace fake {bool asleep=false,wake_fail=false,sleep_cancel=false,late_header=false;}\n'+sleepy
 sleepy=sleepy.replace('fake::writes++;fake::gpio[pin]=level;', 'fake::writes++;fake::gpio[pin]=level;if(pin==47&&!level){fake::asleep=false;fake::gpio[48]=0;}if(pin==36&&!level&&fake::asleep&&!fake::wake_fail){fake::asleep=false;fake::gpio[48]=0;}')
 sleepy=sleepy.replace('assert(wait==5&&!fake::pending);', 'assert(wait==5&&!fake::pending&&!fake::asleep);')
 sleepy=sleepy.replace('*completed=t;fake::pending=nullptr;', '*completed=t;fake::pending=nullptr;if(tx[0]==0x84){assert(n==2&&tx[1]==4);fake::asleep=true;fake::gpio[48]=1;if(fake::sleep_cancel){fake::sleep_cancel=false;assert(v12::lora_request(false,0));}}if(tx[0]==0x80&&fake::late_header){fake::late_header=false;fake::irq=0x10;fake::gpio[14]=1;mcpwm_capture_event_data_t e{fake::capture()};fake::physical(reinterpret_cast<void*>(1),&e,fake::physical_cookie);}')
 (out/'sleep.cpp').write_text(sleepy+'\n#include "lora_sleep_native.inc"\n')

 cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
 flags=['-DPTOB_TWATCH_LORA_DYNAMIC_RX=1','-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DN30_LORA_OWNER_SOURCE="'+str(APP/'main/native_lora.cpp')+'"']
 flags+=['-I'+str(x) for x in (out,APP/'main',APP/'tests',N30/'main',N30/'tests/lora_fakes',W12/'main',W12/'legacy')]
 for case in ('baseline','recovery','sleep'):
  with (out/(case+'-build.log')).open('w') as log:subprocess.run([cxx,*flags,out/(case+'.cpp'),'-o',out/case],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
  with (out/(case+'.log')).open('w') as log:subprocess.run([out/case],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 sources=[source,gpio,APP/'main/native_lora.cpp',APP/'main/lora_shadow.hpp',APP/'main/lora_shadow_input.h',APP/'main/radio_recovery.hpp',APP/'tests/lora_fault_recovery_native.inc',APP/'tests/lora_sleep_native.inc',Path(__file__)]
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','hardware_access':False,'scope':'SYNTHETIC_PERIPHERALS_ACTUAL_WATCH_RADIO_OWNER','sources':{str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in sources}},indent=2)+'\n')
 print('PASS watch SX1262 actual owner baseline, fault recovery and scheduled sleep; no hardware')
if __name__=='__main__':main()
