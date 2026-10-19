#!/usr/bin/env python3
"""MIT. Copyright (c) 2026 Jonathan Yantis. Actual RC scan/pinned abort path."""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--legacy-continuous',action='store_true');a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
service=app/'src/rc_dynamic_service.c';src=service.read_text()
m=re.search(r'struct bt_le_scan_param q = \{[^}]+\};',src);assert m
assert 'bt_le_scan_start(&q, NULL)'in src[m.end():m.end()+160]
params=m[0]
if a.legacy_continuous:params=params.replace('parent_synced?8:16','parent_synced?8:32')
controller=a.deps.resolve()/'zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_scan.c';s=controller.read_text();start=s.index('static int is_abort_cb(void *next, void *curr, lll_prepare_cb_t *resume_cb)\n{');end=s.index('\nstatic void abort_cb(',start)
(out/'actual_scan.h').write_text('static struct bt_le_scan_param actual_scan(bool parent_synced){'+params+'return q;}\n'+s[start:end])
cc=os.environ.get('CC')or shutil.which('clang');cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),str(app/'tests/rc_passive_scan_test.c'),'-o',str(out/'test')]
passed=False
try:
 with(out/'test.log').open('x')as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30);subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
 passed=True
finally:(out/'receipt.json').write_text(json.dumps(dict(passed=passed,hardware=False,legacy_continuous=a.legacy_continuous,scope='ACTUAL_APP_SCAN_PARAMETERS_AND_PINNED_PREEMPT_CALLBACK_NOT_QUEUE_OVERFLOW_REPRODUCTION',sources={str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in [service,controller,app/'tests/rc_passive_scan_test.c']},command=cmd),indent=2)+'\n')
print('PASS RC passive search uses pinned cancel-on-preemption, not continuous scan resume')
