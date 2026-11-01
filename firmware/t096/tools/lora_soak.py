#!/usr/bin/env python3
"""Read-only, bounded autonomous LoRa reception soak. MIT, Jonathan Yantis, 2026.

Capture files contain private lab identity/data: use an ignored artifacts path.
No reset, flash, profile/RF control, clock discipline or automatic reconnection.
"""
import argparse
import concurrent.futures
import json
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import threading
import time

from console_capture import capture, validate_config


def atomic_json(path, value):
    temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n')
    temporary.replace(path)


class Gate:
    """One continuous source/capture context, two real 300s META intervals.

    Startup acquisition is retained; qualification needs native >=180s training.
    Host time measures observation continuity only, never UTC accuracy.
    """
    def __init__(self, max_rms_ns, gap_s=10):
        self.max_rms_ns=max_rms_ns
        self.gap_s=gap_s
        self.previous=None
        self.started=None
        self.meta_origin=self.event_origin=self.tick_origin=0
        self.metas=0
        self.qualified=False
        self.ever_qualified=False
        self.resets=0
        self.samples=0
        self.pairs=0
        self.rms_count=0
        self.rms_sum2=0
        self.rms_max=0
        self.reason='awaiting first accepted pair'

    def reset(self, reason):
        self.started=None
        self.meta_origin=self.event_origin=self.tick_origin=0
        self.metas=0
        self.qualified=False
        self.resets+=1
        self.reason=reason

    def observe(self, p, host_ns):
        if p.get('diag')!=1 or p.get('profile')!='PROD_60_300':
            raise ValueError('slow diagnostic profile required')
        self.samples+=1
        stamp=host_ns/1e9
        context=tuple(p[k] for k in ('boot','emitter','emitter_boot','stream','source_epoch','capture_epoch'))
        counters=tuple(p[k] for k in ('accepted','rejected','missing','late','bad','missing_map','storage_fail'))+tuple(p['hw_errors'])
        last=self.previous
        reason=None
        if last:
            old, old_stamp, old_context, old_counters=last
            if stamp<=old_stamp or stamp-old_stamp>self.gap_s:reason='host capture gap'
            elif p['at_us']<=old['at_us']:reason='device monotonic discontinuity'
            elif context!=old_context:reason='source or capture context changed'
            elif p['serial']==old['serial'] and p['meta_until_us']!=old['meta_until_us']:reason='META lease changed without new META'
            elif p['accepted']==old['accepted'] and any(p[k]!=old[k] for k in ('event_ns','event_tick','event_mono_us','physical_until_us')):reason='physical lease/event changed without new pair'
            elif any(a<b for a,b in zip(counters,old_counters)):reason='counter reset'
            elif any(a>b for a,b in zip(counters[1:],old_counters[1:])):reason='reception/timing error counter advanced'
            elif p['accepted']>old['accepted']:
                count=p['accepted']-old['accepted']
                self.pairs+=count
                if p['event_ns']<=old['event_ns'] or p['event_tick']<=old['event_tick']:reason='UTC/physical event did not advance'
                elif count!=1:reason='unobserved accepted pair(s)'
                elif old['event_mono_us'] and p['event_mono_us']-old['event_mono_us']>62000000:reason='precision cadence gap'
                else:
                    nominal=(p['event_tick']-old['event_tick'])*62.5
                    if abs(p['event_ns']-old['event_ns']-nominal)>max(1000000,nominal*.0002):reason='UTC/physical event discontinuity'
            if p['serial']<old['serial']:reason='META serial regressed'
        usable=(p['requested'] and p['active'] and not p['transmitter'] and not p['fault'] and
                not p['error'] and p['time_valid'] and p['accepted']>0 and
                p['event_ns']>0 and p['at_us']<p['physical_until_us'] and p['at_us']<p['meta_until_us'])
        if not usable:reason=reason or 'LoRa UTC unavailable'
        if self.qualified and not p['trained']:reason=reason or 'trained timing fit lost'
        if p['trained'] and p['link_fit'][1]>self.max_rms_ns:reason='native link RMS exceeds limit'
        if last and p['serial']>last[0]['serial']+1:reason=reason or 'unobserved META serial(s)'
        if reason:self.reset(reason)
        elif self.started is None:
            self.started=stamp
            self.metas=1
            self.meta_origin=p['meta_until_us']-362000000
            self.event_origin=p['event_ns']
            self.tick_origin=p['event_tick']
            self.reason='observing two full META cycles'
        elif last and p['serial']>last[0]['serial']:
            # A poll must witness each boundary; a serial jump cannot fill gaps.
            if p['meta_until_us']<=last[0]['meta_until_us']:
                reason='META origin did not advance';self.reset(reason)
            else:self.metas+=1
        if p['trained'] and (not last or p['accepted']>last[0]['accepted']):
            rms=p['link_fit'][1]
            self.rms_count+=1
            self.rms_sum2+=rms*rms
            self.rms_max=max(self.rms_max,rms)
        if (self.started is not None and stamp-self.started>=600 and self.metas>=3 and p['trained'] and
            p['meta_until_us']-362000000-self.meta_origin>=600000000 and
            p['event_ns']-self.event_origin>=600000000000 and p['event_tick']-self.tick_origin>=9600000000):
            self.qualified=self.ever_qualified=True
            self.reason='two continuous META cycles and trained physical timing observed'
        self.previous=(p,stamp,context,counters)
        return reason

    def summary(self):
        return dict(qualified=self.qualified, ever_qualified=self.ever_qualified,
                    reason=self.reason, samples=self.samples, observed_pairs=self.pairs,
                    observed_META_boundaries=self.metas, continuity_resets=self.resets,
                    native_fit_samples=self.rms_count, native_fit_rms_max_ns=self.rms_max,
                    native_fit_rms_quadratic_mean_ns=math.sqrt(self.rms_sum2/self.rms_count) if self.rms_count else None,
                    metric='native overlapping fit residuals; not independent UTC/output accuracy')


