#!/usr/bin/env python3
"""Fault-only production snapshot and first-fault integration; no hardware."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
z=a.deps/'zephyr';native=z/'subsys/bluetooth/host/conn_internal.h'
raw=native.read_text();enum=re.search(r'typedef enum __packed \{.*?\} bt_conn_state_t;',raw,re.S).group(0)
for name in ['zephyr/kernel.h','zephyr/bluetooth/conn.h','zephyr/sys/atomic.h','zephyr/random/random.h','zephyr/bluetooth/bluetooth.h','zephyr/bluetooth/gatt.h']:
 f=out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Fake public boundary */\n')
(out/'conn_internal.h').write_text('#pragma once\n#define __packed __attribute__((packed))\n#define K_WORK_MASK 31\n'+enum+'\nstruct k_work {unsigned flags;};\nstruct k_work_delayable {struct k_work work;};\nstruct bt_conn {bt_conn_state_t state;unsigned short handle;struct k_work tx_complete_work;struct k_work_delayable deferred_work;};\n')
flags=[os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all',*[f'-I{x}' for x in [out,app/'include',app/'protocol',app/'tests']]]
passed=[]
try:
 for label,defs in [('snapshot',['-DCONFIG_BT_CONN_TX=1']),('no-tx',[]),('disabled',['-DTEST_DISABLED=1'])]:
  with (out/(label+'.log')).open('x') as log:
   subprocess.run([*flags,*defs,str(app/'tests/disconnect_diag_test.c'),'-o',str(out/label)],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=30)
   subprocess.run([str(out/label)],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=30)
  passed.append(label)
 with (out/'controller.log').open('x') as log:
  subprocess.run([*flags,str(app/'tests/controller_snapshot_test.c'),'-o',str(out/'controller')],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=30)
  subprocess.run([str(out/'controller')],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=30)
 passed.append('controller')
 tiny=a.deps/'modules/crypto/tinycrypt/lib'
 units=[app/'tests/disconnect_diag_test.c',*[app/'protocol'/n for n in ['network.c','election_policy.c','gm_diagnostics.c','group_join.c','group_auth.c','group_control.c','authority_codec.c','precision_codec.c','relay_codec.c']],*[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]]
 with (out/'transport.log').open('x') as log:
  subprocess.run([*flags,'-DR2_GM5_NATIVE=1','-DTEST_TRANSPORT=1','-I'+str(tiny/'include'),*map(str,units),'-o',str(out/'transport')],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=60)
  subprocess.run([str(out/'transport')],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=30)
 passed.append('transport')
finally:
 sources=[app/'src/disconnect_diag.c',app/'src/controller_snapshot.c',app/'src/controller_snapshot_body.inc',app/'include/controller_snapshot.h',app/'tests/controller_snapshot_test.c',app/'include/disconnect_diag.h',app/'protocol/ble_transport.c',app/'protocol/ble_transport.h',app/'tests/disconnect_diag_test.c',Path(__file__).resolve(),native]
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'physical':'UNKNOWN','sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in sources}},indent=2)+'\n')
print('PASS actual fault snapshot and first-fault transport with ASan/UBSan; field cause UNKNOWN')
