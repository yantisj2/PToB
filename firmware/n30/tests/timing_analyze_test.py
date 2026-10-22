#!/usr/bin/env python3
"""Conditional quantization model, causal replay and CRC corruption controls.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import importlib.util,math,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1];sys.path.insert(0,str(APP/'tools'))
import timing_analyze as a

def row(i,rate=500,phase=0,diverse=True):
 remote=1000000000+i*62500000+(i*137%397)*125*diverse
 true=remote/(1+rate*1e-9)+phase;tick=(math.floor(true/500)*10+5)
 return dict(n=i+1,local20=tick,corrected20=tick+1250000,remote_ns=remote,enclosure_ns=0,created_ns=remote-1000,expires_ns=remote+1500000000)

fit=a.IntervalFit(0)
for i in range(64):fit.add(row(i))
tick=fit.points[-1]['corrected20']+1250000;p=fit.predict(tick)
truth=tick*50*(1+500e-9)
assert p['feasible'] and abs(p['prediction_ns']-truth)<=p['conditional_half_width_ns']+1e-4
assert p['conditional_half_width_ns']<150 and p['UTC_bound']=='UNKNOWN'
assert fit.predict(fit.points[-1]['corrected20']+30000000) is None
coherent=a.IntervalFit(0)
for i in range(64):coherent.add(row(i,0,200,False))
q=coherent.predict(coherent.points[-1]['corrected20'])
assert q['feasible'] and q['conditional_half_width_ns']>=250 and not q['conditional_sub_tick']
coherent.points[31]=dict(coherent.points[31],remote_ns=coherent.points[31]['remote_ns']+10000)
assert coherent.predict(coherent.points[-1]['corrected20'])['feasible'] is False
wide=a.IntervalFit(0)
for i in range(64):wide.add(dict(row(i),enclosure_ns=1000))
assert wide.predict(wide.points[-1]['corrected20'])['conditional_half_width_ns']>1000
accelerating=a.IntervalFit(100)
for i in range(64):
 r=row(i);x=(r['local20']-row(0)['local20'])/20000000
 accelerating.add(dict(r,remote_ns=round(r['remote_ns']+.5*100*x*x)))
tick=accelerating.points[-1]['corrected20']+1250000
p=accelerating.predict(tick);x=(tick-row(0)['local20'])/20000000
truth=tick*50*(1+500e-9)+.5*100*x*x
assert p['feasible'] and abs(p['prediction_ns']-truth)<=p['conditional_half_width_ns']+1
for bad in (-1,float('inf'),float('nan')):
 try:a.IntervalFit(bad)
 except ValueError:pass
 else:raise AssertionError('Invalid acceleration assumption accepted')

# Use the actual strict ten-row parser fixtures, including intentional bad
# frames whose CRCs were recomputed, rather than trusting saved JSON blindly.
spec=importlib.util.spec_from_file_location('timing_console_fixtures',APP/'tests/console_test.py')
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
raw=f.timing_frame();frames,errors=a.decode(raw,f.tm);assert len(frames)==1 and not errors
report=a.analyze(frames);assert report['causal_innovation']['n']==1 and report['causal_innovation']['rms_ns']==50
assert report['assumptions']['centered_descriptor_bins'] is False and not report['shadow']
assert a.analyze(frames+frames)['unique_records_received']==1
other,_=a.decode(f.timing_frame((1,'predicted_ns','999999900')),f.tm)
conflict=a.analyze(frames+other+frames)
assert conflict['record_conflicts'] and conflict['unique_records_received']==0 and conflict['causal_innovation']['n']==0
assert a.decode(raw.replace(b'fine=584',b'fine=583'),f.tm)[1]
assert a.decode(raw[:raw.index(b'W12_V12_END')],f.tm)[1]
print('PASS interval shadow: known synthetic truth inside conditional polygon, coherent phase remains unidentified, wide mapping stays wide, stale/inconsistent refusal, CRC corruption/missing END, no deduplication bias')
