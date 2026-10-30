# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from holdover_production_analyze import analyze
from shadow_holdover_analyze import tails,UNKNOWN
from framed_console import mutation_command
frame={'begin':dict(nonce='0000000000000001',ticket='0000000000000002',factory='44:b1:76:ad:06:68',session='0000000000000003')}
for action in ('holdover test 0','holdover test 1000','holdover test 120000'):
    command=mutation_command(frame,'0000000000000004',action)
    assert command==('v12 do 0000000000000002 0000000000000004 44:b1:76:ad:06:68 0000000000000003 '+action+'\n').encode()
for action in ('holdover test 999','holdover test 120001','holdover test -1','holdover test 01000','holdover test 120000\noff'):
    try:mutation_command(frame,'0000000000000004',action)
    except RuntimeError:pass
    else:raise AssertionError('Unbounded/malformed HOLD command accepted')
start=40000000;until=start+400000000;epoch=1000000000000;utc=1700000000000000000;receiver=7671
lines=[f'W12_HOLDOVER_EVENT event=BEGIN test=1 start20={start} until20={until} duration_ms=20000 production=1 model=1 receiver_ns={receiver} beta_mppb_c=0 clock_tick20={start} epoch_ns={epoch} phase_ns=0 rate=0 beta=0 measured=25 lagged=25 utc_offset_ns={utc}']
values=[]
for i in range(1,160):
    tick=start+i*2500000;base=epoch+i*125000000;error=i;values.append(error);q=tails(values[-128:])
    row=dict(boot=1,seq=i,start20=start,local20=tick,reference_ns=base-error,predicted_ns=base,baseline_ns=base,error_ns=error,baseline_error_ns=error,
        output_ns=base+utc+receiver,utc_offset_ns=utc,receiver_ns=receiver,output_correction_ns=0,production=1,model=1,n=min(i,128),
        p95_ns=q['p95_ns'] if i>=100 else UNKNOWN,p99_ns=q['p99_ns'] if i>=100 else UNKNOWN,max_ns=q['max_ns'],
        gaps=0,max_gap20=2500000,safe20=tick-start,cross20=0,base_cross20=0)
    lines.append('W12_HOLDOVER '+' '.join(str(k)+'='+str(v) for k,v in row.items()))
    if i%80==0:lines.append(f'W12_HOLDOVER_STATUS test=1 model=1 selected=1 test_active=1 start20={start} until20={until} sample20={tick} output_ns={base+utc+receiver} delivered_us={(base+utc+receiver)//1000} following=0 production=1')
lines.append(f'W12_HOLDOVER_EVENT event=END test=1 start20={start} until20={until} at20={until} reason=DEADLINE complete=1 production=1 model=1')
lines.append('W12_HOLDOVER_RESUME test=1 sample20=440000000 old_output_ns=1700000000000000000 new_output_ns=1700000000000000000 step_ns=0 applied=1 phase_ns=0 production=1 model=1')
raw=('\n'.join(lines)+'\n').encode();r=analyze(raw)
assert r['complete'] and r['trials'][0]['activation_verified'] and r['trials'][0]['observed']['max_ns']==159
assert r['trials'][0]['replay_max_difference_ns']==0 and r['trials'][0]['resume_zero_step']
assert not analyze(raw.replace(b'step_ns=0 applied=1',b'step_ns=7 applied=1'))['complete']
try:analyze(raw.replace(b'predicted_ns=1000125000000',b'predicted_ns=1000125000010'))
except AssertionError:pass
else:raise AssertionError('Corrupt selected output accepted')
print('PASS production activation certificate, independent input replay, exact tails, actual delivery API and nonzero resume-step rejection')
