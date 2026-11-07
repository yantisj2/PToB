#!/usr/bin/env python3
"""Actual critical UI/session bodies with synthetic public API boundaries.

No hardware, timing accuracy or measured CPU-duration claim.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
A=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
p.add_argument('--event-delay-ns',type=int,default=0,help='Synthetic/target-bound fixed500 correction; no device discovery')
args=p.parse_args();assert 0<=args.event_delay_ns<=0xffffffff
O=args.out.resolve();O.mkdir(parents=True,exist_ok=False)
for name in ('kernel.h','sys/atomic.h','random/random.h'):
 q=O/'zephyr'/name;q.parent.mkdir(parents=True,exist_ok=True);q.write_text('/* public API fixture */\n')
llvm=Path('/opt/homebrew/opt/llvm/bin/clang')
cc=os.environ.get('CC')or(str(llvm)if llvm.is_file()else'clang')
flags=[cc,'-std=c11','-O1','-g',
 '-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter',
 '-fsanitize=address,undefined','-fno-sanitize-recover=all','-ffunction-sections','-fdata-sections',
 '-Wl,-dead_strip'if sys.platform=='darwin'else'-Wl,--gc-sections',
 '-DR2_UI_PARTIAL_JOBS=1','-DR2_GM7_NATIVE=1','-DR2_NATIVE_BLE=1',
 '-DR2_RC_PROFILE=1','-DR2_SWITCHABLE_FIELD=1','-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1',
 '-DR2_LORA_FIXED500_EXPERIMENT=1','-DR2_LORA_FIXED500_EVENT_DELAY_NS='+str(args.event_delay_ns),
 '-DR2_TZ_WIRE=1','-DR2_TZ_AUTO=1','-DR2_TZ_MODE=2','-DR2_TZ_MANUAL_ID=2','-DR2_TZ_MANUAL_MIN=-300',
 '-I'+str(A/'include'),'-I'+str(A/'protocol'),'-I'+str(O),
 '-DUI_FAKE_HEADER="'+str(A/'tests/ui_service_fake.h')+'"',
 '-DUI_SERVICE_SOURCE="'+str(A/'src/ui_service.c')+'"']
sources=['src/ui_service.c','src/ui_critical.inc','src/ui_render.c','src/precision.c',
 'src/bounded_json.c','src/time_zone_database.c','tests/ui_service_fake.h',
 'tests/ui_critical_state_test.c','tests/ui_critical_pipeline_test.c','tests/ui_shadow_summary_test.c',
 'include/ui.h','include/shadow_session.h','include/shadow_settle.h','include/rc_shadow.h',
 'include/bounded_json.h','tools/ui_critical_tests.py']
sources+=['include/time_zone_config.h','include/time_zone_selector.h','protocol/time_zone.h',
 'data/time_zone_data.h','legacy_ui/draw.inc','legacy_ui/legacy.h']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
before={name:sha(A/name)for name in sources};steps=[]
for label,fixture in [('state','ui_critical_state_test.c'),('pipeline','ui_critical_pipeline_test.c'),('shadow','ui_shadow_summary_test.c')]:
 binary=O/label
 command=flags+[str(A/'tests'/fixture),*[str(A/'src'/s)for s in ('ui_render.c','precision.c','bounded_json.c','time_zone_database.c')],'-o',str(binary)]
 for phase,cmd in [('compile',command),('run',[str(binary)])]:
  (O/(label+'-'+phase+'-command.json')).write_text(json.dumps(cmd,indent=2)+'\n')
  try:result=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
  except subprocess.TimeoutExpired as error:
   text=lambda value:value.decode(errors='replace')if isinstance(value,bytes)else(value or'')
   (O/(label+'-'+phase+'.log')).write_text(text(error.stdout)+text(error.stderr)+'\nTIMEOUT\n')
   raise
  (O/(label+'-'+phase+'.log')).write_text(result.stdout+result.stderr)
  steps.append({'name':label+'-'+phase,'command':cmd,'exit':result.returncode})
  print(result.stdout+result.stderr,flush=True);result.check_returncode()
assert before=={name:sha(A/name)for name in sources}
(O/'receipt.json').write_text(json.dumps({'status':'PASS_PRODUCTION_PARTIAL_UI_SYNTHETIC_BOUNDARIES',
 'production_definition':'R2_UI_PARTIAL_JOBS=1','hardware':False,'event_delay_ns':args.event_delay_ns,
 'limits':'Native layout/stack/config and real latency remain independent gates; synthetic API/SPI scheduling is not hardware timing.',
 'inputs':before,'steps':steps},indent=2)+'\n')
