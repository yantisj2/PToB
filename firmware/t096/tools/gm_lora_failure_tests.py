#!/usr/bin/env python3
"""MIT. Copyright (c) 2026 Jonathan Yantis. GM role policy, actual owner/renderer."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline',type=Path);a=p.parse_args()
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);passed=[]
 manifest={str(f.relative_to(APP)):sha(f) for f in APP.rglob('*')if f.is_file()and f.suffix in('.c','.h','.inc','.py')and '__pycache__'not in f.parts}
 implementation=a.baseline.resolve()if a.baseline else APP
 owner=out/'owner.c';owner.write_text(re.sub(r'^#include[^\n]*\n','',(implementation/'src/lora_passive_service.c').read_text(),flags=re.M))
 # Baseline lacks the new descriptive flag names. Defining unused names lets
 # the identical test fail on behavior rather than a missing C identifier.
 if a.baseline:owner.write_text('#define GM_BLE_SELECTED 1024u\n#define GM_BLE_LOST 2048u\n#define GM_TIME_CLEAR 4096u\n#define GM_TIME_BLOCKED (GM_BLE_LOST|GM_TIME_CLEAR)\n'+owner.read_text())
 hw=(APP/'src/lora_hw.c').read_text();start=hw.index('static struct r2_lora_hw_rejection rejected;');admission=out/'admission.c';admission.write_text(hw[start:hw.index('\n#endif\nstatic int ',start)])
 tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-return-type','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_FIXED500_EXPERIMENT=1','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(APP/'tests'),'-I'+str(tiny/'include')]
 sources=[APP/'tests/gm_lora_failure_test.c',APP/'src/lora_passive_core.c',APP/'src/lora_receiver_watch.c',APP/'protocol/lora_passive.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',*[tiny/'source'/(n+'.c')for n in('hmac','sha256','utils')]]
 def run(label,cmd,negative=False):
  r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/(label+'.log')).write_text(r.stdout+r.stderr)
  if negative:assert r.returncode and 'Assertion failed' in r.stderr,label
  else:r.check_returncode()
  passed.append(label)
 try:
  binary=out/'owner';run('owner-build',[*flags,'-DR2_PASSIVE_SERVICE_BODY="'+str(owner)+'"','-DR2_HW_ADMISSION_BODY="'+str(admission)+'"',*map(str,sources),'-o',str(binary)])
  for scenario in range(1,9):run('owner-'+str(scenario),[str(binary),str(scenario)],negative=bool(a.baseline and scenario in(1,2,3,6,7)))
  # Build actual renderer from the selected source; all other UI/boundaries same.
  fixture=out/'ui.c';src=(APP/'tests/gm_lora_precision_ui_test.c').read_text().replace('#include "lora_watch_ui_test.c"','#include "'+str(APP/'tests/lora_watch_ui_test.c')+'"')
  if a.baseline:
   watch=(APP/'tests/lora_watch_ui_test.c').read_text().replace('#include "../src/ui_render.c"','#include "'+str(implementation/'src/ui_render.c')+'"')
   (out/'watch.c').write_text(watch);src=src.replace(str(APP/'tests/lora_watch_ui_test.c'),str(out/'watch.c'))
  fixture.write_text(src);ui=out/'ui'
  run('ui-build',[*flags,'-DR2_LORA=1','-DR2_GM7_NATIVE=1','-DR2_TZ_WIRE=1',str(fixture),str(APP/'src/precision.c'),'-o',str(ui)])
  run('ui',[str(ui),str(out)],negative=bool(a.baseline))
  assert all(sha(APP/f)==h for f,h in manifest.items())
 finally:
  (out/'receipt.json').write_text(json.dumps(dict(status=('PASS_BASELINE_COUNTEREXAMPLES'if a.baseline else'PASS')if len(passed)==11 else'INCOMPLETE',hardware=False,cases=passed,source_hashes=manifest,baseline=str(a.baseline)if a.baseline else None,
   limits=['Synthetic public RF/capture/clock boundaries; no hardware election or UTC accuracy claim. Passive LoRa coordination remains zero/no-op; RF observation is not a new election guard.']),indent=2)+'\n')
 print('PASS GM policy actual-MAC owner and actual renderer',len(passed))
if __name__=='__main__':main()
