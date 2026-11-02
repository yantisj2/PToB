#!/usr/bin/env python3
"""Capture cached kernel PPS edges on an enrolled Pi; report unfiltered tails.

Copyright (c) 2026 Jonathan Yantis. MIT.
This observes software timestamps, not electrical PPS or independent UTC error.
"""
import argparse
import bisect
from decimal import Decimal
import json
import math
import os
from pathlib import Path
import time

REMOTE_SOURCE = r'''import json,math,os,pathlib,re,time
P=pathlib.Path
def observation(text,now):
 def finite(value):
  n=float(value)
  if not math.isfinite(n):raise ValueError('nonfinite status number')
  return n
 def constant(value):raise ValueError('invalid status constant')
 def identity(value):
  if value is None:return None
  if not isinstance(value,str) or not re.fullmatch('[0-9a-f]{16}',value):raise ValueError('invalid lineage field')
  return value if value!='0000000000000000' else None
 def counter(value):
  if value is not None and (type(value) is not int or value<0):raise ValueError('invalid status counter')
  return value
 s=json.loads(text,parse_float=finite,parse_constant=constant);r=s.get('receiver',{});d=r.get('data') or {};q=s.get('source_status',{})
 context=[identity(d.get('boot')),identity(q.get('root')),identity(q.get('root_boot')),counter(q.get('epoch')),identity(q.get('parent')),identity(q.get('context'))]
 counters={k:counter(d.get(v)) for k,v in [('receiver_pps_emitted','pps_emitted'),('receiver_rx','rx'),('receiver_authenticated','authenticated')]}
 observed=s.get('observed_boottime_s')
 if observed is not None and type(observed) not in (int,float):raise ValueError('invalid observation time')
 age=now-observed if observed is not None else None
 good=bool(age is not None and 0<=age<=3 and r.get('fresh') and not r.get('error') and
           all(type(d.get(k)) is int and d[k]==0 for k in ('error','receiver_error','capture_error','uart_error')) and
           d.get('following') and d.get('clock_valid') and d.get('offer_valid') and q.get('current') and
           all(v is not None for v in context) and s.get('pps',{}).get('fresh') and s.get('chain',{}).get('complete'))
 return dict(boottime_s=now,observed_boottime_s=observed,eligible=good,context=context,**counters)
os.sched_setaffinity(0,{0,1,2})
def bt():return time.clock_gettime(time.CLOCK_BOOTTIME)
dev=P('/dev/pps-ptob').resolve(strict=True)
base=P('/sys/class/pps')/dev.name
if not (base/'name').read_text().strip().startswith('ptob-pps'):raise ValueError('wrong PPS device')
out=dict(schema=1,observer_cpus=sorted(os.sched_getaffinity(0)),poll_s=.05,
         observer_source_sha256=globals().get('PTOB_SOURCE_SHA256'),
         start_boottime_s=bt(),edges=[],status=[],errors=[])
last=None;next_status=out['start_boottime_s']
try:
 while bt()-out['start_boottime_s']<PARAMS['seconds']:
  now=bt()
  if now>=next_status:
   try:
    out['status'].append(observation(P('/run/ptob-dashboard/status.json').read_text(),bt()))
   except (OSError,ValueError,TypeError,KeyError,AttributeError) as e:
    out['errors'].append(dict(boottime_s=bt(),stage='status',error=str(e)))
    out['status'].append(dict(boottime_s=bt(),eligible=False,context=None))
   next_status+=1
   if next_status<=now:next_status=now+1
  value=(base/'assert').read_text().strip()
  if value!=last:
   out['edges'].append(dict(boottime_s=bt(),assertion=value,seed=last is None));last=value
  time.sleep(.05)
except BaseException as e:out['error']=str(e)
finally:out['end_boottime_s']=bt()
print(json.dumps(out,allow_nan=False))
'''


def timestamp(assertion):
    stamp, sequence = assertion.split('#')
    value = Decimal(stamp)*1_000_000_000
    if not value.is_finite() or value != value.to_integral_value():
        raise ValueError('PPS timestamp is not integral nanoseconds')
    seq = int(sequence)
    if seq < 0:
        raise ValueError('negative PPS sequence')
    return int(value), seq


def statistics(values):
    if not values:
        return dict(n=0, mean_ns=None, rms_ns=None, p99_abs_ns=None, max_abs_ns=None,
                    above_1us=None, above_2us=None, above_5us=None)
    absolute = sorted(abs(v) for v in values)
    return dict(n=len(values), mean_ns=math.fsum(values)/len(values),
                rms_ns=math.sqrt(math.fsum(v*v for v in values)/len(values)),
                p99_abs_ns=absolute[math.ceil(.99*len(values))-1], max_abs_ns=absolute[-1],
                above_1us=sum(v>1000 for v in absolute),
                above_2us=sum(v>2000 for v in absolute), above_5us=sum(v>5000 for v in absolute))


