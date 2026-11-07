#!/usr/bin/env python3
"""Historical BLE dial policy and actual render regression. Offline only.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline-app',type=Path);a=p.parse_args()
A=Path(__file__).resolve().parents[1];B=a.baseline_app.resolve()if a.baseline_app else A;O=a.out.resolve();O.mkdir(parents=True,exist_ok=False)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
files=[A/'tests/ui_ble_legacy_rules_test.c',A/'tests/ui_ble_pre_lora_oracle.h',B/'src/ui_render.c',B/'src/precision.c',B/'include/ui.h',A/'tests/ui_sync_display_test.c',A/'tests/ui_rgb565_fixture.h',Path(__file__)]+list((B/'legacy_ui').glob('*'))
inputs={str(f):sha(f)for f in files if f.is_file()};r=dict(status='INCOMPLETE',hardware=False,baseline=bool(a.baseline_app),inputs=inputs,steps=[],limits=['Actual frontend/pixels with synthetic models; no RF/UTC/hardware acceptance.','PreLoRa public commitd5838882a2567a6e4b585d1559f85bfd2e3b4a0a is the oracle; LoRa model/authority remains unchanged.'])
cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang');flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1','-I'+str(B/'include'),'-I'+str(B/'protocol'),'-I'+str(A/'tests')]
def run(label,cmd):
 r['steps'].append(dict(label=label,command=cmd))
 with(O/(label+'.log')).open('x')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=45)
try:
 binary=O/'legacy';run('compile',flags+[str(A/'tests/ui_ble_legacy_rules_test.c'),str(B/'src/ui_render.c'),str(B/'src/precision.c'),'-o',str(binary)])
 run('actual-legacy-render',[str(binary),str(O)]+(['baseline']if a.baseline_app else[]))
 if not a.baseline_app:
  binary=O/'sync';run('existing-sync-compile',flags+[str(A/'tests/ui_sync_display_test.c'),str(B/'src/ui_render.c'),str(B/'src/precision.c'),'-o',str(binary)])
  run('existing-sync',[str(binary),str(O)])
 assert all(sha(f)==h for f,h in inputs.items());r['status']='PASS_BASELINE_306_REGRESSION_REPRODUCED'if a.baseline_app else'PASS'
finally:
 r['artifacts']={str(f.relative_to(O)):sha(f)for f in O.rglob('*')if f.is_file()and f.name!='receipt.json'};(O/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
print(r['status'],O)
