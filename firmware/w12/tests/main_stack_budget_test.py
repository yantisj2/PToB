#!/usr/bin/env python3
"""Reject the observed nested-snapshot failure, and incomplete stack evidence.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import json,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from stack_budget import DECLARATIONS,PPS_DECLARATIONS,FAST_MAP_DECLARATIONS,measure,check
CONFIG='CONFIG_ESP_MAIN_TASK_STACK_SIZE=12288\n'
# GCC14.2.0 ESP32-S3 0.1.6 C4 rows, before the snapshot isolation fix. The
# first ordinary status request overflowed task main on the attached unit.
FAILED={'main':4016,'input':96,'command':2448,'status':4656,'ui':3504,
        'diagnostics':3328,'update':2736,'fault':2816,'authority':2288}
# New helpers are absent in that old binary. Add conservative fixture frames
# only to let the checker reach the real unchanged failing status path.
FAILED.update(off_ack=2288,aid=2384)
def usage(frames):return '\n'.join('native_main.cpp:1:1:'+DECLARATIONS[key]+'\t'+str(size)+'\tstatic' for key,size in frames.items())
def refuses(config,text,reason):
 try:measure(config,text)
 except ValueError as e:assert reason in str(e),str(e)
 else:raise AssertionError('Unsafe or unknown stack evidence accepted')
assert sum(FAILED[k] for k in ('main','input','command','status'))==11216
refuses(CONFIG,usage(FAILED),'11216 + reserve 4096 exceeds configured 12288')
# Actual SDK C4 0.1.7 development build after isolating the two snapshots.
small={**FAILED,'main':1792,'command':256,'aid':2352}
r=measure(CONFIG,usage(small));assert r['maximum_selected_static_bytes']==6800
assert r['headroom_after_reserve_bytes']==1392
refuses(CONFIG,usage({k:v for k,v in small.items() if k!='status'}),'Missing or ambiguous')
refuses(CONFIG,usage(small)+'\n'+usage({'status':4656}),'Missing or ambiguous')
refuses(CONFIG,usage(small).replace('\t4656\tstatic','\t4656\tdynamic,bounded'),'Unbounded')
refuses(CONFIG+CONFIG,usage(small),'One exact')
refuses(CONFIG.replace('12288','8192'),usage(small),'exceeds configured')
def pps_usage(frames):return '\n'.join('native_main.cpp:1:1:'+PPS_DECLARATIONS[key]+'\t'+str(size)+'\tstatic' for key,size in frames.items())
diagnostic={'production_pps_observe':2400,'production_pps_score':384,'pps_callback':2560}
gnss=pps_usage({'gnss_poll':512})
new=measure(CONFIG,usage(small)+'\n'+pps_usage(diagnostic),gnss)
assert new['paths']['production_pps_callback']['static_bytes']==5248
assert new['paths']['production_pps_snapshot']['static_bytes']==4192
for missing in diagnostic:
 try:measure(CONFIG,usage(small)+'\n'+pps_usage({k:v for k,v in diagnostic.items()if k!=missing}),gnss)
 except ValueError as e:assert 'Missing or ambiguous' in str(e)
 else:raise AssertionError('Missing PPS diagnostic frame accepted')
try:measure(CONFIG,usage(small)+'\n'+pps_usage({**diagnostic,'production_pps_score':4000}),gnss)
except ValueError as e:assert 'exceeds configured' in str(e)
else:raise AssertionError('Oversized PPS callback accepted')
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'target';p.mkdir();(p/'sdkconfig').write_text(CONFIG)
 try:check(d)
 except ValueError as e:assert 'One retained' in str(e)
 else:raise AssertionError('Missing compiler evidence accepted')
 (p/'native_main.cpp.su').write_text(usage(small));assert check(d)==r
 (p/'duplicate').mkdir();(p/'duplicate/native_main.cpp.su').write_text(usage(small))
 try:check(d)
 except ValueError as e:assert 'One retained' in str(e)
 else:raise AssertionError('Ambiguous compiler evidence accepted')
with tempfile.TemporaryDirectory() as d:
 root=Path(d);p=root/'target';p.mkdir();local=root/'local';local.mkdir()
 (p/'sdkconfig').write_text(CONFIG);(p/'native_main.cpp.su').write_text(usage(small)+'\n'+pps_usage(diagnostic))
 (root/'receipt.json').write_text(json.dumps({'version':'0.1.21'}))
 (local/'device.json').write_text(json.dumps({'gnss_profile':'meshnology_w12_l76k_rx39_pps41'}))
 try:check(d)
 except ValueError as e:assert 'One retained GNSS' in str(e)
 else:raise AssertionError('Missing GNSS compiler evidence accepted')
 (p/'gnss_pps.cpp.su').write_text(gnss);assert check(d)==new
 (local/'device.json').write_text('{}');assert check(d)==r
print('PASS observed 11216B status-path rejection, reserve, exact static evidence and retained-file checks')

# Version22 fast-map adds the real C helper path, retaining historical budgets.
fast='s3_clock_reference.c:1:1:'+FAST_MAP_DECLARATIONS['gnss_fast_map']+'\t64\tstatic'
with_fast=measure(CONFIG,usage(small)+'\n'+pps_usage(diagnostic),gnss,fast)
assert with_fast['paths']['gnss_software_map_bracket']['static_bytes']==2368
assert with_fast['uncounted_callee_reserve_bytes']==4096
for bad in ('',fast+'\n'+fast,fast.replace('static','dynamic,bounded')):
 try:measure(CONFIG,usage(small)+'\n'+pps_usage(diagnostic),gnss,bad)
 except ValueError as e:assert 'Missing or ambiguous' in str(e) or 'Unbounded' in str(e)
 else:raise AssertionError('Invalid fast-map static frame accepted')
try:measure(CONFIG,usage(small)+'\n'+pps_usage(diagnostic),gnss,fast.replace('\t64\t','\t6000\t'))
except ValueError as e:assert 'exceeds configured' in str(e)
else:raise AssertionError('Oversized fast-map path accepted')
with tempfile.TemporaryDirectory() as d:
 root=Path(d);p=root/'target';p.mkdir();local=root/'local';local.mkdir()
 (p/'sdkconfig').write_text(CONFIG);(p/'native_main.cpp.su').write_text(usage(small)+'\n'+pps_usage(diagnostic));(p/'gnss_pps.cpp.su').write_text(gnss)
 (root/'receipt.json').write_text(json.dumps({'version':'0.1.22','gnss_fast_map_diagnostic':True}));(local/'device.json').write_text(json.dumps({'gnss_profile':'meshnology_w12_l76k_rx39_pps41'}))
 try:check(d)
 except ValueError as e:assert 'One retained fast map' in str(e)
 else:raise AssertionError('Missing C helper evidence accepted')
 (p/'s3_clock_reference.c.su').write_text(fast);assert check(d)==with_fast
 (root/'receipt.json').write_text(json.dumps({'version':'0.1.22','gnss_fast_map_diagnostic':False}));assert check(d)==new
 (local/'device.json').write_text('{}');assert check(d)==r
 (root/'receipt.json').write_text(json.dumps({'version':'0.1.22','gnss_fast_map_diagnostic':True}))
 try:check(d)
 except ValueError as e:assert 'requires retained GNSS' in str(e)
 else:raise AssertionError('Fast map without GNSS evidence accepted')
print('PASS version22 optional C map-bracket path, disabled/historical budgets and unchanged4096B reserve')

# Current contracts select optional evidence by compiled profile, independently
# of firmware version. Old receipt semantics above remain reproducible.
with tempfile.TemporaryDirectory() as d:
 root=Path(d);p=root/'target';p.mkdir();local=root/'local';local.mkdir()
 (p/'sdkconfig').write_text(CONFIG);(p/'native_main.cpp.su').write_text(usage(small))
 for version in ('0.2.35','0.3.0'):
  current={'version':version,'stack_evidence_contract':'W12_PROFILE_STACK_V1'}
  (local/'device.json').write_text('{}')
  (root/'receipt.json').write_text(json.dumps(current));assert check(d)==r
  for option,reason in [('fine_clock','fine-owner'),('lora_rx','LR2021')]:
   (root/'receipt.json').write_text(json.dumps({**current,option:True}))
   try:check(d)
   except ValueError as e:assert reason in str(e)
   else:raise AssertionError('Current optional compiler evidence missing: '+option)
  (root/'receipt.json').write_text(json.dumps(current))
  (local/'device.json').write_text(json.dumps({'gnss_profile':'meshnology_w12_l76k_rx39_pps41'}))
  try:check(d)
  except ValueError as e:assert 'GNSS' in str(e)
  else:raise AssertionError('Current GNSS compiler evidence missing')
 (root/'receipt.json').write_text(json.dumps({'version':'0.2.35'}))
 try:check(d)
 except ValueError as e:assert 'profile-selected' in str(e)
 else:raise AssertionError('Current stack contract missing')
print('PASS current and future profile-selected GNSS/fine/radio evidence refusals')
