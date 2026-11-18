#!/usr/bin/env python3
"""Replay all host brackets through causal interval models; no hardware access.

Copyright (c) 2026 Jonathan Yantis. MIT.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from gateways.pi5.usb.model import HostSeries


def analyse_host(raw):
    host=HostSeries();previous=None;boundaries=[]
    for sample in raw['samples']:
        c=sample['clocks']
        if previous is not None and abs((c['boottime_ns']-c['monotonic_ns'])-(previous['boottime_ns']-previous['monotonic_ns']))>100_000:
            boundaries.append(dict(index=sample['index'],prior_summary=host.summary()))
            host=HostSeries()
        previous=c
        for name in ('host_before','host_marker_after','host_after'):
            for r in sample.get(name,[]):host.observe(r)
    return dict(summary=host.summary(),transitions=host.transitions,prior_host_segments=boundaries,
                absolute_uncertainty_ns=None,UTC_or_PHY_bias_included=False,chrony_eligible=False)


def main():
    os.umask(0o077);p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture',type=Path);p.add_argument('--out',required=True,type=Path);args=p.parse_args()
    out=args.out.resolve()
    if not out.is_relative_to(ROOT/'artifacts') or out.exists() or not out.parent.is_dir():raise ValueError('new ignored artifacts/ output required')
    data=args.capture.read_bytes();raw=json.loads(data);raw=raw.get('raw',raw)
    result=analyse_host(raw)
    result.update(input_report_sha256=hashlib.sha256(data).hexdigest(),attribution='Jonathan Yantis',observation_license='CC BY 4.0',
                  implementation={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in [Path(__file__),Path(__file__).with_name('interval.py'),Path(__file__).with_name('model.py')]})
    with out.open('x') as f:json.dump(result,f,indent=2,allow_nan=False);f.write('\n')
    print(json.dumps(dict(report=str(out),summary=result['summary']),indent=2))


if __name__=='__main__':main()
