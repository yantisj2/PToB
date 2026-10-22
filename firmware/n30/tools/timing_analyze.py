#!/usr/bin/env python3
"""Replay CRC-bound N30 timing records and a conditional interval-fit shadow.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
No device I/O. Bounds below describe an assumed observation model, never UTC.
"""
import argparse,collections,hashlib,json,math,re
from pathlib import Path
import framed_console as console

def stats(values):
 if not values:return {'n':0,'mean_ns':None,'rms_ns':None,'max_abs_ns':None}
 return {'n':len(values),'mean_ns':sum(values)/len(values),'rms_ns':math.sqrt(sum(x*x for x in values)/len(values)),'max_abs_ns':max(abs(x) for x in values)}

def clip(poly,a,b,c):
 """Intersect a convex polygon with a*phase + b*rate <= c."""
 out=[]
 if not poly:return out
 previous=poly[-1];before=a*previous[0]+b*previous[1]-c
 for point in poly:
  after=a*point[0]+b*point[1]-c
  if (before<=0)!=(after<=0):
   fraction=before/(before-after)
   out.append((previous[0]+fraction*(point[0]-previous[0]),previous[1]+fraction*(point[1]-previous[1])))
  if after<=0:out.append(point)
  previous,before=point,after
 return out

class IntervalFit:
 """Bounded affine set-membership treatment, recomputed on at most64 points.
 Per-channel bias and absolute quantizer origin remain unidentified. A caller
 must explicitly select the centered-bin hypothesis before running this.
 """
 def __init__(self,accel_bound=100,window=64):
  if not math.isfinite(accel_bound) or accel_bound<0 or not 8<=window<=64:raise ValueError('Invalid shadow model assumptions')
  self.accel=accel_bound;self.window=window;self.points=[]
 def add(self,row):self.points=(self.points+[row])[-self.window:]
 def predict(self,tick):
  if len(self.points)<8:return None
  first=self.points[0];latest=self.points[-1]
  if tick<latest['corrected20'] or tick-latest['corrected20']>=30000000:return None
  origin=first['local20'];base=first['remote_ns']
  poly=[(-5000000.,-50000.),(5000000.,-50000.),(5000000.,50000.),(-5000000.,50000.)]
  for p in self.points:
   x=(p['local20']-origin)/20000000
   y=(p['remote_ns']-base)-(p['local20']-origin)*50
   # Descriptor interval + full recorded enclosure + TX tick quantization.
   # Acceleration is an explicit assumed envelope, not a measured bound.
   width=250+p['enclosure_ns']+31.25+.5*self.accel*x*x
   poly=clip(clip(poly,1,x,y+width),-1,-x,-y+width)
   if not poly:return {'feasible':False,'reason':'INCONSISTENT_OBSERVATION_MODEL'}
  x=(tick-origin)/20000000
  values=[phase+rate*x for phase,rate in poly]
  nominal=base+(tick-origin)*50;projection=.5*self.accel*x*x
  lo=nominal+min(values)-projection;hi=nominal+max(values)+projection
  inside=lo>=latest['created_ns'] and hi<latest['expires_ns']
  return {'feasible':True,'prediction_ns':nominal+(min(values)+max(values))/2,'conditional_half_width_ns':(max(values)-min(values))/2,'source_interval_inside':inside,'conditional_sub_tick':inside and (max(values)-min(values))/2<=62.5,'UTC_bound':'UNKNOWN','channel_bias':'UNKNOWN'}

def decode(raw,manifest):
 frames=[];errors=[];parts=[];boot=None
 def finish(parts):
  nonlocal boot
  chunk=b''.join(parts)
  if b'N30_TIMING_STATUS ' not in chunk:return
  try:
   m=re.search(rb'W12_V12_BEGIN .*?nonce=([0-9a-f]{16})\b',chunk)
   if not m:raise ValueError('Missing timing BEGIN')
   parser=console.Parser(m[1].decode(),dict(manifest,n30_frame_kind='TIMING'),boot)
   found=parser.feed(chunk)
   if len(found)!=1:raise ValueError('Incomplete timing response')
   boot=found[0]['begin']['session'];frames.append(found[0])
  except (RuntimeError,ValueError,KeyError) as e:errors.append(str(e))
 for line in raw.splitlines(keepends=True):
  if line.startswith(b'W12_V12_BEGIN '):
   if parts:finish(parts)
   parts=[line]
  elif parts:
   parts.append(line)
   if line.startswith(b'W12_V12_END '):finish(parts);parts=[]
 if parts:finish(parts)
 return frames,errors

