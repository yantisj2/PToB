#!/usr/bin/env python3
"""Verify preserved common-clock evidence without hardware or credentials.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib,json,math,csv
from collections import defaultdict
from pathlib import Path
from analyze import distribution


def check_summary(path):
    r=json.loads(path.read_text())
    assert r['independent_reference'] is False
    assert r['evidence_type']=='GNSS_FREE_HARDWARE_MODEL_AND_SERVICE_OBSERVATIONS'
    assert not r['invalid_input_lines']
    assert r['correction_policy']['ble_device_input_ns']==2900
    assert r['correction_policy']['ble_incremental_ns']==0
    assert r['correction_policy']['lora_extra_ns']==3599390
    for stage in r['stages']:
        assert stage['observed_bins']+stage['missing_bins']==stage['seconds']
        assert stage['snapshots']>=stage['observed_bins']
        assert sum(stage['mode_snapshots'].values())==stage['snapshots']
        assert math.isclose(sum(stage['sampled_mode_seconds'].values()),stage['supported_seconds'],abs_tol=1e-6)
        for value in stage['grid'].values():
            assert value['nominal_opportunities']==stage['seconds']
            assert sum(value[k] for k in ['observed_available_bins','observed_unavailable_bins','missing_bins'])==stage['seconds']
        if stage['snapshots']:
            assert stage['correction_policy_samples']['ble']=={'2900':stage['snapshots']}
            assert stage['correction_policy_samples']['lora']=={'3599390':stage['snapshots']}
        for values in stage['model_disagreement'].values():
            assert values['n']<=stage['snapshots']
            assert values['max_abs_ns']>=values['p99_abs_ns']>=0
    return r


def check_aggregates(base,summary,prefix=''):
    populations=defaultdict(list)
    for row in csv.DictReader((base/(prefix+'distributions.csv')).open()):
        key=tuple(row[x] for x in ['population','stage','ble_context','lora_context','metric'])
        assert int(row['count'])>0
        populations[key].extend([int(row['value_ns'])]*int(row['count']))
    stages={s['name']:s for s in summary['stages']}
    contexts={(s['ble'],s['lora']):s for s in summary['contexts']}
    for (kind,stage,ble,lora,metric),values in populations.items():
        if kind=='context':expected=contexts[(ble,lora)]['disagreement'][metric]
        elif kind=='stage':expected=stages[stage]['model_disagreement'][metric]
        elif kind=='innovation':expected=stages[stage]['causal_innovations'][metric]
        else:expected=stages[stage]['halves'][kind][metric]
        measured=distribution(values)
        for key,value in measured.items():
            assert value is None and expected[key] is None or math.isclose(value,expected[key],rel_tol=1e-12,abs_tol=1e-8),(kind,stage,metric,key)
    availability=defaultdict(list)
    for row in csv.DictReader((base/(prefix+'availability.csv')).open()):availability[row['stage']].append(row)
    for name,s in stages.items():
        rows=availability[name];assert len(rows)==s['seconds']
        assert sum(int(row['observed_snapshots']) for row in rows)==s['snapshots']
        assert sum(int(row['observed_snapshots'])>0 for row in rows)==s['observed_bins']
        for key in ['clock_valid','authoritative','fine','ble','lora','lora_bridge']:
            assert sum(int(row[key] or '0') for row in rows)==s['grid'][key]['observed_available_bins']
            assert sum(row[key]=='' for row in rows)==s['grid'][key]['missing_bins']


def main():
    base=Path(__file__).resolve().parent
    manifest=json.loads((base/'manifest.json').read_text())
    assert manifest['status']=='PRESERVED_EVIDENCE_RESEARCH_DEFERRED'
    assert manifest['GNSS_enabled'] is False
    for name,digest in manifest['files'].items():
        path=(base/name).resolve();assert base in path.parents
        assert hashlib.sha256(path.read_bytes()).hexdigest()==digest,name
    summary=check_summary(base/'data/summary.json')
    check_aggregates(base/'data',summary)
    if (base/'data/parent-replication-summary.json').exists():
        repeated=check_summary(base/'data/parent-replication-summary.json')
        check_aggregates(base/'data',repeated,'parent-replication-')
    protocol=json.loads((base/'protocol.json').read_text())
    assert sum(s['seconds'] for s in protocol['stages'])==protocol['fixed_test_seconds']==2820
    assert protocol['warmup_seconds']==900
    print('PASS preserved evidence hashes, fixed-window denominators, fixed priors and claim boundaries')
    print('Raw extraction and independent accuracy are not verified by this command.')


if __name__=='__main__':main()
