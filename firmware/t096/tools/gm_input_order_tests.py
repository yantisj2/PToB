#!/usr/bin/env python3
"""Actual coherent standby-input ordering regressions; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
def fn(s,name):
 m=re.search(r'^static [^\n]*\b'+name+r'\(|^void '+name+r'\(',s,re.M);assert m,name
 start=s.index('{',m.end());end=start+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
control=(app/'src/ble_service.c').read_text();radio=(app/'src/radio_service.c').read_text()
header=(app/'include/group_admission.h').read_text();start=header.index('struct r2_group_client {');end=header.index('\n};',start)+3
(out/'gm_input_order_client.inc').write_text(header[start:end]+'\n')

(out/'gm_input_order_control.inc').write_text(fn(control,'refresh_direct_gm5_timing')+'\n')
(out/'gm_input_order_reader.inc').write_text(fn(radio,'output_read')+'\n'+fn(radio,'r2_radio_service_snapshot')+'\n')
cycle=fn(control,'cycle');assert cycle.index('refresh_direct_gm5_timing()')<cycle.index('r2_network_tick_coordinated(')<cycle.index('normal_relay_tick(scope,now,utc)')
assert 'if(!direct_gm5_timing)\n  r2_network_timing(' in cycle
assert 'false,valid,context.at)' in control
cc=os.environ.get('CC') or shutil.which('clang');tiny=a.deps/'modules/crypto/tinycrypt/lib'
units=[app/'tests/gm_input_order_test.c',*[app/'protocol'/n for n in ['election_policy.c','gm_diagnostics.c','group_join.c','group_auth.c','group_control.c','authority_codec.c','precision_codec.c','relay_codec.c']],app/'src/radio_precision.c',*[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]]
cmd=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_GM5_NATIVE=1',*[f'-I{x}' for x in [out,app/'include',app/'protocol',app/'tests',tiny/'include']],*map(str,units),'-o',str(out/'test')]
passed=False
try:
 with(out/'compile.log').open('w')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 with(out/'run.log').open('w')as log:subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:
 sources=[app/'src/ble_service.c',app/'src/radio_service.c',app/'protocol/network.c',app/'protocol/gm_standby.inc',app/'tests/gm_input_order_test.c',Path(__file__)]
 (out/'receipt.json').write_text(json.dumps(dict(passed=passed,hardware=False,command=cmd,sources={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in sources},limits='Actual CONTROL helper/PT copy+expiry/network synced bodies; fake clock/capture/mailbox ABI. Call ordering and legacy-path guard source-checked; not a full Zephyr worker or RF simulation.'),indent=2)+'\n')
print('PASS coherent pre-tick standby PT input regressions')