def analyze(frames,centered=False,accel_bound=100,work_margin_us=1000):
 samples={};conflicts=set();sessions={}
 for f in frames:
  console.validate_timing(f['rows']);s=f['rows']['N30_TIMING_STATUS'];key=(f['begin']['session'],s['id']);sessions[key]=s
  for i in range(int(s['count'])):
   row=f['rows'][f'N30_TIMING_SAMPLE{i}'];identity=(*key,int(row['n']))
   normalized={k:(v if k in ('root','boot') else int(v)) for k,v in row.items()}
   normalized.update(capture_boot=f['begin']['session'],diagnostic_id=s['id'])
   if identity in conflicts:continue
   if identity in samples and samples[identity]!=normalized:conflicts.add(identity);del samples[identity]
   else:samples[identity]=normalized
 conflicts=sorted(conflicts)
 rows=list(samples.values());matched=[r for r in rows if r['matched']];causal=[r['remote_ns']-r['predicted_ns'] for r in matched if r['causal']]
 channels={str(ch):stats([r['remote_ns']-r['predicted_ns'] for r in matched if r['causal'] and r['channel']==ch]) for ch in sorted({r['channel'] for r in matched})}
 groups=collections.defaultdict(list)
 for r in matched:groups[(r['capture_boot'],r['diagnostic_id'],r['root'],r['boot'],r['source'],r['train'],r['epoch'])].append(r)
 shadow=[]
 if centered:
  for scope,records in groups.items():
   model=IntervalFit(accel_bound);events=[];scores=[];trace=[];inconsistent=missing=narrow=0;max_half=None
   # Original observations are scored before their later corrections train
   # this hypothetical host model. Publication includes an explicit work margin.
   for r in records:
    events.extend([(r['local20'],0,r),(r['corrected20']+work_margin_us*20,1,r)])
   for tick,kind,r in sorted(events,key=lambda e:(e[0],e[1],e[2]['n'])):
    if kind:model.add(r);continue
    p=model.predict(tick)
    if not p:missing+=1;continue
    if not p['feasible']:inconsistent+=1;continue
    scores.append(r['remote_ns']-p['prediction_ns']);narrow+=p['conditional_sub_tick']
    trace.append({'n':r['n'],'innovation_ns':scores[-1],'conditional_half_width_ns':p['conditional_half_width_ns']})
    max_half=max(max_half or 0,p['conditional_half_width_ns'])
   shadow.append({'scope':scope,'causal_innovation':stats(scores),'predictions':trace,'unavailable_predictions':missing,'inconsistent_predictions':inconsistent,'conditional_sub_tick_predictions':narrow,'maximum_conditional_half_width_ns':max_half,'UTC_bound':'UNKNOWN'})
 expected=sum(int(s['sampled']) for s in sessions.values())
 return {'schema':1,'scope':'RELATIVE_DIAGNOSTIC_AND_CONDITIONAL_SHADOW_ONLY','frames':len(frames),'sessions':len(sessions),'record_conflicts':conflicts,'sampled_by_firmware':expected,'unique_records_received':len(rows),'missing_sample_records':max(0,expected-len(rows)),'unmatched_records':len(rows)-len(matched),'causal_unavailable':sum(not r['causal'] for r in matched),'causal_innovation':stats(causal),'channels':channels,'bracket_width_ns':stats([(r['after20']-r['before20'])*50 for r in rows]),'fine_high_bits':sorted({r['fine']>>10 for r in rows}),'timer_phase_mod10':dict(collections.Counter(str(r['local20']%10) for r in rows)),'shadow':shadow,'assumptions':{'centered_descriptor_bins':centered,'acceleration_envelope_ppb_s':accel_bound,'shadow_work_margin_us':work_margin_us,'RF_channel_bias':'UNKNOWN','quantizer_phase_calibrated':False,'shadow_can_control_clock':False},'absolute_UTC_bound':'UNKNOWN','physical_output_accuracy':'NOT_MEASURED'}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--raw',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--assume-centered-bins',action='store_true');p.add_argument('--accel-bound-ppb-s',type=float,default=100);p.add_argument('--work-margin-us',type=int,default=1000);a=p.parse_args()
 if a.work_margin_us<0 or a.work_margin_us>1000000:p.error('Explicit work margin must be0..1000000us')
 if not math.isfinite(a.accel_bound_ppb_s) or a.accel_bound_ppb_s<0:p.error('Acceleration assumption must be finite and nonnegative')
 frames,errors=decode(a.raw.read_bytes(),json.loads(a.manifest.read_text()));result=analyze(frames,a.assume_centered_bins,a.accel_bound_ppb_s,a.work_margin_us)
 result.update(frame_errors=errors,complete_CRC_population=not errors,input_sha256={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in (a.raw,a.manifest)})
 a.out.mkdir(parents=True,exist_ok=False);(a.out/'analysis.json').write_text(json.dumps(result,indent=2)+'\n');print('Analyzed',len(frames),'CRC timing frames;',len(errors),'incomplete/refused frames; physical accuracy unqualified')
if __name__=='__main__':main()
