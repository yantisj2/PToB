#!/usr/bin/env python3
"""Paired USB shadow / cached GPIO observation on the enrolled Pi.

Copyright (c) 2026 Jonathan Yantis. MIT. Offline replay opens no hardware.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import time

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from gateways.pi5.usb.analyse import analyse,settlement_ready
from gateways.pi5.usb.contract import silicon_for_serial
from gateways.pi5.usb.calibration import DEFAULT as DEFAULT_CALIBRATION,validate as validate_calibration


def main():
    os.umask(0o077)
    p=argparse.ArgumentParser(description=__doc__)
    mode=p.add_mutually_exclusive_group(required=True)
    mode.add_argument('--capture',action='store_true')
    mode.add_argument('--analyse',type=Path)
    p.add_argument('--serial',help='full exact USB serial; display aliases are insufficient')
    p.add_argument('--seconds',type=int,default=60)
    p.add_argument('--rate',type=int,choices=(8,16,32),default=8)
    p.add_argument('--no-mfindex',action='store_true',help='IN-only coarse control without register observation')
    p.add_argument('--host-read-method',choices=('seek-read','pread','native-pread'),default='seek-read',help='separate populations for stock register read methods; native compiles a temporary ordinary user-space helper')
    p.add_argument('--realtime-bracket-method',choices=('legacy-wide','native-pair'),default='legacy-wide',help='opt-in native RAW/REALTIME/RAW sub-bracket; requires native-pread and preserves full snapshots')
    p.add_argument('--host-warmup-seconds',type=int,choices=range(0,11),default=0,help='explicit recorded host-only preparation at8Hz before the fixed capture; source warmup remains independent')
    p.add_argument('--host-filter',choices=('none','robust-host-v1','weighted-host-v2'),default='none',help='explicit experimental past-only host regression; preserves raw source/OLS diagnostics')
    p.add_argument('--markers',action='store_true',help='explicit UFA1 zero-data marker OUT/IN,1Hz; requires authorized scope')
    p.add_argument('--local-estimate-diagnostic',action='store_true',help='optional1Hz UTE1 vendor-IN local/session/root diagnostic; never source authority')
    p.add_argument('--connection',type=Path,default=Path.home()/'.config/ptob/pi5.json')
    calibration=p.add_mutually_exclusive_group()
    calibration.add_argument('--calibration',type=Path,help='frozen provisional shadow calibration JSON')
    calibration.add_argument('--no-calibration',action='store_true',help='explicitly disable derived shadow correction')
    p.add_argument('--out',required=True,type=Path,help='new ignored artifacts/ JSON path')
    args=p.parse_args();out=args.out.resolve()
    correction=None if args.no_calibration else validate_calibration(json.loads(args.calibration.read_text()) if args.calibration else DEFAULT_CALIBRATION)
    if not out.is_relative_to(ROOT/'artifacts') or out.exists() or not out.parent.is_dir():
        raise ValueError('choose an unused report in an existing ignored artifacts/ directory')
    # Freeze provenance before a long remote population. Development edits
    # during capture must not be misreported as the loaded replay version.
    sources={f.name:f.read_text() for pattern in ('*.py','*.c','*.json') for f in Path(__file__).parent.glob(pattern)}
    implementation={name:hashlib.sha256(source.encode()).hexdigest() for name,source in sources.items()}
    provenance=dict(started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),implementation=implementation,
                    public_source_snapshot=sources,calibration_configuration=correction,experimental_host_filter=args.host_filter)
    with out.with_suffix('.implementation.json').open('x') as f:json.dump(provenance,f,indent=2)
    if args.capture:
        silicon_for_serial(args.serial)
        spec=importlib.util.spec_from_file_location('pi5_remote',ROOT/'gateways/pi5/tools/remote.py')
        remote=importlib.util.module_from_spec(spec);spec.loader.exec_module(remote)
        profile=json.loads(args.connection.expanduser().read_text())
        source=(Path(__file__).with_name('contract.py')).read_text()+'\n'+(Path(__file__).with_name('live.py')).read_text()+'\nprint(json.dumps(capture(**PARAMS),allow_nan=False))\n'
        source_path=out.with_suffix('.remote.py')
        with source_path.open('x') as f:f.write(source)
        try:
            raw=remote.execute(profile,source,dict(serial=args.serial,seconds=args.seconds,rate=args.rate,mfindex=not args.no_mfindex,markers=args.markers,host_read_method=args.host_read_method,
                native_source=sources['runtime_reader.c'] if args.host_read_method=='native-pread' else None,local_estimate=args.local_estimate_diagnostic,
                realtime_bracket_method=args.realtime_bracket_method,host_warmup_seconds=args.host_warmup_seconds),
                               root=True,timeout=args.seconds+45)
        except Exception as e:
            with out.open('x') as f:json.dump(dict(remote_error=str(e),executed_source=str(source_path)),f,indent=2)
            raise
        if raw['source_sha256']!=hashlib.sha256(source.encode()).hexdigest():raise ValueError('executed source identity mismatch')
    else:
        raw=json.loads(args.analyse.read_text());raw=raw.get('raw',raw)
    # Preserve a raw report even if a replay implementation fails.
    result=dict(raw=raw,created_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
                attribution='Jonathan Yantis',observation_license='CC BY 4.0',
                evidence_status='PRIVATE unreviewed hardware observations; not UTC qualification',
                implementation=implementation)
    try:
        result['analysis']=analyse(raw,calibration=correction,host_filter=args.host_filter)
        result['settlement']=settlement_ready(raw,result['analysis'])
    except Exception as e:result['analysis_error']=dict(type=type(e).__name__,error=str(e))
    with out.open('x') as f:
        json.dump(result,f,indent=2,allow_nan=False);f.write('\n')
    summary=dict(report=str(out),samples=len(raw['samples']),capture_errors=raw['errors'],released=raw['interface_released'])
    if 'analysis' in result:
        a=result['analysis'];summary.update(attempts=a['attempts'],GPIO=a['simultaneous_GPIO'],observer=a['observer'],
                                          source_counts=a['source']['counts'],source_segments=len(a['source']['segments']),
                                          host=a['host'],lineage=a['sampled_lineage_join'])
        summary['settlement']=result.get('settlement')
    else:summary['analysis_error']=result['analysis_error']
    print(json.dumps(summary,indent=2))


if __name__=='__main__':main()
