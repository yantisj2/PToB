#!/usr/bin/env python3
"""MIT. Copyright(c)2026 Jonathan Yantis. Independent causal analytic clocks."""
import importlib.util,copy
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'tools/lora_holdover_analyze.py';s=importlib.util.spec_from_file_location('analysis',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def row(n):
 tick=(1+n*30)*16000000
 return dict(profile='RAM_30_120_300',transmitter=False,boot='1',emitter='2',emitter_boot='3',stream=1,source_epoch=1,capture_epoch=1,
             accepted=n+1,event_tick=tick,event_ns=10**12+tick*125//2+n*3000,hold_rates=[0,100,0,1,0,1],hold_selected=True,trained=True,link_fit=[11,7])
rows=[row(n)for n in range(11)];r=m.analyze(rows)['groups'][0]
assert r['frozen_comparisons'][0]['baseline']['rms_ns']==3000
assert r['frozen_comparisons'][0]['hold']['rms_ns']==0
assert r['frozen_comparisons'][3]['baseline']['rms_ns']==12000
assert r['frozen_comparisons'][3]['hold']['rms_ns']==0
assert r['frozen_comparisons'][3]['censored']==4
assert m.analyze(rows+rows[-1:])['groups'][0]==r
missing=m.analyze(rows[:4]+rows[5:])['groups'][0]
assert missing['unobserved_accepted_events']==1 and missing['frozen_comparisons'][0]['unknown']==1
bad=copy.deepcopy(rows);bad[4]['event_ns']+=10000
assert m.analyze(bad)['groups'][0]['frozen_comparisons'][0]['hold']['max_abs_ns']==10000
bad=copy.deepcopy(rows);bad[4]['event_tick']=bad[3]['event_tick']
try:m.analyze(bad);raise AssertionError('mutated event accepted')
except ValueError:pass
other=copy.deepcopy(rows);other[0]['stream']=2
assert len(m.analyze(other)['groups'])==2
print('PASS causal frozen LoRa RMS, rate sign, untrimmed tails, missing/censored denominators and context isolation')
