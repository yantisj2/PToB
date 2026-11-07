#!/usr/bin/env python3
"""Actual opportunistic UI work flow; offline synthetic input only.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT"""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();o=a.out.resolve();o.mkdir(exist_ok=False)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def manifest():return{str(p.relative_to(APP)):sha(p)for p in APP.rglob('*')if p.is_file()and p.suffix in('.c','.h','.inc','.py')and'__pycache__'not in p.parts}
before=manifest()
main=(APP/'src/main.c').read_text()
start=main.index('    struct r2_ui_context ui_context={')
end=main.index('\n#ifdef R2_TZ_WIRE',start)
snippet=main[start:end]
(o/'main_hint_under_test.h').write_text('static void main_hint_fixture(struct hint_service service,struct hint_clock local_clock){\nstruct {int phase;} maintenance={0};bool poweroff_requested=false;\n#define R2_IDLE 0\n'+snippet+'\n#undef R2_IDLE\n}\n')

cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(o),'-DR2_TZ_WIRE=1']
def run(name,args):
 r=subprocess.run(args,text=True,capture_output=True,timeout=45);(o/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode()
for n in['kernel.h','sys/atomic.h','random/random.h']:
 f=o/'zephyr'/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Existing host boundary. */\n')
for variant,extra in [('plain',[]),('lora',['-DR2_LORA=1'])]:
 vf=flags+extra
 run(variant+'-render-build',vf+['-c',str(APP/'src/ui_render.c'),'-o',str(o/(variant+'-render.o'))])
 run(variant+'-precision-build',vf+['-c',str(APP/'src/precision.c'),'-o',str(o/(variant+'-precision.o'))])
 binary=o/(variant+'-worker')
 run(variant+'-worker-build',vf+['-DR2_UI_SCHEDULE_FIXTURE=1','-DR2_UI_MAIN_PRODUCER_FIXTURE=1','-include',str(APP/'tests/ui_service_fake.h'),str(APP/'tests/ui_schedule_test.c'),str(o/(variant+'-render.o')),str(o/(variant+'-precision.o')),'-o',str(binary)])
 for scene in range(13):run(variant+'-scene'+str(scene),[str(binary),str(scene)])
assert before==manifest()
(o/'receipt.json').write_text(json.dumps(dict(status='PASS_ACTUAL_UI_OPPORTUNISTIC_FLOW',hardware=False,inputs=before,scenes=['4Hz','8Hz','2sbusy+maintainedlogic+TZ','WITHDRAW/rootchange_beforeSPI','sourcechange_inflightSPI','precommit_lock_race','paused_history','soft_hint_ABA_future_lease','inlock_drawn_race_reader_tag','inlock_history_race_reader_tag','2s_full_context_hint_burst_ratecap','no_timeslice28ms_peer_yield','virtualtap_with_peer_work']),indent=2)+'\n')
print('PASS actual UI opportunistic scheduling26scenes including independent LoRa HOLD')
