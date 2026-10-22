#!/usr/bin/env python3
"""Actual CONTROL COLD receive publication regressions; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--old-root',type=Path);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
source=app if a.old_root is None else a.old_root.resolve()/'firmware/t096'
text=(source/'src/ble_service.c').read_text();start=text.index('  r2_group_admission_client(&public);',text.index('  worker_stage(4);'));end=text.index('r2_radio_service_client(&public,&roster,now);',start)+len('r2_radio_service_client(&public,&roster,now);')
(out/'cold_client_publish.inc').write_text(text[start:end]+'\n')
header=(app/'include/group_admission.h').read_text();start=header.index('struct r2_group_client {');end=header.index('\n};',start)+3;(out/'cold_client_types.inc').write_text(header[start:end]+'\n')
cmd=[os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),str(app/'tests/cold_client_test.c'),'-o',str(out/'test')]
with(out/'compile.log').open('w')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
with(out/'run.log').open('w')as log:r=subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,timeout=30)
passed=(r.returncode!=0)if a.old_root else(r.returncode==0)
(out/'receipt.json').write_text(json.dumps(dict(passed=passed,expected_old_failure=bool(a.old_root),returncode=r.returncode,hardware=False,sources={str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in(source/'src/ble_service.c',app/'tests/cold_client_test.c',Path(__file__))},limits='Actual publication slice; public client ABI, producer/consumer are faked. Full RF and controller scheduling remain separate.'),indent=2)+'\n')
assert passed
print('PASS COLD publication old-negative'if a.old_root else'PASS COLD publication actual-body')
