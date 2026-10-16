#!/usr/bin/env python3
"""MIT. Copyright (c) 2026 Jonathan Yantis.

Finite console-only background supervisor. Run explicitly after deployment.
Each reconnect is a new segment with fresh identity and ownership checks.
"""
import argparse
import concurrent.futures
import datetime
import json
import os
from pathlib import Path
import signal
import threading
import time
from console_capture import capture, validate_config


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config',type=Path,required=True)
    p.add_argument('--unit',action='append',required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--duration',type=int,default=7*86400)
    p.add_argument('--segment',type=int,default=3600)
    a=p.parse_args()
    if not 60<=a.duration<=14*86400 or not 15<=a.segment<=3600:
        p.error('duration 60s..14d; segment 15s..1h')
    devices=json.loads(a.config.read_text())['devices'];validate_config(devices)
    if len(set(a.unit))!=len(a.unit) or any(u not in devices for u in a.unit):
        p.error('distinct configured units required')
    out=a.out.resolve();root=Path(__file__).resolve().parents[3]/'artifacts'
    if root not in out.parents:p.error('background logs must stay under ignored artifacts/')
    os.umask(0o077);out.mkdir(parents=True,exist_ok=False,mode=0o700)
    stop=threading.Event()
    for sig in [signal.SIGTERM,signal.SIGINT]:signal.signal(sig,lambda *_:stop.set())
    start=time.monotonic();deadline=start+a.duration
    run=dict(pid=os.getpid(),started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
             units=a.unit,config=str(a.config.resolve()),duration_s=a.duration,segment_s=a.segment,
             hardware_actions='READ_ONLY_CONSOLE',reset_or_configuration_commands=False)
    (out/'run.json').write_text(json.dumps(run,indent=2)+'\n')

    def worker(unit):
        index=0;previous=None
        with (out/(unit+'-segments.jsonl')).open('x') as journal:
            while not stop.is_set() and deadline-time.monotonic()>=15:
                folder=out/f'{unit}-{index:06d}';folder.mkdir(mode=0o700)
                opened=time.monotonic()
                result=capture(unit,devices[unit],folder,min(a.segment,int(deadline-opened)),16*1024*1024,stop)
                summary=dict(segment=index,path=str(folder/unit),elapsed_s=time.monotonic()-start,
                             status=result['status'],closed=result['closed'],boot=result.get('boot'),
                             error=result.get('error'),gap_before_s=0 if previous is None else opened-previous)
                journal.write(json.dumps(summary)+'\n');journal.flush()
                # Atomic progress for later analysis/monitoring; no fabricated
                # healthy state is inferred from a running process.
                temp=out/(unit+'-latest.tmp');temp.write_text(json.dumps(summary,indent=2)+'\n')
                temp.replace(out/(unit+'-latest.json'))
                previous=time.monotonic();index+=1
                if not result['closed']:
                    raise RuntimeError(unit+': closure unknown; refusing another segment')
                if result['status'] not in ('COMPLETE','COMPLETE_WITH_GAPS','STOPPED'):
                    stop.wait(min(15,max(0,deadline-time.monotonic())))
        return dict(unit=unit,segments=index)

    results=[];errors=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(a.unit)) as pool:
        futures=[pool.submit(worker,u) for u in a.unit]
        # A later unit can fail while an earlier one is still collecting.
        # Observe whichever finishes first so unknown closure stops the entire
        # owned cohort promptly, rather than waiting for its seven-day peer.
        for future in concurrent.futures.as_completed(futures):
            try:results.append(future.result())
            except BaseException as e:errors.append(repr(e));stop.set()
    run.update(status='INCOMPLETE' if errors else 'STOPPED' if stop.is_set() else 'COMPLETE',
               finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
               results=results,errors=errors)
    (out/'finished.json').write_text(json.dumps(run,indent=2)+'\n')
    return 1 if errors else 0


if __name__=='__main__':raise SystemExit(main())
