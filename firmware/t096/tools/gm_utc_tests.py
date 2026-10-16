#!/usr/bin/env python3
"""Finite production UTC-path tests, ASan/UBSan; no hardware or simulation."""
import argparse,hashlib,json,subprocess,shutil
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);p.add_argument('--deps',required=True,type=Path);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(exist_ok=False,parents=True)
shutil.copytree(app,out/'source',ignore=shutil.ignore_patterns('__pycache__','*.pyc'));src=out/'source'
def extract(path,start,end):
 s=(src/path).read_text();return s[s.index(start):s.index(end,s.index(start))]
(out/'gm_utc_under_test.h').write_text('\n'.join([
 extract('protocol/network.c','static struct v12_dc_coarse local_source(','\nvoid r2_network_tick('),
 extract('src/group_admission.c','static struct r2_join_record message(','\nstatic void accepted_peer('),
 extract('src/radio_service.c','static int generic_encode(','\nstatic void '),
 extract('src/radio_service.c','static int prepare_data(','\n/* Control admission')]))
(out/'zephyr').mkdir();(out/'zephyr/kernel.h').write_text('/* Header-only test boundary. */\n')
(out/'ui_binding_under_test.h').write_text(extract('src/ble_service.c','void r2_service_ui(','\nint r2_service_name('))
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
cc=(__import__('os').environ.get('CC') or __import__('shutil').which('clang'))
flags=[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g', '-I'+str(src/'include'),'-I'+str(src/'protocol'),'-I'+str(out),'-I'+str(tiny/'include')]
crypto=[src/'protocol'/x for x in ['group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c']]+[tiny/'source'/x for x in ['hmac.c','sha256.c','utils.c']]
cases=[('gm-utc-owner',[src/'tests/gm_utc_owner_test.c',src/'src/clock.c',src/'src/nmea.c',src/'src/precision.c']),('gm-utc',[src/'tests/gm_utc_test.c',src/'src/precision.c',src/'src/clock.c',src/'src/nmea.c',*crypto]),('precision',[src/'tests/precision_test.c',src/'src/precision.c']),('clock',[src/'tests/clock_test.c',src/'src/clock.c',src/'src/nmea.c']),('group-join',[src/'tests/group_join_test.c',*crypto]),('group-network',[src/'tests/group_network_test.c',src/'protocol/network.c',src/'protocol/election_policy.c',*crypto]),('radio-precision',[src/'tests/radio_precision_test.c',src/'src/radio_precision.c']),('ui-binding',[src/'tests/ui_binding_test.c',src/'src/ui_render.c',src/'src/precision.c'])]
results=[]
try:
 for name,files in cases:
  with (out/(name+'-compile.log')).open('w') as f:subprocess.run([*flags,*(['-DR2_SWITCHABLE_FIELD=1'] if name=='gm-utc-owner' else []),*map(str,files),'-o',str(out/name)],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=45)
  with (out/(name+'-run.log')).open('w') as f:subprocess.run([str(out/name)],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
  results.append(name)
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':len(results)==len(cases),'cases':results,'files':{str(f.relative_to(out)):hashlib.sha256(f.read_bytes()).hexdigest() for f in out.rglob('*') if f.is_file()}},indent=2)+'\n')
 print(json.dumps({'passed':len(results)==len(cases),'cases':results,'out':str(out)}))
