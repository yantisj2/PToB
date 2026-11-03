#!/usr/bin/env python3
"""Actual bounded HOLD core, ASan/UBSan; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
units=[app/'tests/holdover_test.c',app/'src/rc_boundary.c',app/'protocol/group_auth.c',*[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]]
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').is_file()else shutil.which('clang'))
cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(app/'include'),'-I'+str(app/'protocol'),'-I'+str(tiny/'include'),*map(str,units),'-o',str(out/'test')]
passed=False
try:
 with (out/'test.log').open('x')as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  owner=cmd.copy();owner.insert(1,'-I'+str(app/'tests/holdover-stubs'));owner.insert(1,'-DR2_TIMING_EVENTS_TEST=1');owner[owner.index(str(app/'tests/holdover_test.c'))]=str(app/'tests/holdover_owner_test.c');owner[-1]=str(out/'owner')
  subprocess.run(owner,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'owner'),str(out/'owner-trace.jsonl')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  flags=cmd[:cmd.index(str(units[0]))]
  for name,files in [('core',['src/core.c','tests/core_test.c']),('ui',['src/ui_render.c','src/precision.c','tests/ui_sync_display_test.c'])]:
   binary=out/name
   subprocess.run([*flags,*[str(app/f)for f in files],'-o',str(binary)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
   subprocess.run([str(binary),str(out)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([__import__('sys').executable,str(app/'tests/holdover_analyze_test.py')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([__import__('sys').executable,str(app/'tests/holdover_replay_test.py'),str(out/'owner-trace.jsonl')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:
 exercised=[Path(__file__).resolve(),app/'include/holdover.h',app/'src/holdover_service.inc',app/'tests/holdover_owner_test.c',*units]
 exercised += [app/f for f in ['src/core.c','src/radio_audit.c','include/radio_audit.h','tests/holdover-stubs/zephyr/kernel.h','src/ui_render.c','src/precision.c','include/ui.h','tests/core_test.c','tests/ui_sync_display_test.c','tools/holdover_analyze.py','tools/holdover_replay.py','tests/holdover_analyze_test.py','tests/holdover_replay_test.py']]
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'command':cmd,'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in exercised if f.is_file()}},indent=2)+'\n')
 print((out/'test.log').read_text())
