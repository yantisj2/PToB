#!/usr/bin/env python3
"""Offline W12 fine owner, counter and shutdown checks using synthetic peripherals.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess
from build import fine_backend,validate_fine_compilation
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
N30=APP.parent/'n30'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 device={'device_id':'0000020000000005'}
 receipt={'fine_clock':True,'clock_backend':fine_backend(True),'listening_percent':100,'gnss_fast_map_diagnostic':False}
 compiled=[{'file':'/app/'+n,'command':'cc -DPTOB_W12_FINE_CLOCK=1'} for n in ('native_main.cpp','gnss_pps.cpp','native_g1.cpp','periodic_rx.cpp','w12_fine_clock.cpp')]
 assert validate_fine_compilation(receipt,device,compiled)
 def refuses(r,rows):
  try:validate_fine_compilation(r,device,rows)
  except ValueError:return
  raise AssertionError('Unbound fine configuration admitted')
 refuses({**receipt,'fine_clock':False},compiled)
 refuses({**receipt,'listening_percent':50},compiled)
 refuses({**receipt,'gnss_fast_map_diagnostic':True},compiled)
 refuses({**receipt,'clock_backend':fine_backend(False)},compiled)
 refuses(receipt,compiled[:-1]);refuses(receipt,compiled+compiled[-1:])
 for i in range(4):
  rows=[dict(r) for r in compiled];rows[i]['command']='cc';refuses(receipt,rows)
  rows[i]['command']='cc -DPTOB_W12_FINE_CLOCK=1 -DPTOB_W12_FINE_CLOCK=0';refuses(receipt,rows)
 cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
 # Reuse the existing synthetic SDK model, adapting only its pin and channel
 # expectations. These fixtures do not establish actual peripheral semantics.
 for f in (N30/'tests/fine_fakes').rglob('*'):
  if not f.is_file():continue
  dest=out/'fakes'/f.relative_to(N30/'tests/fine_fakes');dest.parent.mkdir(parents=True,exist_ok=True)
  s=f.read_text().replace('pin==4||pin==5','pin==16').replace('gpio==4||gpio==5','gpio==16')
  s=s.replace('c->flags.pos_edge==unsigned(i<2&&PTOB_N30_FINE_PINS)', 'c->flags.pos_edge==unsigned(i<2)').replace('c->flags.neg_edge==unsigned(i<2&&PTOB_N30_FINE_PINS)', 'c->flags.neg_edge==unsigned(i==0)')
  s=s.replace('gpio==16','gpio==16||gpio==14').replace('assert(bits==(3u<<27))','assert(bits==(3u<<27)||bits==(1u<<28))').replace('assert(x==(1u<<29)||x==(3u<<27))','assert(x==(1u<<29)||x==(3u<<27)||x==(1u<<28))')
  if f.name=='fine_fake.hpp':
   s=s.replace('(x==(3u<<27)&&fake_radio_clear_ok)', '((x==(3u<<27)||x==(1u<<28))&&fake_radio_clear_ok)')
   s+='\ninline int mcpwm_del_capture_channel(mcpwm_cap_channel_handle_t c){assert(!c->fsm);return fake_disable_ok?0:-1;}\ninline int mcpwm_del_capture_timer(mcpwm_cap_timer_handle_t t){assert(!t->fsm);return fake_disable_ok?0:-1;}\n'
  dest.write_text(s)
 (out/'fakes/w12_fine_config.h').write_text('#pragma once\n#define PTOB_W12_FINE_FACTORY UINT64_C(0x020000003001)\n#define PTOB_W12_FINE_RX_GPIO 16\n')
 src=(APP/'main/native_main.cpp').read_text()
 (out/'actual_fine_stop_all.hpp').write_text(src[src.index('void stop_all(){'):src.index('#ifdef V12_R2_ENABLED\n// Keep',src.index('void stop_all(){'))])
 (out/'actual_fine_update_gate.hpp').write_text(src[src.index('[[gnu::noinline]] void poll_update_drain('):src.index('[[gnu::noinline]] void clock_fault_progress()')])
 flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DPTOB_W12_FINE_CLOCK=1','-DPTOB_N30_FINE_PINS=1','-I'+str(out),'-I'+str(out/'fakes'),'-I'+str(APP/'main')]
 def run(command,label):
  with (out/(label+'.log')).open('w') as log:subprocess.run(list(map(str,command)),cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=40)
 run([cxx,*flags,APP/'tests/fine_clock_test.cpp','-o',out/'owner'],'owner-compile')
 for case in ('live','lora','pending_late','cleanup','wrap','storm','gap','route','enable','timeout','stopped','stale_raw','stale_radio','irq'):run([out/'owner',case],'owner-'+case)
 run([cxx,*flags,APP/'tests/fine_shutdown_test.cpp','-o',out/'shutdown'],'shutdown-compile')
 for case in ('clean','timeout','cleanup'):run([out/'shutdown',case],'shutdown-'+case)
 for name in ('fine_counter','fine_receiver'):
  # Same algorithm vectors exercise the W12 copies rather than N30 headers.
  s=(N30/'tests'/(name+'_test.cpp')).read_text().replace('n30::fine','w12::fine')
  if name=='fine_counter':
   # History belongs to the finite N30 observer, outside this W12 port.
   s=s[:s.index(' History ')]+'\n}\n' if ' History ' in s else s
  f=out/(name+'.cpp');f.write_text(s);run([cxx,*flags,f,'-o',out/name],name+'-compile');run([out/name],name)
 run([cxx,*flags,APP/'tests/fine_clock_prediction_test.cpp','-o',out/'slew'],'slew-compile');run([out/'slew'],'slew')
 paths=[APP/'main/native_main.cpp',APP/'tests/fine_shutdown_test.cpp',APP/'tests/fine_clock_prediction_test.cpp',APP/'main/w12_fine_clock.cpp',APP/'main/fine_counter.hpp',APP/'main/fine_receiver.hpp',APP/'tests/fine_clock_test.cpp',Path(__file__),N30/'tests/fine_counter_test.cpp',N30/'tests/fine_receiver_test.cpp',*sorted((N30/'tests/fine_fakes').rglob('*'))]
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'W12_ACTUAL_RX_ONLY_OWNER_ASAN_UBSAN_SYNTHETIC_PERIPHERALS','hardware_access':False,'sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()}},indent=2)+'\n')
 print('PASS W12 fine owner/counter/receiver with ASan/UBSan; hardware untested')
if __name__=='__main__':main()
