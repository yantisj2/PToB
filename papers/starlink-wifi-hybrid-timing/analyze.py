#!/usr/bin/env python3
"""Recompute selected derived observations; no hardware or private logs required.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import csv,json,math,statistics
from pathlib import Path

def stats(v):
 if not v:return {'n':0}
 a=sorted(abs(x) for x in v);mean=math.fsum(v)/len(v);med=statistics.median(v)
 return {'n':len(v),'mean_us':mean,'rms_us':math.sqrt(math.fsum(x*x for x in v)/len(v)),
  'centered_std_us':math.sqrt(math.fsum((x-mean)**2 for x in v)/len(v)),
  'abs_p99_us':a[math.ceil(.99*len(v))-1],'max_abs_us':max(a),
  'MAD_sigma_us':1.4826*statistics.median(abs(x-med) for x in v)}

def fit(rows,weighted=False):
 x0=rows[0]['x'];y0=rows[0]['y'];xs=[(r['x']-x0)/1e6 for r in rows];ys=[r['y']-y0 for r in rows]
 ws=[1/r['delay']**2 if weighted else 1 for r in rows];sw=math.fsum(ws)
 mx=math.fsum(w*x for w,x in zip(ws,xs))/sw;my=math.fsum(w*y for w,y in zip(ws,ys))/sw
 rate=math.fsum(w*(x-mx)*(y-my) for x,y,w in zip(xs,ys,ws))/math.fsum(w*(x-mx)**2 for x,w in zip(xs,ws))
 return {'x0':x0,'y0':y0,'a':my-rate*mx,'ppm':rate}
def prediction(m,x):return m['y0']+m['a']+m['ppm']*(x-m['x0'])/1e6

def gated_fit(minima):
 kept=[];rejected=[];resets=[];consecutive=0;innovations=[]
 for r in minima:
  if len(kept)>=8 and r['x']-kept[0]['x']>=60000000:
   prior=fit(kept);residuals=[q['y']-prediction(prior,q['x']) for q in kept]
   sigma=stats(residuals)['MAD_sigma_us'];innovation=r['y']-prediction(prior,r['x'])
   threshold=max(50.,6*sigma)+.5*max(0,r['delay']-min(q['delay'] for q in kept))
   innovations.append(innovation)
   if abs(innovation)>threshold:
    rejected.append({'sample':r['sample'],'innovation_us':innovation,'gate_us':threshold});consecutive+=1
    if consecutive>=3:resets.append(r['sample']);kept=[];consecutive=0
    continue
  consecutive=0;kept.append(r)
 return (fit(kept,True) if len(kept)>=8 else None),kept,rejected,resets,innovations

def synthetic_checks():
 baseline=[{'x':i*16000000,'y':1.7*i*16,'delay':2500,'sample':i} for i in range(20)]
 spike=[dict(r) for r in baseline];spike[10]['y']+=2000
 m,kept,rejected,resets,_=gated_fit(spike)
 assert [r['sample'] for r in rejected]==[10] and not resets and abs(m['ppm']-1.7)<1e-10
 step=[dict(r) for r in baseline[:13]]
 for r in step[10:]:r['y']+=2000
 m,kept,rejected,resets,_=gated_fit(step)
 assert [r['sample'] for r in rejected]==[10,11,12] and resets==[12] and m is None
 return 'PASS: synthetic isolated spike rejected; three-step sequence withdraws model. Not hardware evidence.'

def analyze(folder):
 meta=json.loads((folder/'observations.json').read_text());cut=300000000
 exchanges=[]
 for r in csv.DictReader((folder/'ntp.csv').open()):
  if r['accepted']=='1':exchanges.append({'x':int(r['mid_us']),'send':int(r['send_us']),'receive':int(r['receive_us']),
   'y':int(r['offset_us']),'delay':int(r['delay_us']),'sample':int(r['sample']),'fit_eligible':r.get('training_eligible','1')=='1'})
 training=[r for r in exchanges if r['fit_eligible'] and r['send']>=meta.get('training_start_us',0) and r['receive']<cut]
 groups=[];group=[]
 for r in training:
  if group and r['send']-group[-1]['send']>4000000:groups.append(group);group=[]
  group.append(r)
 if group:groups.append(group)
 minima=[min(g,key=lambda r:(r['delay'],-r['sample'])) for g in groups if len(g)==8]
 model,kept,rejected,resets,innovations=gated_fit(minima);assert model is not None
 phases=[];rows=list(csv.DictReader((folder/'pps.csv').open()))
 for r in rows:
  if r['valid']!='1':continue
  x=int(r['local_us']);fraction=float(r['local_fraction_us']);d=prediction(model,x+fraction)
  phase=((x+meta['local_utc_phase_origin_us']+math.floor(d)+500000)%1000000-500000)+(d-math.floor(d))+fraction
  phases.append((r,x,phase))
 calibration=[(r,x,v) for r,x,v in phases if x<cut][-16:];assert len(calibration)==16
 base=math.fsum(v for r,x,v in calibration)/16
 test=[(r,x,v-base) for r,x,v in phases if cut<=x<600000000]
 errors=[v for r,x,v in test]
 filter_base=math.fsum(float(r['filtered_phase_us']) for r,x,v in calibration)/len(calibration)
 filter_future=[float(r['filtered_phase_us']) for r,x,v in test]
 jitter=lambda rs:stats([r['y']-prediction(model,r['x']) for r in rs])
 result={'model':model,'fixed_base_us':base,'calibration_PPS':[int(r['number']) for r,x,v in calibration],
  'counts':{'PPS_total':len(rows),'PPS_valid':len(phases),'PPS_invalid':len(rows)-len(phases),
   'training_exchange_n':len(training),'complete_bursts':len(minima),'omitted_burst_lengths':[len(g) for g in groups if len(g)!=8],
   'admitted_minima':len(kept),'gate_rejections':len(rejected),'source_shift_resets':len(resets),
   'validation_PPS_total':sum(r['partition']=='validation' for r in rows),'validation_PPS_valid':len(test)},
  'gate_rejections':rejected,'source_shift_resets':resets,'prior_innovations':stats(innovations),
  'observed_source_plus_transfer_residual':{'all_training_exchanges':jitter(training),'training_burst_minima':jitter(minima),
   'heldout_exchanges':jitter([r for r in exchanges if r['send']>=cut])},
  'all_qualified_PPS':{'raw':stats([float(r['raw_phase_us']) for r,x,v in phases]),'MIN_RTT8':stats([float(r['filtered_phase_us']) for r,x,v in phases])},
  'paired_future_MIN_RTT8_comparator':{'scope':'Descriptive comparator; same pre-cutoff last16 PPS rule, estimator-specific phase base, no validation mean subtraction',
   'base_us':filter_base,'uncorrected':stats(filter_future),'fixed_base_corrected':stats([v-filter_base for v in filter_future])},
  'heldout':stats(errors),'horizons':{},'synthetic_gate_checks':synthetic_checks(),
  'network_delay_us':{'n':len(exchanges),'min':min(r['delay'] for r in exchanges),'median':statistics.median(r['delay'] for r in exchanges),'max':max(r['delay'] for r in exchanges)}}
 last_train=max(r['receive'] for r in kept)
 rates=[fit(kept[:i]+kept[i+1:],True)['ppm'] for i in range(len(kept))]
 result['leave_one_burst_out_rate_sensitivity']={'n':len(rates),'min_ppm':min(rates),'max_ppm':max(rates),
  'range_ppm':max(rates)-min(rates),'meaning':'Training-data sensitivity diagnostic only; not an uncertainty interval and no model reselection'}
 result['last_admitted_training_receive_us']=last_train
 # Descriptive decomposition only. Never alter the sealed rate/base or errors.
 tx=[(x-test[0][1]+float(r['local_fraction_us'])-float(test[0][0]['local_fraction_us']))/1e6 for r,x,v in test]
 mx=math.fsum(tx)/len(tx);my=math.fsum(errors)/len(errors)
 slope=math.fsum((x-mx)*(e-my) for x,e in zip(tx,errors))/math.fsum((x-mx)**2 for x in tx)
 intercept=my-slope*mx
 result['future_error_trend']={'slope_us_per_s':slope,'intercept_at_first_validation_PPS_us':intercept,
  'descriptive_detrended_residual':stats([e-intercept-slope*x for x,e in zip(tx,errors)]),
  'role':'Posthoc precision/drift diagnostic only; no validation mean or rate correction is applied to heldout accuracy'}
 result['post_cutoff_horizons']={}
 for h in (10,30,60,120,300):
  selected=[v for r,x,v in test if x-last_train+float(r['local_fraction_us'])<h*1000000]
  result['horizons'][str(h)]={**stats(selected),'maximum_forecast_age_s':max([(x-last_train+float(r['local_fraction_us']))/1e6 for r,x,v in test if x-last_train+float(r['local_fraction_us'])<h*1000000],default=None)}
  result['post_cutoff_horizons'][str(h)]=stats([v for r,x,v in test if x<cut+h*1000000])
 result['heldout_age_range_s']=[min((x-last_train+float(r['local_fraction_us']))/1e6 for r,x,v in test),max((x-last_train+float(r['local_fraction_us']))/1e6 for r,x,v in test)]
 result['GNSS_rate_diagnostic_only']={}
 for name,group in (('training',[(r,x) for r,x,v in phases if x<cut]),('future',[(r,x) for r,x,v in phases if x>=cut])):
  first,x0=group[0];fraction=float(first['local_fraction_us']);number=int(first['number'])
  points=[]
  for r,x in group:
   dx=x-x0+float(r['local_fraction_us'])-fraction
   points.append({'x':dx,'y':(int(r['number'])-number)*1000000-dx,'delay':1})
  diagnostic=fit(points)
  result['GNSS_rate_diagnostic_only'][name]={'n':len(points),'rate_ppm':diagnostic['ppm'],
   'residual':stats([p['y']-prediction(diagnostic,p['x']) for p in points]),'role':'Post-hoc diagnostic only; never trains/reselects NTP model'}
 sealed=json.loads((folder/'sealed-model.json').read_text())
 assert [r['sample'] for r in kept]==sealed['selected_samples']
 assert len(kept)==sealed['training_n']
 assert [int(r['number']) for r,x,v in calibration]==sealed['calibration_PPS']
 assert model['x0']==sealed['x0_relative_us'] and model['y0']==sealed['y0_relative_us'] and abs(model['a']-sealed['intercept_us'])<1e-8
 assert abs(model['ppm']-sealed['rate_ppm'])<1e-9 and abs(base-sealed['base_us'])<1e-6,'Frozen coefficients changed'
 for r,x,v in test:assert abs(v-float(r['frozen_error_us']))<1e-6,'Stored forecast differs'
 result['verification']='PASS: exact selected policy and heldout derivatives; private raw clock-bridge replay is a separate audit'
 return result

if __name__=='__main__':
 folder=Path(__file__).resolve().parent
 print(json.dumps(analyze(folder),indent=2,allow_nan=False))
