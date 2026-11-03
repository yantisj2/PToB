"""Independent prefix populations and integer clock projection.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import importlib.util
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'tools/holdover_analyze.py';s=importlib.util.spec_from_file_location('analyze',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def rows(bad=0,unknown=0):
 out=[]
 for i in range(100):
  age=1000 if i<unknown else 2000
  lane=[i+1,2000,0,32,30,1500 if i<bad else 100,0,0,0,age]
  out.append(dict(source='test',boot='one',op='holdover',payload=dict(policy='BLE_RATE_ONLY_T096_V1',resets=1,trials=[lane,[0]*10])))
 return out
assert m.analyze(rows())['groups'][0]['supported_ms']==2000
assert m.analyze(rows(bad=1))['groups'][0]['supported_ms']==2000
assert m.analyze(rows(bad=2))['groups'][0]['supported_ms']is None
assert m.analyze(rows(unknown=2))['groups'][0]['ages'][0]['unknown']==2
r=rows();r[0]['payload']['trials'][0][5]=1500;r.append(r[0]);assert m.analyze(r)['groups'][0]['ages'][0]['p99_ns']==100
assert m.project([16000000,1000000000,0,-100,1],24000000)==1499999950
assert m.project([16000000,1000000000,10,0,1],1616000000)==100999999000
print('PASS independent HOLD prefix P99, unknown denominators, scope and signed integer projection')
