#!/usr/bin/env python3
"""Declared-noise tests of the actual C recursive clock, no hardware/PPS data.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
These populations expose noise/wander/bias sensitivity, not UTC qualification.
"""
import argparse,json,math,random,subprocess
from pathlib import Path

def stats(values):
 return dict(samples=len(values),rms_ns=math.sqrt(sum(x*x for x in values)/len(values)),
             mean_ns=sum(values)/len(values),max_abs_ns=max(map(abs,values)))
def rounded(x):return int(x+.5)if x>=0 else int(x-.5)
def project(model,second):
 at,ns,rate,phase=model;age=second-at
 return ns+age*1000000000+int(age*rate)+rounded(phase*min(1.,age))
def run(binary,seed,wander,noise,bias,drop=False,ramp=0.,drift_state=False):
 rng=random.Random(seed);phase=0.;rate=100.;origin=1793724000000000000
 truth=[];points=[];events=[]
 for sec in range(3600):
  truth.append(origin+sec*1000000000+rounded(phase))
  if sec%30==0 and not(drop and sec%300 in(90,120)):
   ns=truth[-1]+rounded(rng.gauss(0,noise))+bias;points.append((sec*16000000,ns));events.append(sec)
  rate+=ramp+rng.gauss(0,math.sqrt(wander));phase+=rate
 rows=''.join(f'{tick} {ns}\n'for tick,ns in points)
 result=subprocess.run([str(binary)],input=rows,capture_output=True,text=True);result.check_returncode()
 states=[tuple(map(float,line.split()))for line in result.stdout.splitlines()];assert len(states)==len(points)
 # Actual FOLLOW availability is three seconds after original SYNC; one-second
 # continuous slew is the firmware integration tested separately by core tests.
 commits={sec+3:(points[i][1],states[i])for i,sec in enumerate(events)}
 model=None;errors=[];raw=[];ages=[[]for _ in range(30)];last_event=None
 for sec in range(3600):
  if sec in commits:
   ns,state=commits[sec];phase,rate,*cov=state;drift=cov[0]if drift_state else 0.
   target=ns+3*1000000000+rounded(phase+3*rate+4.5*drift)
   rate=rounded(rate+3*drift)
   previous=project(model,sec)if model else target
   model=(sec,previous,rate,target-previous);last_event=sec-3
   raw_model=(sec,ns+3*1000000000,0,0)
  if sec>=2100:
   assert model is not None
   error=project(model,sec)-truth[sec];errors.append(error);raw.append(project(raw_model,sec)-truth[sec])
   # All0..29s slot phases count, including the first three seconds that
   # necessarily still use the prior model before FOLLOW is available.
   ages[sec%30].append(error)
 return errors,raw,ages
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--drift-state',action='store_true');a=p.parse_args()
 populations=[]
 for name,wander,noise,bias,drop in [('constant',0.,4200,0,False),('matched_wander',.01,4200,0,False),
  ('larger_wander',1.,4200,0,False),('fixed_endpoint_bias',0.,4200,10000,False),('lost_pairs',.01,4200,0,True),
  ('frequency_ramp',0.,4200,0,False)]:
  all_errors=[];all_raw=[];all_ages=[[]for _ in range(30)];runs=[]
  for seed in range(100):
   ramp=.1 if name=='frequency_ramp'else 0.
   errors,raw,ages=run(a.binary,seed,wander,noise,bias,drop,ramp,a.drift_state);all_errors.extend(errors);all_raw.extend(raw);runs.append(stats(errors))
   for age,values in enumerate(ages):all_ages[age].extend(values)
  q=dict(name=name,seeds=100,assumed_actual_frequency_rw_ppb2_per_s=wander,assumed_measurement_sd_ns=noise,
   fixed_bias_ns=bias,frequency_ramp_ppb_per_s=ramp,missing_pair_pattern='2 per300s'if drop else'none',clock=stats(all_errors),
   raw_nominal_anchor=stats(all_raw),worst_run_rms_ns=max(r['rms_ns']for r in runs),
   age_bins=[dict(precision_interval_age_s=age,**stats(values))for age,values in enumerate(all_ages)],
   passes_aggregate_1000ns=stats(all_errors)['rms_ns']<1000)
  populations.append(q)
 assert populations[0]['clock']['rms_ns']<populations[0]['raw_nominal_anchor']['rms_ns']
 assert populations[3]['clock']['rms_ns']>9000,'A one-way filter must not infer/remove constant endpoint bias'
 report=dict(status='PASS_DECLARED_MODEL_SENSITIVITY_NOT_HARDWARE_QUALIFICATION',hardware=False,
  filter_measurement_sd_ns=4200,filter_frequency_rw_ppb2_per_s=.01,drift_state=a.drift_state,filter_drift_rw_ppb2_per_s3=1e-8 if a.drift_state else None,period_s=30,follow_commit_delay_s=3,
  phase_slew_s=1,scored_s=[2100,3600],all_tails_retained=True,independent_reference=False,populations=populations)
 a.out.write_text(json.dumps(report,indent=2)+'\n')
 print('PASS actual C servo,600 declared-noise trials; aggregate RMS ns:',[(r['name'],round(r['clock']['rms_ns'],1))for r in populations])
if __name__=='__main__':main()
