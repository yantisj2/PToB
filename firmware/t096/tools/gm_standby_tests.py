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
started=time.monotonic();results=[];restart_results=[];stages=[];passed=False
try:
 with (out/'compile.log').open('x')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 fence_units=[x for x in units if x.name not in ('gm_standby_test.c','network.c')]+[app/'tests/gm_standby_fence_test.c']
 with (out/'fences.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+list(map(str,fence_units))+['-o',str(out/'fences')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 stages.append('compile_simulator_and_fences')
 with (out/'fences.run.log').open('x')as log:subprocess.run([str(out/'fences')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 stages.append('fences')
 retirement_units=[x for x in units if x.name not in ('gm_standby_test.c','network.c')]+[app/'tests/gm_standby_retirement_test.c']
 with (out/'retirement.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+list(map(str,retirement_units))+['-o',str(out/'retirement')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 with (out/'retirement.run.log').open('x')as log:subprocess.run([str(out/'retirement')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 stages.append('original_outstanding_grant_retirement')
 source_refresh_units=[x for x in units if x.name not in ('gm_standby_test.c','network.c')]+[app/'tests/gm_source_refresh_test.c']
 with (out/'source-refresh.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+list(map(str,source_refresh_units))+['-o',str(out/'source-refresh')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 with (out/'source-refresh.run.log').open('x')as log:subprocess.run([str(out/'source-refresh')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 stages.append('current_session_source_refresh_existing_map')
 recovery_units=[app/'tests/gm_standby_recovery_test.c',*units[1:]]
 with (out/'recovery.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+list(map(str,recovery_units))+['-o',str(out/'recovery')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 for gap,negative,management in [(v,0,1)for v in [6,12,20,30,50]]+[(v,0,0)for v in [12,20,30,50]]+[(20,1,0)]:
  with (out/f'recovery-{gap}-{negative}-{management}.log').open('x')as log:subprocess.run([str(out/'recovery'),str(gap),str(negative),str(management)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 stages.append('ten_actual_wire_management_loss_and_precision_recovery_cases')
 restart_units=[app/'tests/gm_standby_restart_test.c',*units[1:]]
 with (out/'restart.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+list(map(str,restart_units))+['-o',str(out/'restart')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 for role,delay in [(r,d)for r in [0,1]for d in [500,5000]]:
  with (out/f'restart-{role}-{delay}.log').open('x')as log:r=subprocess.run([str(out/'restart'),str(role),str(delay)],stdout=log,stderr=subprocess.STDOUT,timeout=60)
  restart_results.append(dict(reset_role='ACTIVE'if role else'HOT',disconnect_delay_ms=delay,returncode=r.returncode))
  if r.returncode:raise RuntimeError(f'failed {restart_results[-1]}')
 stages.append('four_partial_reboot_retained_remote_cache_and_COLD_replacement_schedules')
 owner_units=[x for x in units if x.name not in ('gm_standby_test.c','network.c')]+[app/'tests/gm_standby_owner_test.c']
 with (out/'owner.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+list(map(str,owner_units))+['-o',str(out/'owner')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 for period in [100000,125000,150000,175000,200000,250000,0]:
  with (out/f'owner-{period}.log').open('x')as log:subprocess.run([str(out/'owner'),str(period)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 stages.append('paired_owner_RX2_FIFO32_seven_delayed_and_explicit_burst_cases')
 for name in ['kernel.h','sys/atomic.h','random/random.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h']:
  h=out/'zephyr'/name;h.parent.mkdir(parents=True,exist_ok=True);h.write_text('/* Fake public controller boundary. */\n')
 transport_units=[app/'tests/gm_standby_transport_test.c',*[x for x in units if x.name not in ('gm_standby_test.c','radio_precision.c')]]
 with (out/'transport.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+['-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter',f'-I{out}',f'-I{app/"tests"}']+list(map(str,transport_units))+['-o',str(out/'transport')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 with (out/'transport.run.log').open('x')as log:subprocess.run([str(out/'transport')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 stages.append('native_admission_transport')
 role_reentry_units=[app/'tests/gm_role_reentry_test.c',*[x for x in units if x.name not in ('gm_standby_test.c','network.c','radio_precision.c')]]
 with (out/'role-reentry.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+['-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter',f'-I{out}',f'-I{app/"tests"}']+list(map(str,role_reentry_units))+['-o',str(out/'role-reentry')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 with (out/'role-reentry.run.log').open('x')as log:subprocess.run([str(out/'role-reentry')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 stages.append('former_ACTIVE_source_exit_native_JOIN_STATUS_new_GM_lower_generation')
 stages.append('five_guest_retirement_faults_and_actual_newboot_JOIN_STATUS_with_retained_cache')
 stages.append('native_COLD_incumbent_refresh_over120s_and_unavailable_incumbent_expiry')
 stages.append('native_COLD_50_200_900_1100ms_callbacks_and_50ms_JOIN_STATUS_owner_delays')
 stages.append('all_G5_role_callback_bounds_scan_stop_5s_aggregate_GATT_1s_STATUS_queue_600ms')
 stages.append('G5_signed_record_queue_600ms_original_expiry_replay_and_legacy_guards')
 with (out/'transport-diag.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+['-DR2_CONTROLLER_DIAGNOSTICS=1','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter',f'-I{out}',f'-I{app/"tests"}']+list(map(str,transport_units))+['-o',str(out/'transport-diag')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 with (out/'transport-diag.run.log').open('x')as log:subprocess.run([str(out/'transport-diag')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 stages.append('native_inventory_diagnostic_edges_and_normal_off')
 radio_units=[app/'tests/radio_service_test.c',*[app/'src'/x for x in ['radio_precision.c','relay_precision.c','core.c','clock.c','nmea.c','precision.c']],*[app/'protocol'/x for x in ['role_announcement.c','group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c']],*[tiny/'source'/x for x in ['hmac.c','sha256.c','utils.c']]]
 with (out/'handoff.compile.log').open('x')as log:subprocess.run(cmd[:cmd.index(str(units[0]))]+['-Wno-unused-parameter',f'-I{out}',f'-I{app/"tests"}']+list(map(str,radio_units))+['-o',str(out/'handoff')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 with (out/'handoff.producer.log').open('x')as log:subprocess.run([str(out/'transport'),str(out/'handoff.bin')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 for mode in range(35,50):
  with (out/f'handoff-{mode}.log').open('x')as log:subprocess.run([str(out/'handoff'),str(mode),str(out/'handoff.bin')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 stages.append('actual_producer_precision_mailbox_15_cases')
 cases=[(0,i,4)for i in range(1 if a.quick else 24)]+[(i,0,k)for i in range(1,18)for k in ([4]if a.quick else [3,4])]+([]if a.quick else [(0,i,3)for i in range(6)])
 for scenario,order,nodes in cases:
  with (out/f'{scenario}-{order}-{nodes}.log').open('x')as log:r=subprocess.run([str(out/'simulator'),str(scenario),str(order),str(nodes)],stdout=log,stderr=subprocess.STDOUT,timeout=60)
  results.append(dict(scenario=scenario,order=order,nodes=nodes,returncode=r.returncode));
  if r.returncode:raise RuntimeError(f'failed {results[-1]}')
 for phase in range(1,6):
  with (out/f'17-phase-{phase}.log').open('x')as log:r=subprocess.run([str(out/'simulator'),'17','0','3',str(phase)],stdout=log,stderr=subprocess.STDOUT,timeout=60)
  results.append(dict(scenario=17,order=0,nodes=3,failure_phase=phase,returncode=r.returncode))
  if r.returncode:raise RuntimeError(f'failed {results[-1]}')
 passed=True
finally:
 unchanged=frozen==source_map()and all(hashlib.sha256((app.parents[1]/x).read_bytes()).hexdigest()==digest for x,digest in supporting.items())
 sources=set(units+[app/'tests/gm_standby_owner_test.c',app/'tests/gm_source_refresh_test.c',app/'tests/gm_role_reentry_test.c',app/'tests/gm_standby_retirement_test.c',app/'tests/gm_standby_fence_test.c',app/'tests/gm_standby_transport_test.c',app/'src/group_admission.c',app/'protocol/ble_transport.c',app/'protocol/gm_standby.inc',app/'protocol/gm_standby.h',app/'protocol/network.h'])
 (out/'receipt.json').write_text(json.dumps(dict(passed=passed and unchanged,source_unchanged=unchanged,source_sha256=source_sha,source_files=frozen,supporting_files=supporting,elapsed_s=time.monotonic()-started,hardware=False,production_control_slots=2,stages_completed=stages,expected_cases=len(cases)+5 if'cases'in globals()else None,results=results,restart_results=restart_results,mutated_state_limits="Fake public controller callbacks; no HCI/deferred-L2CAP scheduler or physical cause proof",command=cmd,sources={str(x):hashlib.sha256(x.read_bytes()).hexdigest()for x in sources}),indent=2)+'\n')
if not unchanged:raise RuntimeError('T096 source changed during run; receipt not accepted')
print('PASS G5 production owner, causal JOIN and physical-tuple precision; no hardware')
