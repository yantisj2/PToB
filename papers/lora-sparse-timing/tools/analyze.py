#!/usr/bin/env python3
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""Recompute selected-data statistics. No hardware or private paths required."""
import csv, hashlib, json, math, statistics
from pathlib import Path
P=Path(__file__).resolve().parents[1]
def rows(name):
 with (P/'data'/name).open(newline='') as f:return list(csv.DictReader(f))
def stats(values):
 if not values:return {'n':0}
 n=len(values);mean=sum(values)/n;s=sorted(abs(x) for x in values)
 return {'n':n,'mean_ns':mean,'RMS_ns':math.sqrt(sum(x*x for x in values)/n),'centered_SD_ns':math.sqrt(sum((x-mean)**2 for x in values)/n),'p99_absolute_ns':s[math.ceil(.99*n)-1],'max_absolute_ns':s[-1]}
def analyze():
 out={};c=json.loads((P/'data/cohorts.json').read_text())
 for run in ['f288','f287']:
  r=rows(run+'-phase.csv');x=[float(x['phase_ns']) for x in r if x['phase_ns']!='']
  out[run]=dict(captured=len(r),eligible=sum(int(x['reference_eligible']) for x in r),mapped=sum(int(x['map_available']) for x in r),phase=stats(x))
  assert len({x['sequence'] for x in r})==len(r)
  assert all(int(x['sequence'])==int(r[0]['sequence'])+i for i,x in enumerate(r))
  assert all((x['phase_ns']!='')==bool(int(x['map_available'])) for x in r)
  if run=='f288':
   assert [int(r[0]['sequence']),int(r[-1]['sequence'])]==[28,927]
   out[run]['physical_sync_age_bins']={}
   for lo in range(0,30,5):
    y=[float(x['phase_ns']) for x in r if lo<=float(x['physical_sync_age_s'])<lo+5]
    out[run]['physical_sync_age_bins'][f'{lo}..{lo+5}']=stats(y)
    assert len(y)==150
   old=[float(x['phase_ns']) for x in r if float(x['accepted_pair_age_s'])>=30]
   out[run]['accepted_pair_age_ge30s']=stats(old)
   for k,v in c['clock_phase'].items():
    if k in out[run]['phase']:assert math.isclose(v,out[run]['phase'][k],rel_tol=1e-11,abs_tol=1e-7)
   assert out[run]['mapped']==900
  else:assert (len(r),len(x))==(215,206)
 b=rows('bandwidth-pairs.csv');counts=rows('bandwidth-counts.csv');out['bandwidth']=[]
 for a in counts:
  x=[float(r['phase_ns']) for r in b if all(r[k]==a[k] for k in ['run','context','bandwidth_hz'])]
  assert len(x)==int(a['pps_bracketed'])==int(a['accepted_pairs'])
  assert int(a['accepted_pairs'])<=int(a['offered_sync'])
  out['bandwidth'].append({**a,'phase':stats(x)})
 out['secondary_pooled_b285']={str(bw):stats([float(r['phase_ns']) for r in b if r['run']=='B285' and int(r['bandwidth_hz'])==bw]) for bw in [41667,62500,125000,250000,500000]}
 cal=[float(r['raw_phase_ns']) for r in rows('calibration-pairs.csv')]
 prior=round(-statistics.median(cal));assert prior==102931911==c['frozen_native_event_delay_ns']
 out['calibration']={'n':len(cal),'native_event_delay_ns':prior,'raw_phase':stats(cal)}
 def toa(sf,pl):
  symbol_us=(2**sf)*2
  payload_symbols=8+max(math.ceil((8*pl-4*sf+28+16)/(4*sf))*8,0)
  return round((12+4.25+payload_symbols)*symbol_us)
 packet=[toa(11,pl) for pl in [24,32,112]];out['airtime']={'SYNC_us':packet[0],'FOLLOW_us':packet[1],'META_us':packet[2],'guard_us':10000,'nominal_guarded_hour_us':120*(packet[0]+packet[1]+20000)+12*(packet[2]+10000),'cap_us':90000000}
 assert packet==[263168,295936,787456]
 assert out['airtime']['nominal_guarded_hour_us']==79061952
 sf12=[toa(12,pl) for pl in [24,32,112]]
 out['airtime']['SF12_comparison']={'packet_us':sf12,'guarded_hour_us':120*(sf12[0]+sf12[1]+20000)+12*(sf12[2]+10000)}
 assert sf12==[460800,591872,1443840]
 assert out['airtime']['SF12_comparison']['guarded_hour_us']==146166720
 return out
if __name__=='__main__':
 d=analyze();(P/'results.json').write_text(json.dumps(d,indent=2)+'\n')
 print('PASS: F288 900/900, F287 206/215 of planned 900; 31 bandwidth pairs, 4 calibration pairs; age bins and airtime reproduced.')
