#!/usr/bin/env python3
"""Actual pinned ticker enqueue preemption and native GM flash producer guard.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
HAL/JOB execution is a model; no hardware, timing or field-cause assertion.
"""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
from flash_thread_guard import generate
from gm7_controller_hooks import generate as gm_generate
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--build',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);z=a.deps.resolve()/'zephyr'
source=z/'subsys/bluetooth/controller/flash/soc_flash_nrf_ticker.c';manifest=generate(source,out/'generated');stock=source.read_text();guarded=(out/'generated'/source.name).read_text();ticker=(a.build/'target/r2_controller/ticker/ticker.c').read_text();ull=(a.build/'target/gm7_controller/ull.c').read_text();gm_generate(a.build/'target/r2_controller_diag/ull.c',a.build/'target/r2_radio_controller/lll_scan.c',a.build/'target/r2_radio_controller/lll_scan_aux.c',a.build/'target/r2_precision_controller/ull_adv_sync.c',out/'gm',z/'subsys/bluetooth/controller/hci/hci_driver.c');hci=(out/'gm/hci_driver.c').read_text()
def fn(s,n):
 m=re.search(r'^(?:static )?(?:void|uint8_t|int)\s+'+n+r'\([^;]*?\)\s*\{',s,re.M);assert m,n
 i=s.index('{',m.start());level=1;j=i+1
 while level:
  level+=(s[j]=='{')-(s[j]=='}');j+=1
 return s[m.start():j]
start=ticker.index('#if defined(CONFIG_BT_TICKER_EXT)\nuint8_t ticker_start(');end=ticker.index('\n#if defined(CONFIG_BT_TICKER_UPDATE)',start)
start_types=ticker.index('struct ticker_user_op_start {');end_types=ticker.index('\n/* User data structure for operations',start_types);user=ticker.index('struct ticker_user {')
(out/'ticker_fairness_types.inc').write_text(ticker[start_types:end_types]+'\n'+ticker[user:ticker.index('\n};',user)+3]+'\n')
prototype='uint8_t ticker_start_ext(uint8_t,uint8_t,uint8_t,uint32_t,uint32_t,uint32_t,uint32_t,uint16_t,uint32_t,ticker_timeout_func,void*,ticker_op_func,void*,struct ticker_ext*);\n'
update_begin=ticker.index('uint8_t ticker_update(uint8_t instance_index');update_end=ticker.index('\n#endif /* CONFIG_BT_TICKER_UPDATE */',update_begin)
prototype+='uint8_t ticker_update_ext(uint8_t,uint8_t,uint8_t,uint32_t,uint32_t,uint32_t,uint32_t,uint16_t,uint8_t,ticker_op_func,void*,uint8_t,uint8_t);\n'
actual=prototype+ticker[start:end]+'\n'+ticker[update_begin:update_end]+'\n'+fn(ticker,'ticker_stop')+'\n';(out/'flash_thread_actual.inc').write_text(actual)
locks='K_MUTEX_DEFINE(gm7_thread_api);\n'+fn(ull,'r2_gm7_thread_lock')+'\n'+fn(ull,'r2_gm7_thread_unlock')+'\n';(out/'flash_thread_locks.inc').write_text(locks)
section=re.search(r'\tr2_gm7_thread_lock\(\);\n\tevt = hci_cmd_handle\(buf, \(void \*\*\) &node_rx\);\n\tr2_gm7_thread_unlock\(\);',hci);assert section
(out/'flash_thread_hci.inc').write_text(section[0]+'\n')
section=re.search(r'\tr2_gm7_thread_lock\(\);\n\terr = hci_acl_handle\(buf, &evt\);\n\tr2_gm7_thread_unlock\(\);',hci);assert section
(out/'flash_thread_acl.inc').write_text(section[0]+'\n')
def enqueue(s,kind):
 body=fn(s,'nrf_flash_sync_exe')
 if kind=='start':
  begin=body.index('\tret = ticker_start(');end=body.index('\n\n\tif (ret !=',begin)
  if '\tr2_gm7_thread_lock();'in body[:begin]:begin=body.rindex('\tr2_gm7_thread_lock();',0,begin)
 else:
  begin=body.index('\t\tret = ticker_stop(');end=body.index('\n\t\t__ASSERT',begin)
  if '\t\tr2_gm7_thread_lock();'in body[:begin]:begin=body.rindex('\t\tr2_gm7_thread_lock();',0,begin)
 return body[begin:end]
for version,s in [('old',stock),('new',guarded)]:
 for kind in ('start','stop'):
  block=enqueue(s,kind);prefix='static uint32_t flash_'+kind+'_'+version+'(void) {uint8_t instance_index=0,ticker_id=8;uint32_t ret;void *op_desc=&flash_context;\n'
  (out/('flash_'+kind+'_'+version+'.inc')).write_text(prefix+block+'\n(void)op_desc;return ret;}\n')
# The guard encloses only thread enqueues. Every ULL callback remains byte-identical.
assert stock[:stock.index('int nrf_flash_sync_exe')].replace('#include "soc_flash_nrf.h"','#include "soc_flash_nrf.h"\n#include "gm7_controller.h"')==guarded[:guarded.index('int nrf_flash_sync_exe')]
body=fn(guarded,'nrf_flash_sync_exe');assert body.index('r2_gm7_thread_unlock();')<body.index('k_sem_take(')
assert 'r2_gm7_thread_lock'not in body[body.index('k_sem_take('):body.index('ticker_stop(')-30] # no lock spanning wait
cmd=[os.environ.get('CC')or shutil.which('clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-g','-O1','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'tests'),str(app/'tests/flash_thread_test.c'),'-o',str(out/'test')]
passed=False
try:
 with(out/'test.log').open('w')as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=45);subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=45)
 passed=True
finally:
 files=[Path(__file__),app/'tests/flash_thread_test.c',app/'tools/flash_thread_guard.py',app/'tools/gm7_controller_hooks.py',source,a.build/'target/r2_controller/ticker/ticker.c',a.build/'target/gm7_controller/ull.c',a.build/'target/gm7_controller/hci_driver.c']
 (out/'receipt.json').write_text(json.dumps(dict(passed=passed,hardware=False,command=cmd,manifest=manifest,files={str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in files},actual_queue_sha256=hashlib.sha256(actual.encode()).hexdigest(),limits=['Exact pinned ticker_start/start_us/ticker_stop payload and ringpublication bodies; deterministic preemption at their existing cpu_dmb before publish','Fake HAL/JOB/completion and HCIcommand boundary; demonstrates a possible lost operation, not physical attribution of the captured timeout','New guard uses existing mutex and releases before sem_sync; stock timeout/ULL callback lifetime unchanged']),indent=2)+'\n')
print((out/'test.log').read_text())
