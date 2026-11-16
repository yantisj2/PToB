# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'tools/imu_report.py';spec=importlib.util.spec_from_file_location('report',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
keys='samples glance_events tilt_shadow_events wakes lowered arms disarms ignored_awake ignored_disarmed ignored_refractory stale_polls overflows sensor_errors resets errors bus_errors budget_errors'.split()
def frame(at,wakes=0):
 r={k:'0' for k in keys};r.update(samples=str(int(at*12)),wakes=str(wakes),ready='1',asleep='1',sample_age_ms='40',max_poll_gap_us='80000',max_service_us='1000',orientation_ok='1',xyz='0/0/1024')
 return {'capture_elapsed_s':at,'begin':{'session':'one'},'rows':{'TWATCH_IMU':r}}
f=[frame(i/2,int(i>=5)) for i in range(10)]
labels=[dict(start_s='1',end_s='3',kind='raise',label='raise'),dict(start_s='1',end_s='2',kind='pose',label='face-up')]
r,_=m.analyze(f,labels);assert r['labels'][0]['result']=='DETECTED' and r['labels'][1]['unit_vector']==[0,0,1]
r,_=m.analyze(f[:3]+f[8:],labels);assert r['labels'][0]['result']=='UNKNOWN'
f[5]['rows']['TWATCH_IMU']['sample_age_ms']='600';r,_=m.analyze(f,labels);assert r['labels'][0]['result']=='UNKNOWN'
f[-1]['begin']['session']='two'
try:m.analyze(f);raise AssertionError('mixed boots accepted')
except ValueError:pass
print('PASS offline IMU report: labeled detection, pose axes, missing/stale coverage and mixed-boot refusal')
