#!/usr/bin/env python3
"""Focused actual GM7 cycle re-election regression; no hardware operations.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
a=p.parse_args();app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
for n in('kernel.h','sys/atomic.h','random/random.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h','bluetooth/hci.h'):
 f=out/'zephyr'/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Fake public platform boundary. */\n')
sources=[app/'protocol'/n for n in('gm7_codec.c','gm7_control.c','gm7_owner.c','gm7_schedule.c','gm7_air.c','gm7_bridge.c','gm7_promote.c','group_auth.c','gnss_diagnostics.c')]
sources += [app/'src'/n for n in('gm7_source.c','precision.c','ui_render.c')]
sources += [tiny/'source'/n for n in('hmac.c','sha256.c','utils.c')]
sources += [app/'tests/gm7_re_election_service_test.c']
inputs=sources+[app/'src/gm7_service.c',app/'tests/gm7_service_test.c',app/'tests/gm7_service_fake.h',Path(__file__).resolve()]
inputs += sorted((app/'include').glob('*.h'))+sorted((app/'protocol').glob('*.h'))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();bound={str(f):sha(f)for f in inputs}
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_GM7_NATIVE=1','-DR2_GM5_NATIVE=1',*['-I'+str(f)for f in(app/'include',app/'protocol',app/'tests',tiny/'include',out)]]
r=dict(status='INCOMPLETE',class_='ACTUAL_PRODUCTION_CYCLE_SOURCE_OWNER_RESERVATION_FAKE_RF',inputs=bound,commands=[],hardware_operated=False,limits='Static seeded reachable retained scope and fake public kernel/HAL/RF boundaries; actual source extraction, owner policy, cycle, scheduler/reservation and first-ANN completion. Not field-cause, CPU-fix or measured RF/UTC evidence.')
try:
 for label,cmd in [('compile',flags+list(map(str,sources))+['-o',str(out/'re-election')]),('run',[str(out/'re-election')])]:
  r['commands'].append(cmd)
  with(out/(label+'.log')).open('x')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=45)
 assert all(sha(Path(f))==h for f,h in bound.items())
 r['status']='PASS'
finally:
 r['files']={str(f.relative_to(out)):sha(f)for f in out.rglob('*')if f.is_file()and f.name!='receipt.json'}
 (out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
print('PASS actual GM7 cold re-election and retained source/RF/owner guards:',out)
