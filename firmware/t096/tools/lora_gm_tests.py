#!/usr/bin/env python3
"""Actual LoRa-constrained GM election/JOIN regressions; synthetic RF/session inputs.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
for name in ['kernel.h','sys/atomic.h','random/random.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h']:
 h=out/'zephyr'/name;h.parent.mkdir(parents=True,exist_ok=True);h.write_text('/* Fake public controller boundary. */\n')
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
flags=[os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all',*[f'-I{x}'for x in [out,app/'tests',app/'include',app/'protocol',tiny/'include']]]
crypto=[app/'protocol'/n for n in ['group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c']]+[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]
sources=[];passed=False
try:
 for name in ['lora_gm_coordination','gm_yield_network','gm_pair_hint','gm_startup_network','lora_gm_admission']:
  test=app/'tests'/(name+'_test.c');units=[test,*crypto]
  if name!='lora_gm_admission':units += [app/'protocol/network.c',app/'protocol/election_policy.c']
  sources.extend(units);cmd=[*flags,*(['-Wno-unused-function','-Wno-unused-variable']if name=='lora_gm_admission'else[]),*map(str,units),'-o',str(out/name)]
  with (out/(name+'.compile.log')).open('x')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
  with (out/(name+'.run.log')).open('x')as log:subprocess.run([str(out/name)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps(dict(passed=passed,hardware=False,sources={str(x):hashlib.sha256(x.read_bytes()).hexdigest()for x in set(sources+[app/'src/group_admission.c',app/'protocol/gm_coordination.h'])}),indent=2)+'\n')
print('PASS actual GM election, direct admission, LoRa constraints and prior yield/startup regressions; no hardware')
