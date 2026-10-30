#!/usr/bin/env python3
"""Shadow estimator and exact OLED framebuffer checks. Offline only.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import os
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
A=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(A.parents[1]/'registry'));from registry import load,header
    (out/'ptob_registry.h').write_text(header(load(A.parents[1]/'registry/example.json')))
    flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DPTOB_W12_SHADOW_HOLDOVER=1','-DPTOB_W12_HOLDOVER_PRODUCTION=1','-DPTOB_W12_HOLDOVER_HORIZON=1','-DPTOB_W12_HOLDOVER_RATE=1','-DPTOB_W12_THERMAL_SHADOW_ONLY=1','-DPTOB_W12_FINE_CLOCK=1','-I'+str(out)]
    for path in (A/'main',A/'legacy',A/'main/r2/r2-g1',A/'main/r2/rc7/include'):flags+=['-I'+str(path)]
    for name in ('shadow_holdover','shadow_display','production_holdover','holdover_rate','holdover_horizon'):
        cmd=[os.environ.get('CXX','clang++'),*flags,A/'tests'/(name+'_test.cpp'),'-o',out/name]
        with (out/(name+'-build.log')).open('w') as log:subprocess.run(list(map(str,cmd)),stdout=log,stderr=subprocess.STDOUT,check=True)
        with (out/(name+'.log')).open('w') as log:subprocess.run([out/name,out],stdout=log,stderr=subprocess.STDOUT,check=True)
    from holdover_horizon_analyze import analyze as horizon_analyze
    hh=(out/'horizon-synthetic.log').read_bytes();hr=horizon_analyze(hh)
    assert hr['complete'] and hr['latest']['duration_ms']>=24000 and hr['latest']['lower_bound']
    lines=hh.splitlines(keepends=True)
    at=next(i for i,s in enumerate(lines) if b'W12_HORIZON_STATUS' in s)
    later=next(i for i in range(at+1,len(lines)) if b'W12_HOLDOVER_PAIR' in lines[i])
    delayed=lines.pop(at);lines.insert(later,delayed)
    assert horizon_analyze(b''.join(lines))['complete'] # Status copy can precede later printed pair records.
    try:horizon_analyze(hh.replace(b'temperature=0 production=0',b'temperature=1 production=0',1))
    except AssertionError:pass
    else:raise AssertionError('Temperature mixed into the duration population')
    from shadow_holdover_analyze import analyze
    assert analyze(b'')['populations']==[]
    for field in ('error_ns','baseline_error_ns'):
        try:analyze(('W12_SHADOW boot=1 seq=1 population=1 production=0 horizon_ms=1000 start20=1 local20=20000001 reference_ns=100 predicted_ns=100 baseline_ns=100 error_ns=0 baseline_error_ns=0 '+field+'=7\n').encode())
        except AssertionError:pass
        else:raise AssertionError('Corrupt arithmetic accepted')
    lines=[];values=[]
    from shadow_holdover_analyze import tails,UNKNOWN
    for i in range(140):
        error=9000000000 if i==138 else (-1 if i%2 else 1)*(i+1);values.append(error)
        window=values[-128:];q=tails(window)
        row=dict(boot=1,seq=i+1,population=1,production=0,horizon_ms=1000,start20=1000000000+60000000*i,
            reference_ns=1000000000000,predicted_ns=1000000000000+error,baseline_ns=1000000000000+error,
            error_ns=error,baseline_error_ns=error,n=len(window),p95_ns=q['p95_ns'] if len(window)>=100 else UNKNOWN,
            p99_ns=q['p99_ns'] if len(window)>=100 else UNKNOWN,max_ns=q['max_ns'],completed=i+1,missed=0,aborted=0)
        row['local20']=row['start20']+20000000
        lines.append('W12_SHADOW '+' '.join(str(k)+'='+str(v) for k,v in row.items()))
    result=analyze(('\n'.join(lines)+'\n').encode());assert result['populations'][0]['rolling_reproduced']
    assert result['populations'][0]['observed']['max_ns']==9000000000
    lines=[];values=[]
    for i in range(1,961):
        error=i*10//8;values.append(error);q=tails(values[-128:])
        row=dict(boot=2,seq=i,population=1,production=0,horizon_ms=120000,trial=1,
            start20=40000000,local20=40000000+i*2500000,age20=i*2500000,gaps=0,max_gap20=2500000,
            safe20=min(i,799)*2500000,cross20=2000000000 if i>=800 else 0,
            base_cross20=2000000000 if i>=800 else 0,run_max_ns=error,base_run_max_ns=error,
            reference_ns=1000000000000,predicted_ns=1000000000000+error,baseline_ns=1000000000000+error,
            error_ns=error,baseline_error_ns=error,n=min(i,128),
            p95_ns=q['p95_ns'] if i>=100 else UNKNOWN,p99_ns=q['p99_ns'] if i>=100 else UNKNOWN,
            max_ns=q['max_ns'],base_p99_ns=q['p99_ns'] if i>=100 else UNKNOWN,completed=0,missed=0,aborted=0)
        lines.append('W12_SHADOW '+' '.join(str(k)+'='+str(v) for k,v in row.items()))
    result=analyze(('\n'.join(lines)+'\n').encode())['populations'][0]
    assert result['rolling_reproduced'] and result['first_crossing_seconds']==100
    assert result['sampled_safe_seconds']==99.875 and result['cap_reached'] and result['from_start']
    assert result['age_bands'][-1]['shadow']['max_ns']==1200
    assert result['coverage_gaps']==0 and result['maximum_gap_seconds']==.125
    partial=analyze(('\n'.join(lines[500:])+'\n').encode())['populations'][0]
    assert partial['rolling_reproduced'] and not partial['from_start']
    with (out/'production-analysis.log').open('w') as log:
        subprocess.run([sys.executable,A/'tests/holdover_production_analyze_test.py'],stdout=log,stderr=subprocess.STDOUT,check=True)
    paths=[A/'tools/holdover_horizon_analyze.py',A/'main/w12_holdover_horizon.hpp',A/'tests/holdover_horizon_test.cpp',A/'main/w12_holdover_rate.hpp',A/'tests/holdover_rate_test.cpp',A/'tools/holdover_rate_replay.py',A/'main/w12_shadow_holdover.hpp',A/'main/w12_thermal_projection.hpp',A/'main/g1_clock_prediction.hpp',A/'tests/production_holdover_test.cpp',A/'tests/holdover_production_analyze_test.py',A/'tools/holdover_production_analyze.py',A/'main/g1_corrected_clock.hpp',A/'main/native_g1.cpp',A/'main/native_main.cpp',A/'main/native_dashboard.hpp',A/'legacy/display.cpp',A/'legacy/display.hpp',A/'tests/shadow_holdover_test.cpp',A/'tests/shadow_display_test.cpp',Path(__file__),A/'tools/shadow_holdover_analyze.py']
    (out/'receipt.json').write_text(json.dumps(dict(status='PASS',hardware_access=False,sources={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}),indent=2)+'\n')
    print('PASS shadow holdoff/thermal/quantiles/tails and actual OLED pixels under ASan/UBSan')
if __name__=='__main__':main()
