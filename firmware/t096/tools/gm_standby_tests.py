#!/usr/bin/env python3
"""G5 normal-owner regressions: real causal JOIN, network and precision fitter.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--quick',action='store_true');a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
units=[app/'tests/gm_standby_test.c',*[app/'protocol'/x for x in ['network.c','election_policy.c','gm_diagnostics.c','group_join.c','group_auth.c','group_control.c','authority_codec.c','precision_codec.c','relay_codec.c']],app/'src/radio_precision.c',*[tiny/'source'/x for x in ['hmac.c','sha256.c','utils.c']]]
cmd=[os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_GM5_NATIVE=1',*[f'-I{x}'for x in [app/'include',app/'protocol',tiny/'include']],*map(str,units),'-o',str(out/'simulator')]
def source_map():
 return {str(x.relative_to(app)):hashlib.sha256(x.read_bytes()).hexdigest()for x in sorted(app.rglob('*'))if x.is_file()and'__pycache__'not in x.parts and x.suffix!='.pyc'}
frozen=source_map();source_sha=hashlib.sha256(json.dumps(frozen,sort_keys=True,separators=(',',':')).encode()).hexdigest()
supporting={str(x.relative_to(app.parents[1])):hashlib.sha256(x.read_bytes()).hexdigest()for x in [app.parents[1]/"docs/t096"/name for name in ["build.md","diagnostics.md","gm-standby.md"]]}
started=time.monotonic();results=[];stages=[];passed=False
try:
 with (out/'compile.log').open('x')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 fence_units=[x for x in units if x.name not in ('gm_standby_test.c','network.c')]+[app/'tests/gm_standby_fence_test.c']
 with (out/'fences.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+list(map(str,fence_units))+['-o',str(out/'fences')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 stages.append('compile_simulator_and_fences')
 with (out/'fences.run.log').open('x')as log:subprocess.run([str(out/'fences')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 stages.append('fences')
 for name in ['kernel.h','sys/atomic.h','random/random.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h']:
  h=out/'zephyr'/name;h.parent.mkdir(parents=True,exist_ok=True);h.write_text('/* Fake public controller boundary. */\n')
 transport_units=[app/'tests/gm_standby_transport_test.c',*[x for x in units if x.name not in ('gm_standby_test.c','radio_precision.c')]]
 with (out/'transport.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+['-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter',f'-I{out}',f'-I{app/"tests"}']+list(map(str,transport_units))+['-o',str(out/'transport')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 with (out/'transport.run.log').open('x')as log:subprocess.run([str(out/'transport')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 stages.append('native_admission_transport')
 radio_units=[app/'tests/radio_service_test.c',*[app/'src'/x for x in ['radio_precision.c','relay_precision.c','core.c','clock.c','nmea.c','precision.c']],*[app/'protocol'/x for x in ['role_announcement.c','group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c']],*[tiny/'source'/x for x in ['hmac.c','sha256.c','utils.c']]]
 with (out/'handoff.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+['-Wno-unused-parameter',f'-I{out}',f'-I{app/"tests"}']+list(map(str,radio_units))+['-o',str(out/'handoff')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 with (out/'handoff.producer.log').open('x')as log:subprocess.run([str(out/'transport'),str(out/'handoff.bin')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 for mode in range(35,50):
  with (out/f'handoff-{mode}.log').open('x')as log:subprocess.run([str(out/'handoff'),str(mode),str(out/'handoff.bin')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 stages.append('actual_producer_precision_mailbox_15_cases')
 cases=[(0,i,4)for i in range(1 if a.quick else 24)]+[(i,0,k)for i in range(1,15)for k in ([4]if a.quick else [3,4])]+([]if a.quick else [(0,i,3)for i in range(6)])
 for scenario,order,nodes in cases:
  with (out/f'{scenario}-{order}-{nodes}.log').open('x')as log:r=subprocess.run([str(out/'simulator'),str(scenario),str(order),str(nodes)],stdout=log,stderr=subprocess.STDOUT,timeout=60)
  results.append(dict(scenario=scenario,order=order,nodes=nodes,returncode=r.returncode));
  if r.returncode:raise RuntimeError(f'failed {results[-1]}')
 passed=True
finally:
 unchanged=frozen==source_map()and all(hashlib.sha256((app.parents[1]/x).read_bytes()).hexdigest()==digest for x,digest in supporting.items())
 sources=set(units+[app/'tests/gm_standby_fence_test.c',app/'tests/gm_standby_transport_test.c',app/'src/group_admission.c',app/'protocol/ble_transport.c',app/'protocol/gm_standby.inc',app/'protocol/gm_standby.h',app/'protocol/network.h'])
 (out/'receipt.json').write_text(json.dumps(dict(passed=passed and unchanged,source_unchanged=unchanged,source_sha256=source_sha,source_files=frozen,supporting_files=supporting,elapsed_s=time.monotonic()-started,hardware=False,production_control_slots=2,stages_completed=stages,expected_cases=len(cases)if'cases'in globals()else None,results=results,command=cmd,sources={str(x):hashlib.sha256(x.read_bytes()).hexdigest()for x in sources}),indent=2)+'\n')
if not unchanged:raise RuntimeError('T096 source changed during run; receipt not accepted')
print('PASS G5 production owner, causal JOIN and physical-tuple precision; no hardware')