class Budget:
    def __init__(self, folder, limit, reserve):
        self.folder,self.limit,self.reserve=folder,limit,reserve
        self.used=0
        self.checked=None
        self.lock=threading.Lock()

    def __call__(self, size):
        with self.lock:
            if self.used+size>self.limit:raise OSError('run disk budget exhausted')
            now=time.monotonic()
            if self.checked is None or now-self.checked>=1:
                if shutil.disk_usage(self.folder).free<self.reserve:raise OSError('free disk reserve reached')
                self.checked=now
            self.used+=size


class Monitor:
    def __init__(self, out, units, receivers, rms, budget, stop, stabilize_timeout=7200):
        self.out,self.stop,self.budget=out,stop,budget
        self.gates={u:Gate(rms) for u in receivers}
        self.units={u:dict(replies=0) for u in units}
        self.lock=threading.Lock()
        self.armed_at=None
        self.violations=0
        self.failure=(out/'failures.jsonl').open('x')
        self.csv=(out/'samples.csv').open('x')
        self.csv.write('host_mono_ns,unit,at_us,emitter,serial,cursor,accepted,trained,fit_n,rms_ns,event_ns,physical_until_us,meta_until_us\n')
        self.last_summary=0
        self.gate_deadline=time.monotonic()+stabilize_timeout
        self.gate_timed_out=False

    def event(self, unit, entry):
        with self.lock:
            reason=None
            if self.armed_at is None and not self.gate_timed_out and time.monotonic()>=self.gate_deadline:
                self.gate_timed_out=True;self.stop.set();reason='initial stability gate timed out'
            if entry['kind'] in ('query_timeout','frame_refused','capture_error','partial_tail'):
                reason=entry['kind']
                if unit in self.gates:self.gates[unit].reset(reason)
            if entry['kind']=='reply' and entry['op']=='lora-diag':
                p=entry['payload']
                self.units[unit]=dict(replies=self.units[unit]['replies']+1, latest=p)
                if unit in self.gates:reason=self.gates[unit].observe(p, entry['host_mono_ns']) or reason
                else:
                    previous=getattr(self,'tx_previous',{}).get(unit)
                    if p.get('fault') or p.get('error'):reason='transmitter fault'
                    if previous and any(p[k]>previous[k] for k in ('late','storage_fail','missing_map')):reason='transmitter timing/storage failure'
                    if not hasattr(self,'tx_previous'):self.tx_previous={}
                    self.tx_previous[unit]=p
                row=f'{entry["host_mono_ns"]},{unit},{p["at_us"]},{p["emitter"]},{p["serial"]},{p["cursor"]},{p["accepted"]},{int(p["trained"])},{p["link_fit"][0]},{p["link_fit"][1]},{p["event_ns"]},{p["physical_until_us"]},{p["meta_until_us"]}\n'
                self.budget(len(row))
                self.csv.write(row)
                self.csv.flush()
            if reason:
                if self.armed_at is not None:self.violations+=1
                line=json.dumps(dict(unit=unit, host_mono_ns=entry['host_mono_ns'], reason=reason, after_gate=self.armed_at is not None))+'\n'
                self.budget(len(line))
                self.failure.write(line)
                self.failure.flush()
            if self.armed_at is None and not self.gate_timed_out and all(g.qualified for g in self.gates.values()):
                self.armed_at=entry['host_mono_ns']
            if time.monotonic()-self.last_summary>=5:
                atomic_json(self.out/'live.json', self.summary())
                self.last_summary=time.monotonic()

    def summary(self):
        return dict(phase='SOAK' if self.armed_at is not None else 'STABILIZING',
                    initial_gate_host_mono_ns=self.armed_at, post_gate_violations=self.violations,
                    initial_gate_timed_out=self.gate_timed_out,
                    receivers={u:g.summary() for u,g in self.gates.items()}, units=self.units,
                    disk_bytes_charged=self.budget.used, agent_required=False,
                    absolute_UTC_accuracy='UNQUALIFIED', range_test=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config',type=Path,required=True,help='same exact-device config as console_capture.py')
    p.add_argument('--unit',action='append',required=True)
    p.add_argument('--receiver',action='append',required=True,help='required LoRa client; repeat for each client')
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--duration',type=int,default=7*86400)
    p.add_argument('--max-rms-ns',type=int,required=True,help='operator acceptance limit for native relative fit RMS')
    p.add_argument('--stabilize-timeout',type=int,default=7200,help='maximum initial acquisition/gating seconds')
    p.add_argument('--max-bytes',type=int,default=2*1024**3)
    p.add_argument('--free-reserve-bytes',type=int,default=1024**3)
    p.add_argument('--detach',action='store_true')
    a=p.parse_args()
    if not 620<=a.duration<=14*86400 or not 600<=a.stabilize_timeout<=14*86400 or a.max_rms_ns<=0 or a.max_bytes<16*1024**2 or a.free_reserve_bytes<16*1024**2:p.error('invalid bounded run limits')
    devices=json.loads(a.config.read_text())['devices']
    validate_config(devices)
    if len(set(a.unit))!=len(a.unit) or len(set(a.receiver))!=len(a.receiver) or not set(a.receiver)<=set(a.unit) or not set(a.unit)<=devices.keys():p.error('unique exact units and receiver subset required')
    a.out=a.out.resolve()
    if a.detach:
        if a.out.exists():p.error('output path already exists')
        launch=a.out.with_name(a.out.name+'.launcher.log')
        with launch.open('xb') as log:
            command=[sys.executable,str(Path(__file__).resolve()),*[x for x in sys.argv[1:] if x!='--detach']]
            child=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        print(json.dumps(dict(pid=child.pid, launcher_log=str(launch), output=str(a.out), stop=f'kill -TERM {child.pid}')))
        return
    a.out.mkdir(mode=0o700,parents=True,exist_ok=False)
    stop=threading.Event()
    for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,lambda *_:stop.set())
    budget=Budget(a.out,a.max_bytes-1024**2,a.free_reserve_bytes)
    monitor=Monitor(a.out,a.unit,a.receiver,a.max_rms_ns,budget,stop,a.stabilize_timeout)
    atomic_json(a.out/'run.json',dict(pid=os.getpid(),started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),duration_s=a.duration,
                poll_s=2,snapshot_s=30,total_capture_duration_s=a.duration,stabilize_timeout_s=a.stabilize_timeout,max_rms_ns=a.max_rms_ns,max_bytes=a.max_bytes,free_reserve_bytes=a.free_reserve_bytes,
                devices={u:devices[u] for u in a.unit},receivers=a.receiver,read_only=True,gate='>=600s continuous, >=3 META boundaries, trained, advancing UTC',
                tool_sha256=__import__('hashlib').sha256(Path(__file__).read_bytes()).hexdigest()))
    results={}
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(a.unit)) as pool:
        jobs={pool.submit(capture,u,devices[u],a.out,a.duration,16*1024**2,stop,30,
                          ['status','lora','clock','precision','network'],'lora-diag',2,False,monitor.event,budget):u for u in a.unit}
        for job in concurrent.futures.as_completed(jobs):
            u=jobs[job]
            try:results[u]=job.result()
            except BaseException as e:results[u]=dict(status='INCOMPLETE',error=repr(e),closed=False)
            if results[u]['status'] not in ('COMPLETE','COMPLETE_WITH_GAPS'):stop.set()
    result=monitor.summary()
    result['captures']=results
    result['status']=('PASS_RELATIVE_RECEPTION_SOAK' if not monitor.gate_timed_out and monitor.armed_at is not None and not monitor.violations and
                      all(g.qualified for g in monitor.gates.values()) and all(v['status']=='COMPLETE' and v.get('closed') for v in results.values())
                      else 'STOPPED' if not monitor.gate_timed_out and all(v['status']=='STOPPED' for v in results.values()) else 'INCOMPLETE_OR_FAILED')
    monitor.failure.close()
    monitor.csv.close()
    atomic_json(a.out/'summary.json',result)
    print(json.dumps(dict(status=result['status'],summary=str(a.out/'summary.json'))))
    return 0 if result['status']=='PASS_RELATIVE_RECEPTION_SOAK' else 2


if __name__=='__main__':sys.exit(main())
