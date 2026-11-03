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

CSV_DISPLAY = ('display_available','display_held','display_utc_us','display_sampled_us','display_ref_age_ms','display_acquiring')

CSV_EXTRA = ('sent_disc','sent_meta','sent_sync','sent_follow',
             'received_disc','received_meta','received_sync','received_follow',
             'rejected','missing','late','refused','bad','missing_map','storage_fail',
             'retries','owed','charged_us','tx_gate','pending','fh_disabled',
             'hw_crc_bad','hw_overflow','hw_spi_error','hw_cutoffs','hw_invalid_capture',
             'fault','error','requested','active','transmitter','time_valid',
             'manual_jobs','manual_active','manual_serial','manual_stream')+CSV_DISPLAY


def tx_error_counters(p):
    # These are cumulative error counters, not the full LoRa hardware phase
    # array. Charge, retries, pending and owed may legitimately change normally.
    return tuple(p[k] for k in ('late','storage_fail','missing_map'))+tuple(p['hw_errors'])


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

    def observe(self, p, host_ns, transmitter=None):
        if p.get('diag')!=1 or p.get('profile') not in ('PROD_60_300','RAM_60_300','RAM_30_120_300'):
            raise ValueError('slow diagnostic profile required')
        # Current firmware supplies its original observation lease and normal
        # physical-event cadence bound. Older slow profiles retain62s/362s.
        enhanced=p.get('profile')=='RAM_30_120_300'
        beat=p.get('beat_us',30000000 if enhanced else 60000000)
        hold=p.get('hold_us',62000000)
        lease=p.get('lease_us',362000000)
        cadence=p.get('cadence_bound_us',62000000)
        if (any(type(x) is not int for x in (beat,hold,lease,cadence)) or
            beat!=(30000000 if enhanced else 60000000) or not beat<=cadence<=hold or lease<300000000 or
            (enhanced and (p.get('meta_us')!=300000000 or p.get('discovery_us')!=120000000))):
            raise ValueError('incoherent slow diagnostic timing bounds')
        self.samples+=1
        stamp=host_ns/1e9
        context=tuple(p[k] for k in ('profile','boot','emitter','emitter_boot','stream','source_epoch','capture_epoch'))
        counters=tuple(p[k] for k in ('accepted','rejected','missing','late','bad','missing_map','storage_fail'))+tuple(p['hw_errors'])
        last=self.previous
        reason=None
        if last:
            old, old_stamp, old_context, old_counters=last
            if stamp<=old_stamp or stamp-old_stamp>self.gap_s:reason='host capture gap'
            elif p['at_us']<=old['at_us']:reason='device monotonic discontinuity'
            elif context!=old_context:reason='source or capture context changed'
            elif any(p.get(k,d)!=old.get(k,d) for k,d in (('beat_us',60000000),('hold_us',62000000),('lease_us',362000000),('cadence_bound_us',62000000))):reason='profile timing bounds changed'
            elif p['serial']==old['serial'] and p['meta_until_us']!=old['meta_until_us']:reason='META lease changed without new META'
            elif p['accepted']==old['accepted'] and any(p[k]!=old[k] for k in ('event_ns','event_tick','event_mono_us','physical_until_us')):reason='physical lease/event changed without new pair'
            elif any(a<b for a,b in zip(counters,old_counters)):reason='counter reset'
            elif any(a>b for a,b in zip(counters[1:],old_counters[1:])):reason='reception/timing error counter advanced'
            elif p['accepted']>old['accepted']:
                count=p['accepted']-old['accepted']
                self.pairs+=count
                if p['event_ns']<=old['event_ns'] or p['event_tick']<=old['event_tick']:reason='UTC/physical event did not advance'
                elif count!=1:reason='unobserved accepted pair(s)'
                elif old['event_mono_us'] and p['event_mono_us']-old['event_mono_us']>cadence:reason='precision cadence gap'
                else:
                    nominal=(p['event_tick']-old['event_tick'])*62.5
                    if abs(p['event_ns']-old['event_ns']-nominal)>max(1000000,nominal*.0002):reason='UTC/physical event discontinuity'
            if p['serial']<old['serial']:reason='META serial regressed'
        if 'manual_jobs' in p or (transmitter is not None and 'manual_jobs' in transmitter):
            # Firmware exposing requests needs a fresh matching TX observation.
            # An RX-only run cannot know that a job completed between polls.
            if (transmitter is None or not transmitter.get('transmitter') or
                transmitter.get('boot')!=p['emitter_boot'] or transmitter.get('emitter')!=p['emitter'] or
                'manual_jobs' not in transmitter):
                reason='matching UTC-job transmitter observation unavailable'
            elif transmitter['manual_active']:
                reason='lab UTC request active'
            elif (transmitter['manual_serial'] and transmitter['manual_stream']==p['stream'] and
                  p['serial']<=transmitter['manual_serial']):
                reason='lab UTC META influence; waiting for ordinary META'
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
            self.meta_origin=p['meta_until_us']-lease
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
            p['meta_until_us']-lease-self.meta_origin>=600000000 and
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

    def fresh(self, host_ns):
        return self.previous is not None and 0<=host_ns/1e9-self.previous[1]<=self.gap_s


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
    def __init__(self, out, units, receivers, rms, budget, stop, stabilize_timeout=7200, qualify_after_seconds=0):
        self.out,self.stop,self.budget=out,stop,budget
        self.gates={u:Gate(rms) for u in receivers}
        self.units={u:dict(replies=0) for u in units}
        self.lock=threading.Lock()
        self.armed_at=None
        self.violations=0
        self.time_zones={u:dict(supported_samples=0,unsupported_samples=0,
                               known_samples=0,unknown_samples=0,cached_samples=0,
                               zone_changes=0,forgotten_samples=0,boot_changes=0,
                               boot=None,last_zone=None,learned=False) for u in units}
        self.display={u:dict(supported_samples=0,unsupported_samples=0,visible_samples=0,
                            fresh_samples=0,held_samples=0,acquisition_samples=0,unavailable_samples=0,
                            advancing_intervals=0,nonadvancing_intervals=0,backward_intervals=0,
                            host_gap_intervals=0,held_context_changes=0,acquisition_context_changes=0,max_hold_reference_age_ms=None,max_acquisition_reference_age_ms=None) for u in receivers}
        self.display_previous={}
        self.failure=(out/'failures.jsonl').open('x')
        self.csv=(out/'samples.csv').open('x')
        self.csv.write('host_mono_ns,unit,at_us,emitter,serial,cursor,accepted,trained,fit_n,rms_ns,event_ns,physical_until_us,meta_until_us,'+','.join(CSV_EXTRA)+'\n')
        self.last_summary=0
        started=time.monotonic()
        self.qualify_after_seconds=qualify_after_seconds
        self.qualify_at=started+qualify_after_seconds
        self.gate_deadline=started+stabilize_timeout
        self.gate_timed_out=False

    def observe_time_zone(self, unit, p):
        stats=self.time_zones[unit]
        tz=p.get('tz')
        if not isinstance(tz,dict) or 'display_flags' not in tz:
            stats['unsupported_samples']+=1
            return 'drawn TZ diagnostic disappeared' if stats['supported_samples'] else None
        zone=(tz.get('id'),tz.get('offset_min'))
        flags=tz['display_flags']
        integers=all(type(x) is int for x in zone)
        known=(integers and 0<zone[0]<=65535 and
               -840<=zone[1]<=840 and (zone[0]!=1 or zone[1]==0))
        unknown=integers and zone==(0,-32768)
        if type(flags) is not int or not 0<=flags<=255 or not (known or unknown):
            return 'malformed drawn TZ diagnostic'
        boot=p.get('boot')
        if stats['boot'] is not None and boot!=stats['boot']:
            stats['boot_changes']+=1;stats['learned']=False;stats['last_zone']=None
        stats['boot']=boot;stats['supported_samples']+=1
        stats['known_samples' if known else 'unknown_samples']+=1
        if known:
            if stats['last_zone'] is not None and tuple(stats['last_zone'])!=zone:stats['zone_changes']+=1
            stats['learned']=True;stats['last_zone']=list(zone)
            if flags&4:stats['cached_samples']+=1
        elif stats['learned']:
            stats['forgotten_samples']+=1
            return 'learned display TZ forgotten within boot'
        return None

    def observe_display(self, unit, p, host_ns):
        if unit not in self.display:return
        stats=self.display[unit]
        if not all(k in p for k in CSV_DISPLAY[:-1]):
            stats['unsupported_samples']+=1;self.display_previous.pop(unit,None);return
        stats['supported_samples']+=1
        visible=bool(p['display_available'] and p['display_utc_us'])
        stats['visible_samples' if visible else 'unavailable_samples']+=1
        if visible:
            acquiring=bool(p.get('display_acquiring'))
            stats['acquisition_samples' if acquiring else 'held_samples' if p['display_held'] else 'fresh_samples']+=1
            if acquiring or p['display_held']:
                key='max_acquisition_reference_age_ms' if acquiring else 'max_hold_reference_age_ms'
                age=p['display_ref_age_ms'];old=stats[key]
                stats[key]=age if old is None else max(old,age)
        previous=self.display_previous.get(unit)
        if previous and previous[0]['display_available'] and visible:
            old,old_host=previous
            if not 0<host_ns-old_host<=10000000000:
                stats['host_gap_intervals']+=1
            elif p['boot']==old['boot'] and p['display_sampled_us']>old['display_sampled_us']:
                change=p['display_utc_us']-old['display_utc_us']
                stats['advancing_intervals' if change>0 else 'nonadvancing_intervals' if change==0 else 'backward_intervals']+=1
            context=('emitter','emitter_boot','stream','source_epoch','capture_epoch')
            if (p['display_held'] or p.get('display_acquiring')) and any(p.get(k)!=old.get(k) for k in context):
                stats['acquisition_context_changes' if p.get('display_acquiring') else 'held_context_changes']+=1
        self.display_previous[unit]=(dict(p),host_ns)

    def expire_stale_receivers(self, host_ns):
        for unit,gate in self.gates.items():
            if gate.qualified and not gate.fresh(host_ns):
                reason='qualified receiver capture stale'
                gate.reset(reason)
                if self.armed_at is not None:self.violations+=1
                line=json.dumps(dict(unit=unit,host_mono_ns=host_ns,reason=reason,after_gate=self.armed_at is not None))+'\n'
                self.budget(len(line));self.failure.write(line);self.failure.flush()

    def finish(self, host_ns):
        with self.lock:
            self.expire_stale_receivers(host_ns)
            return self.summary()

    def event(self, unit, entry):
        with self.lock:
            reason=None
            if self.armed_at is None and not self.gate_timed_out and time.monotonic()>=self.gate_deadline:
                self.gate_timed_out=True;self.stop.set();reason='initial stability gate timed out'
            if entry['kind'] in ('query_timeout','frame_refused','capture_error','partial_tail'):
                reason=entry['kind']
                if unit in self.gates:self.gates[unit].reset(reason)
            if entry['kind']=='reply' and entry['op']=='status':
                reason=self.observe_time_zone(unit,entry['payload']) or reason
                if reason:
                    for gate in self.gates.values():gate.reset(reason)
            if entry['kind']=='reply' and entry['op']=='lora-diag':
                p=entry['payload']
                self.observe_display(unit,p,entry['host_mono_ns'])
                self.units[unit]=dict(replies=self.units[unit]['replies']+1, latest=p)
                if unit in self.gates:
                    tx=None
                    for name,candidate in getattr(self,'tx_previous',{}).items():
                        age=entry['host_mono_ns']-self.tx_host_ns[name]
                        if (candidate.get('boot')==p['emitter_boot'] and candidate.get('emitter')==p['emitter'] and
                            0<=age<=int(self.gates[unit].gap_s*1e9)):
                            tx=candidate;break
                    reason=self.gates[unit].observe(p, entry['host_mono_ns'],tx) or reason
                else:
                    previous=getattr(self,'tx_previous',{}).get(unit)
                    if p.get('fault') or p.get('error'):reason='transmitter fault'
                    # First reply establishes the retained baseline; subsequent
                    # resets cannot erase a post-gate error or start a new run.
                    if previous:
                        counters, old=tx_error_counters(p),tx_error_counters(previous)
                        if len(counters)!=len(old) or any(a<b for a,b in zip(counters,old)):
                            reason='transmitter error counter regressed'
                        elif any(a>b for a,b in zip(counters,old)):
                            reason='transmitter timing/storage/hardware error counter advanced'
                    if reason:
                        # A known TX failure breaks pre-SOAK continuity too.
                        # Only receivers following this emitter are affected.
                        for gate in self.gates.values():
                            if gate.previous and gate.previous[0]['emitter']==p['emitter']:
                                gate.reset(reason)
                    if previous and 'manual_jobs' in p and p['manual_jobs']!=previous.get('manual_jobs'):
                        reason='lab UTC admission counter changed; certification window invalidated'
                        for gate in self.gates.values():
                            if gate.previous and gate.previous[0]['emitter']==p['emitter']:
                                gate.reset(reason)
                    if not hasattr(self,'tx_previous'):self.tx_previous={};self.tx_host_ns={}
                    self.tx_previous[unit]=p;self.tx_host_ns[unit]=entry['host_mono_ns']
                extra=tuple(p.get('sent',[0]*4))+tuple(p.get('received',[0]*4))+tuple(p.get(k,0) for k in CSV_EXTRA[8:21])+tuple(p['hw_errors'])+tuple(int(p.get(k,0)) for k in CSV_EXTRA[26:-len(CSV_DISPLAY)])+tuple('' if k not in p else int(p[k]) for k in CSV_DISPLAY)
                row=f'{entry["host_mono_ns"]},{unit},{p["at_us"]},{p["emitter"]},{p["serial"]},{p["cursor"]},{p["accepted"]},{int(p["trained"])},{p["link_fit"][0]},{p["link_fit"][1]},{p["event_ns"]},{p["physical_until_us"]},{p["meta_until_us"]},'+','.join(map(str,extra))+'\n'
                self.budget(len(row))
                self.csv.write(row)
                self.csv.flush()
            if reason:
                if self.armed_at is not None:self.violations+=1
                line=json.dumps(dict(unit=unit, host_mono_ns=entry['host_mono_ns'], reason=reason, after_gate=self.armed_at is not None))+'\n'
                self.budget(len(line))
                self.failure.write(line)
                self.failure.flush()
            if self.armed_at is None and not self.gate_timed_out and time.monotonic()>=self.qualify_at:
                self.expire_stale_receivers(entry['host_mono_ns'])
                zones_ready=all(not s['supported_samples'] or s['learned'] for s in self.time_zones.values())
                if zones_ready and all(g.qualified for g in self.gates.values()):self.armed_at=entry['host_mono_ns']
            if time.monotonic()-self.last_summary>=5:
                atomic_json(self.out/'live.json', self.summary())
                self.last_summary=time.monotonic()

    def summary(self):
        hold_remaining=max(0,self.qualify_at-time.monotonic())
        return dict(phase='SOAK' if self.armed_at is not None else 'STABILIZING',
                    initial_gate_host_mono_ns=self.armed_at, post_gate_violations=self.violations,
                    initial_gate_timed_out=self.gate_timed_out,
                    qualify_after_seconds=self.qualify_after_seconds,
                    qualification_hold_remaining_s=hold_remaining,
                    qualification_hold_active=hold_remaining>0,
                    qualification_hold_reason='host warmup before certification; acquisition/migration evidence retained' if hold_remaining>0 else None,
                    qualification_scope='Host qualification hold and startup/migration samples retained before SOAK; no firmware timing change, debt reset or aging credit.',
                    receivers={u:g.summary() for u,g in self.gates.items()}, units=self.units,
                    time_zone_durability=self.time_zones,
                    time_zone_scope='Existing30s status samples of boot-local drawn TZ retention; no timing/export authority or extra queries.',
                    display_durability=self.display,display_scope='Sampled presentation availability/progression only; ACQ/HOLD have unknown uncertainty and never qualify fresh service.',
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
    p.add_argument('--qualify-after-seconds',type=int,default=0,help='host capture warmup excluded from certification; normal current receiver gate still required')
    p.add_argument('--max-bytes',type=int,default=2*1024**3)
    p.add_argument('--free-reserve-bytes',type=int,default=1024**3)
    p.add_argument('--detach',action='store_true')
    a=p.parse_args()
    if not 620<=a.duration<=14*86400 or not 600<=a.stabilize_timeout<=14*86400 or a.max_rms_ns<=0 or a.max_bytes<16*1024**2 or a.free_reserve_bytes<16*1024**2:p.error('invalid bounded run limits')
    if not 0<=a.qualify_after_seconds<min(a.duration,a.stabilize_timeout):p.error('qualification hold must end before capture and stabilization deadlines')
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
    monitor=Monitor(a.out,a.unit,a.receiver,a.max_rms_ns,budget,stop,a.stabilize_timeout,a.qualify_after_seconds)
    atomic_json(a.out/'run.json',dict(pid=os.getpid(),started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),duration_s=a.duration,
                poll_s=2,snapshot_s=30,total_capture_duration_s=a.duration,stabilize_timeout_s=a.stabilize_timeout,max_rms_ns=a.max_rms_ns,max_bytes=a.max_bytes,free_reserve_bytes=a.free_reserve_bytes,
                qualify_after_seconds=a.qualify_after_seconds,qualification_scope='All pre-SOAK acquisition/migration samples and failures retained; host hold does not change firmware debt or timers.',
                devices={u:devices[u] for u in a.unit},receivers=a.receiver,read_only=True,gate='>=600s continuous, >=3 META boundaries, trained, advancing UTC',
                tool_sha256=__import__('hashlib').sha256(Path(__file__).read_bytes()).hexdigest()))
    results={}
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(a.unit)) as pool:
        jobs={pool.submit(capture,u,devices[u],a.out,a.duration,16*1024**2,stop,30,
                          ['status','lora','clock','precision','network'],'lora-diag',2,False,monitor.event,budget,True):u for u in a.unit}
        for job in concurrent.futures.as_completed(jobs):
            u=jobs[job]
            try:results[u]=job.result()
            except BaseException as e:results[u]=dict(status='INCOMPLETE',error=repr(e),closed=False)
            if results[u]['status'] not in ('COMPLETE','COMPLETE_WITH_GAPS'):stop.set()
    result=monitor.finish(time.monotonic_ns())
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
