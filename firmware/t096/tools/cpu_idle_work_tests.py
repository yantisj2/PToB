#!/usr/bin/env python3
"""Actual unchanged-work guards with synthetic boundaries; no hardware.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline',type=Path)
 a=p.parse_args();o=a.out.resolve();o.mkdir(parents=True,exist_ok=False)
 files=[APP/x for x in ['src/lora_passive_service.c','src/ui_service.c','src/lora_hw.c','tests/lora_ram_fixture.h','tests/cpu_lora_poll_test.c','tests/cpu_context_work_test.c','tools/cpu_idle_work_tests.py']]
 before={str(f.relative_to(APP)):sha(f)for f in files}
 lora=(APP/'src/lora_passive_service.c').read_text();ui=(APP/'src/ui_service.c').read_text()
 def section(t,start,end):return t[t.index(start):t.index(end,t.index(start))]
 context=section(ui,'void r2_ui_context_update(','bool r2_ui_model_requested(')
 worker=section(lora,'static void worker(','void r2_lora_init(')
 assert lora.count('worker_priority(')==2
 priority=section(lora,'static int worker_priority(','static void worker(')
 assert priority=='static int worker_priority(void){\n return owner.stopping||fenced()?DRAIN_PRIORITY:WORKER_PRIORITY;\n}\n'
 assert 'mono(' not in worker
 (o/'context_under_test.h').write_text(context);(o/'worker_under_test.h').write_text(worker)
 # Observe projection only in the generated host body; no firmware metrics.
 marker='static void view_advance(struct r2_lora_view *v,uint64_t now){'
 def owner(t):
  assert t.count(marker)==1
  return re.sub(r'^#include[^\n]*\n','',t,flags=re.M).replace(marker,marker+'++projection_calls;')
 body=o/'owner.c';body.write_text(owner(lora))
 hw=(APP/'src/lora_hw.c').read_text();start=hw.index('static struct r2_lora_hw_rejection rejected;')
 admission=o/'admission.c';admission.write_text(hw[start:hw.index('\n#endif\nstatic int ',start)])
 tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib';llvm=Path('/opt/homebrew/opt/llvm/bin/clang')
 cc=os.environ.get('CC') or (str(llvm) if llvm.is_file() else 'clang')
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(APP/'tests'),'-I'+str(o)]
 def run(name,args):
  r=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=60);(o/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
 run('context-build',flags+[APP/'tests/cpu_context_work_test.c','-o',o/'context'])
 run('context',[o/'context'])
 lf=flags+['-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_FIXED500_EXPERIMENT=1','-I'+str(tiny/'include'),'-DR2_HW_ADMISSION_BODY="'+str(admission)+'"']
 sources=[APP/'tests/cpu_lora_poll_test.c',APP/'src/lora_passive_core.c',APP/'src/lora_receiver_watch.c',APP/'protocol/lora_passive.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',*[tiny/'source'/(n+'.c')for n in('hmac','sha256','utils')]]
 def build(name,b):run(name+'-build',lf+['-DR2_PASSIVE_SERVICE_BODY="'+str(b)+'"',*sources,'-o',o/name])
 build('poll',body)
 for mode in range(1,6):run('poll-'+str(mode),[o/'poll',str(mode)])
 if a.baseline:
  baseline=o/'baseline-owner.c';baseline.write_text(owner(a.baseline.read_text()));build('baseline-poll',baseline);run('baseline-negative-control',[o/'baseline-poll','6'])
 # Stop/drain sites and essential observation are outside the changed loop.
 assert 'k_thread_priority_set(&worker_thread,DRAIN_PRIORITY);' in section(lora,'static void stop_now(','void r2_lora_name_publish(')
 assert worker.count('k_sleep(K_MSEC(2))')==1 and 'mutex' not in re.sub(r'/\*.*?\*/','',worker,flags=re.S)
 if a.baseline:
  baseline=a.baseline.read_text()
  for start,end in [('static void stop_now(','void r2_lora_name_publish('),('void r2_lora_watchdog_check(','void r2_lora_watchdog_poll('),('void r2_lora_watchdog_poll(','int r2_lora_json(')]:
   assert section(lora,start,end)==section(baseline,start,end),start
 assert before=={str(f.relative_to(APP)):sha(f)for f in files}
 (o/'receipt.json').write_text(json.dumps(dict(status='PASS_CPU_IDLE_WORK_PRODUCTION_BODIES',hardware=False,cases=6+bool(a.baseline),source_hashes=before,baseline_sha256=sha(a.baseline)if a.baseline else None,projection_observation='host-only counter at unchanged view_advance entry',timing='No CPU timing measurement; counts and semantics only'),indent=2)+'\n')
if __name__=='__main__':main()
