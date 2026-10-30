#!/usr/bin/env python3
"""Offline public numerical/identity verifier. No hardware or private inputs.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from build_analysis import recompute, summarize, BASE
from duration_replay import quantile, rounded
from write_values import make_tex
from dataset import dataset, COLLECTION

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--unsealed',action='store_true');args=parser.parse_args()
    data=dataset()
    prepared=json.loads((BASE/'edition.json').read_text())
    identities={f['path']:f for f in prepared['files']}
    with tempfile.TemporaryDirectory(prefix='mcxo-replay-') as work:
        actual=recompute(True,Path(work))
        for p in Path(work).rglob('*'):
            name=p.relative_to(work).as_posix()
            if p.is_file() and name in identities:
                assert digest(p)==identities[name]['sha256'],('Regenerated output differs',name)
    encoded=(json.dumps(actual,indent=2)+'\n').encode()
    assert hashlib.sha256(encoded).hexdigest()==identities['analysis.json']['sha256'],'Analysis differs from approved numerical replay'
    assert summarize(actual)==json.loads((COLLECTION/'summary.json').read_text()),'Compact summary differs'
    assert (BASE/'results.tex').read_text()==make_tex(actual),'Manuscript result text differs from supplied populations'
    for p in actual['populations']:
        assert p['missing_pair_records']==0 and p['max_numeric_difference_ns']==0
        assert p['max_active_runs']<=48
        assert p['independent_runs'] is False and p['continuous_error_bound'] is False
    code=data.json('reviewed-code/manifest.json')
    for f in code['files']:assert hashlib.sha256(data.read_bytes(f['snapshot'])).hexdigest()==f['sha256']
    witnesses=list(csv.DictReader(io.StringIO(data.read_text('evidence/selected-output-witnesses.csv'))))
    audit=data.json('evidence/secondary-audit.json')
    assert len(witnesses)==5234
    for duration,n,p99,maximum in [(12000,728,166,228),(24000,1502,295,364),(48000,3004,845,973)]:
        e=[abs(int(x['error_ns'])) for x in witnesses if int(x['command_ms'])==duration]
        assert (len(e),quantile(e,99),max(e))==(n,p99,maximum)
    trials=audit['selected_output']['trials'];assert len(trials)==18
    assert sum(x['gaps_above250ms'] for x in trials)==197
    assert round(max(x['maximum_gap_s'] for x in trials),3)==2.125
    assert all(x['activation_verified'] and x['causal_rate_replay'] and x['resume_zero_step'] and x['missing_log_records']==0 for x in trials)
    assert [sum((x['p99_ns']<x['baseline_p99_ns'],x['p99_ns']>x['baseline_p99_ns'],x['p99_ns']==x['baseline_p99_ns'])[i] for x in trials) for i in range(3)]==[4,7,7]
    records=[json.loads(x) for x in data.read_text('evidence/C1-normal.jsonl').splitlines()]
    starts={x['trial']:x for x in records if x['kind']=='START'};pairs=[x for x in records if x['kind']=='PAIR']
    counterfactual=data.json('evidence/expiry-counterfactual.json')
    for x in counterfactual['runs']:
        s=starts[x['trial']];w=next(p for p in pairs if p['local20']-s['start20']>=s['probe_ms']*20000)
        peak=0
        for p in pairs:
            if not s['start20']<p['local20']<=w['local20']:continue
            dt=p['local20']-s['start20'];t=dt/20000000;phase=s['phase_ns']
            predicted=s['epoch_ns']+50*dt+rounded(t*s['rate']+math.copysign(min(abs(phase),1000*t),phase))
            peak=max(peak,abs(predicted-p['reference_ns']))
        assert peak==x['prefix_peak_ns'] and x['used_actual_witness'] and not x['interpolation']
        assert (w['available20']-s['start20'])/20000000==x['first_later_witness_available_age_s']<5
    forbidden=['/Users/','44:b1:76:','aa3dd8934332f131','r2_enrollment_sha256','psk','LAB_READY']
    for name in data.files:
        assert not any(x in data.read_text(name) for x in forbidden),('Private/unselected archive field',name)
        assert data.files[name]['sha256']==identities[name]['sha256'],('Original selected input changed',name)
    for p in [*BASE.rglob('*'),*COLLECTION.iterdir()]:
        if not p.is_file() or p.suffix in ('.pdf','.pyc') or p.name=='verify.py':continue
        if p.suffix=='.zip':continue
        text=p.read_text()
        assert not any(x in text for x in forbidden),('Private/unselected field',p)
    if not args.unsealed:
        seal=json.loads((BASE/'publication.json').read_text())
        assert seal['status']=='APPROVED_RESEARCH_DRAFT_GITHUB_PREPARATION'
        repo=BASE.parents[1]
        for f in seal['files']:
            p=(repo/f['path']).resolve();assert p.is_relative_to(repo) and p.stat().st_size==f['bytes'] and digest(p)==f['sha256'],('Publication identity',p)
        assert seal['prepared_edition_sha256']==digest(BASE/'edition.json')
        for name in ('main.tex','main.pdf','results.tex'):
            assert digest(BASE/name)==identities[name]['sha256'],('Approved manuscript changed',name)
        assert 5<=prepared['review']['pages']<=7 and prepared['review']['all_pages_visually_inspected']
    subprocess.run([sys.executable,str(BASE/'tools/test_duration_replay.py')],check=True)
    subprocess.run([sys.executable,str(BASE/'tools/test_dataset.py')],check=True)
    print('PASS: numerical prefixes, independent per-age populations, exact status replay, secondary separation, source identity, publication selection'+(' (unsealed)' if args.unsealed else ', sealed edition'))
if __name__=='__main__':main()
