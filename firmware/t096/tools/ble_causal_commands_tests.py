#!/usr/bin/env python3
"""Actual console parser/history/framing boundary; no hardware.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
A=Path(__file__).resolve().parents[1];O=a.out.resolve();O.mkdir(parents=True,exist_ok=False)
(O/'zephyr').mkdir();(O/'zephyr/kernel.h').write_text('/* Public host kernel boundary. */\n');(O/'build_identity.h').write_text('#define R2_SOURCE_SHA "'+'f'*64+'"\n')
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(x)for x in[A/'include',A/'protocol',O]]]
inputs=[A/'tests/ble_causal_command_test.c',A/'src/core.c',A/'src/ble_causal_events.c',A/'src/timing_events.c',A/'include/ble_causal_events.h',A/'include/core.h',Path(__file__)];sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();r={'status':'INCOMPLETE','hardware':False,'inputs':{str(p):sha(p)for p in inputs},'steps':[]}
try:
 for mode,defs in [('capture',['-DR2_BLE_CAUSAL_EVENTS=1','-DR2_GM7_NATIVE=1','-DR2_TIMING_EVENTS_TEST=1']),('gm7',['-DR2_GM7_NATIVE=1']),('legacy',[])]:
  sources=[A/'tests/ble_causal_command_test.c',A/'src/core.c']+([A/'src/ble_causal_events.c',A/'src/timing_events.c']if mode=='capture'else[])
  for label,cmd in [(mode+'-compile',flags+defs+list(map(str,sources))+['-o',str(O/mode)]),(mode,[str(O/mode)])]:
   r['steps'].append({'label':label,'command':cmd})
   with(O/(label+'.log')).open('x')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=45)
 assert all(sha(p)==h for p,h in r['inputs'].items());r['status']='PASS'
finally:(O/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
print('PASS actual303 console grammar/history/frame boundary',O)
