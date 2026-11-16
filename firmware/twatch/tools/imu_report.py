#!/usr/bin/env python3
"""Offline analysis of exact-watch IMU frames; no device access.
Optional labels CSV: start_s,end_s,kind,label; kind is pose,raise,negative.
Times are capture-relative seconds. Missing coverage is UNKNOWN, never a miss.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,csv,json,math,os,statistics,subprocess
from pathlib import Path

def analyze(frames,labels=()):
 rows=[(float(f['capture_elapsed_s']),dict(f['rows']['TWATCH_IMU'])) for f in frames if 'TWATCH_IMU' in f.get('rows',{})]
 for _,r in rows:
  if 'tilt_events' in r:r['tilt_shadow_events']=r['tilt_events'] # common historical counter, detector field distinguishes control
 if not rows:raise ValueError('No TWATCH_IMU frames: use a diagnostics-capable build and --imu')
 sessions={f['begin']['session'] for f in frames}
 if len(sessions)!=1:raise ValueError('Mixed boots: analyze each capture separately')
 if any(b[0]<=a[0] for a,b in zip(rows,rows[1:])):raise ValueError('Nonmonotonic capture times')
 counters=('samples','glance_events','tilt_shadow_events','wakes','lowered','arms','disarms','ignored_awake','ignored_disarmed','ignored_refractory','stale_polls','overflows','sensor_errors','resets','errors','bus_errors','budget_errors')
 first,last=rows[0][1],rows[-1][1]
 if any(int(b[1][k])<int(a[1][k]) for a,b in zip(rows,rows[1:]) for k in counters):raise ValueError('Counter reset: split capture at reinitialization')
 result={'scope':'HOST_CAPTURE_OBSERVATION_NOT_GESTURE_QUALIFICATION','detector':rows[-1][1].get('detector','59'),'frames':len(rows),'elapsed_s':rows[-1][0]-rows[0][0],
 'counter_delta':{k:int(last[k])-int(first[k]) for k in counters},'final_counters':{k:int(last[k]) for k in counters},
 'max_sample_age_ms':max(int(r['sample_age_ms']) for _,r in rows),'max_poll_gap_us':int(last['max_poll_gap_us']),
 'max_service_us':int(last['max_service_us']),'orientation_readback_ok':all(r['orientation_ok']=='1' for _,r in rows),
 'ready_all':all(r['ready']=='1' for _,r in rows),'labels':[],'limitations':['2 Hz snapshots do not reconstruct the full 12.5 Hz motion waveform','Readback verifies programmed P2 mapping, not physical screen/strap axes','No accuracy score without operator-labeled trials']}
 for label in labels:
  start,end=float(label['start_s']),float(label['end_s']);kind=label['kind']
  if kind not in ('pose','raise','negative') or end<=start:raise ValueError('Invalid label interval/kind')
  window=[q for q in rows if start<=q[0]<=end]
  before=[q for q in rows if q[0]<=start];after=[q for q in rows if q[0]>=end]
  entry={'label':label['label'],'kind':kind,'start_s':start,'end_s':end,'result':'UNKNOWN','reason':'Incomplete or unhealthy coverage'}
  if before and after:
   coverage=[q for q in rows if before[-1][0]<=q[0]<=after[0][0]]
   healthy=bool(window) and all(r['ready']=='1' and int(r['sample_age_ms'])<=500 for _,r in coverage)
   healthy=healthy and all(b[0]-a[0]<=1.5 for a,b in zip(coverage,coverage[1:]))
   healthy=healthy and all(int(after[0][1][k])==int(before[-1][1][k]) for k in ('errors','bus_errors','budget_errors','overflows','resets'))
   if healthy:
    entry.pop('reason');a,b=before[-1][1],after[0][1]
    entry['events']={k:int(b[k])-int(a[k]) for k in ('glance_events','tilt_shadow_events','wakes','lowered')}
    if kind=='pose':
     xyz=[tuple(int(v) for v in r['xyz'].split('/')) for _,r in window];mean=[statistics.mean(v[i] for v in xyz) for i in range(3)];norm=math.sqrt(sum(x*x for x in mean))
     entry.update(result='OBSERVED',mean_counts=mean,stdev_counts=[statistics.pstdev(v[i] for v in xyz) for i in range(3)],unit_vector=[x/norm for x in mean] if norm else None)
    else:
     eligible=all(r['asleep']=='1' for _,r in coverage[:-1]) or entry['events']['wakes']>0
     # A raise cannot be scored while touch/button already has the display awake.
     if kind=='raise' and not eligible:entry.update(reason='Display already awake; no wake opportunity')
     else:entry['result']=('DETECTED' if entry['events']['wakes'] else 'MISSED') if kind=='raise' else ('FALSE_WAKE' if entry['events']['wakes'] else 'QUIET')
  result['labels'].append(entry)
 return result,rows

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--frames',type=Path,required=True);p.add_argument('--labels',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 repo=Path(__file__).resolve().parents[3];out=a.out.resolve()
 if repo/'artifacts' not in out.parents or out.exists() or subprocess.run(['git','-C',str(repo),'check-ignore','--quiet',str(out)]).returncode:raise SystemExit('Use a fresh ignored artifacts directory')
 frames=[json.loads(x) for x in a.frames.read_text().splitlines() if x.strip()]
 with a.labels.open() if a.labels else open(os.devnull) as f:labels=list(csv.DictReader(f)) if a.labels else []
 result,rows=analyze(frames,labels);os.umask(0o077);out.mkdir(mode=0o700)
 (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
 with (out/'samples.csv').open('w') as f:
  fields=['capture_elapsed_s',*rows[0][1].keys()];w=csv.DictWriter(f,fieldnames=fields);w.writeheader()
  for at,r in rows:w.writerow(dict(capture_elapsed_s=at,**r))
 print(json.dumps(result,indent=2))
if __name__=='__main__':main()