def analyse(record):
    statuses = record['status']
    times = [s['boottime_s'] for s in statuses]
    if any(b<=a for a,b in zip(times,times[1:])):
        raise ValueError('status observations must be in monotonic order')
    phases, eligible, periods, gaps, seeds = [], [], [], [], []
    invalid, previous, observed_skips, reused = [], None, 0, 0
    seen = set()
    for edge in record['edges']:
        try:
            tick, seq = timestamp(edge['assertion'])
        except (ValueError, ArithmeticError) as e:
            invalid.append(dict(edge=edge, error=str(e)))
            previous = None
            continue
        if edge.get('seed'):
            # Its timestamp may predate this capture by hours. Preserve it,
            # but do not turn the seed-to-first-edge interval into a new gap.
            seeds.append(edge);seen.add((tick,seq));previous=(tick,seq,True)
            continue
        if (tick,seq) in seen:
            reused+=1
            continue
        seen.add((tick,seq))
        phase = (tick+500_000_000)%1_000_000_000-500_000_000
        phases.append(phase)
        i = bisect.bisect_left(times,edge['boottime_s'])
        # Require actual observations on both sides; boundary/unknown edges
        # stay in the full population. This is descriptive lineage, not proof
        # of exact-event authentication from the one-Hz status stream.
        if 0<i<len(statuses):
            a,b=statuses[i-1],statuses[i]
            if (a['eligible'] and b['eligible'] and a['context']==b['context'] and
                    b['boottime_s']-a['boottime_s']<=1.6):
                eligible.append(phase)
        if previous is not None:
            interval, delta = tick-previous[0], seq-previous[1]
            if previous[2] and delta==1:
                pass
            elif not previous[2] and delta==1 and 500_000_000<interval<1_500_000_000:
                periods.append(interval-1_000_000_000)
            else:
                skipped=max(0,delta-1);observed_skips+=skipped
                gaps.append(dict(previous_sequence=previous[1],sequence=seq,sequence_delta=delta,
                                 interval_ns=interval,unobserved_assertions=skipped,
                                 starts_at_cached_seed=previous[2],
                                 nominal_missing_slots=max(0,(interval+500_000_000)//1_000_000_000-delta)
                                 if not previous[2] and delta>0 and interval>0 else None))
        previous=(tick,seq,False)
    return dict(schema=1,seconds=record['end_boottime_s']-record['start_boottime_s'],
                observed_statuses=len(statuses),eligible_statuses=sum(bool(s['eligible']) for s in statuses),
                status_context_changes=sum(a.get('context')!=b.get('context') for a,b in zip(statuses,statuses[1:])),
                cached_seeds=seeds,duplicate_records_ignored=reused,invalid_edges=invalid,
                all_new_edge_phase=statistics(phases),source_eligible_edge_phase=statistics(eligible),
                observed_consecutive_period_error=statistics(periods),
                unobserved_assertions=observed_skips,gaps=gaps,capture_errors=record.get('errors',[]),
                capture_failure=record.get('error'),
                limits=['Software GPIO timestamps include IRQ delivery and system-clock discipline.',
                        'No independent UTC or electrical output error is measured.',
                        'Nominal missing slots are inferred from timestamps; clock steps can imitate pulse gaps.',
                        'Sequence skips mean assertions unseen by this observer, not necessarily absent pulses.',
                        'Cached seed intervals are excluded from new period/gap populations; sequence skips are still counted.',
                        'One-Hz status brackets cannot prove exact-event lineage.',
                        'P99 is a nearest-rank sample statistic; small populations do not resolve rare tails.'])


def main():
    os.umask(0o077)
    parser=argparse.ArgumentParser(description=__doc__)
    mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--capture',action='store_true',help='read the enrolled Pi via guarded SSH')
    mode.add_argument('--analyse',type=Path,help='analyse an existing raw capture; no hardware access')
    parser.add_argument('--seconds',type=int,default=300)
    parser.add_argument('--connection',type=Path,default=Path.home()/'.config/ptob/pi5.json')
    parser.add_argument('--out',type=Path,required=True,help='new JSON report under ignored artifacts/')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[3]
    out=args.out.resolve()
    if not out.is_relative_to(root/'artifacts') or out.exists() or not out.parent.is_dir():
        raise ValueError('choose a new report in an existing ignored artifacts/ directory')
    if not 1<=args.seconds<=3600:
        raise ValueError('capture duration must be 1–3600 seconds')
    if args.capture:
        from remote import execute
        profile=json.loads(args.connection.expanduser().read_text())
        raw=execute(profile,REMOTE_SOURCE,dict(seconds=args.seconds),timeout=args.seconds+30)
    else:
        raw=json.loads(args.analyse.read_text())
        raw=raw.get('raw',raw)
    result=dict(raw=raw,analysis=analyse(raw),created_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
                attribution='Jonathan Yantis',observation_license='CC BY 4.0',
                evidence_status='Private local observations; not publication-reviewed or accuracy qualification')
    with out.open('x') as f:
        out.chmod(0o600);json.dump(result,f,indent=2,allow_nan=False);f.write('\n')
    print(json.dumps(dict(report=str(out),analysis=result['analysis']),indent=2))


if __name__=='__main__':main()
