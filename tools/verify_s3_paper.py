#!/usr/bin/env python3
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""Recompute selected derived samples; no device, private path or credentials."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


def metrics(values):
    n = len(values)
    if not n:
        return {'n': 0}
    mean = math.fsum(values) / n
    squares = math.fsum(x*x for x in values) / n
    ordered = sorted(abs(x) for x in values)
    return dict(n=n, mean_ns=mean, rms_ns=math.sqrt(squares),
                std_ns=math.sqrt(max(0, squares-mean*mean)),
                min_ns=min(values), max_ns=max(values), max_abs_ns=max(ordered),
                abs_p50_ns=ordered[math.ceil(.5*n)-1],
                abs_p95_ns=ordered[math.ceil(.95*n)-1],
                abs_p99_ns=ordered[math.ceil(.99*n)-1])


def compute(root):
    evidence = root / 'evidence/s3/2026-10-22'
    with (evidence / 'pps-sanitized.csv').open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    cohorts = {}
    for name in sorted({r['cohort'] for r in rows}):
        selected = [r for r in rows if r['cohort'] == name]
        ordinals = [int(r['ordinal']) for r in selected]
        if ordinals != list(range(1, len(selected)+1)):
            raise ValueError(f'{name}: ordinal sequence is not complete')
        for row in selected:
            flags = ('valid','diagnostic_enabled','map_valid','reference_seen','reference_fix','reference_pps','same_scope')
            if any(row[k] not in ('0','1') for k in flags):
                raise ValueError(f'{name}: unsupported flag')
            eligible = row['reason']=='0' and all(row[k]=='1' for k in flags[1:])
            if (row['valid']=='1') != eligible:
                raise ValueError(f'{name}: inconsistent eligibility flags')
        valid = [r for r in selected if r['valid'] == '1']
        errors = [int(r['phase_ns']) for r in valid]
        seq = [int(r['sequence_from_first']) for r in selected]
        if len(seq) != len(set(seq)):
            raise ValueError(f'{name}: duplicate sequence')
        ticks = [int(r['relative_edge_ticks16m']) for r in selected]
        if any(b <= a for a,b in zip(ticks,ticks[1:])):
            raise ValueError(f'{name}: non-increasing physical edge time')
        if any(not math.isclose(float(r['relative_edge_s']),int(r['relative_edge_ticks16m'])/16000000,abs_tol=1e-10) for r in selected):
            raise ValueError(f'{name}: counter scale mismatch')
        cohorts[name] = dict(recorded=len(selected), valid=len(valid), invalid=len(selected)-len(valid),
            interior_missing=sum(max(0, b-a-1) for a, b in zip(seq, seq[1:])),
            edge_span_s=(ticks[-1]-ticks[0])/16000000,
            **metrics(errors))
        if name == 'actual46993_fixed660s':
            if any(t<0 or t>=660*16000000 for t in ticks):
                raise ValueError('Long-cohort edge outside immutable native-counter window')
            blocks=[]
            for block in range(11):
                within=[r for r in selected if block*60*16000000<=int(r['relative_edge_ticks16m'])<(block+1)*60*16000000]
                values=[int(r['phase_ns']) for r in within if r['valid']=='1']
                blocks.append(dict(block=block,start_s=block*60,end_s=(block+1)*60,
                    recorded=len(within),invalid=sum(r['valid']!='1' for r in within),**metrics(values)))
            pairs=[(int(a['phase_ns']),int(b['phase_ns'])) for a,b in zip(selected,selected[1:])
                if a['valid']==b['valid']=='1' and int(b['sequence_from_first'])==int(a['sequence_from_first'])+1]
            corr=None
            if len(pairs)>1:
                left=math.fsum(a for a,b in pairs)/len(pairs)
                right=math.fsum(b for a,b in pairs)/len(pairs)
                denominator=math.sqrt(math.fsum((a-left)**2 for a,b in pairs)*math.fsum((b-right)**2 for a,b in pairs))
                if denominator:
                    corr=math.fsum((a-left)*(b-right) for a,b in pairs)/denominator
            cohorts[name].update(native_counter_window_s=660,minute_blocks=blocks,
                adjacent_valid_pairs=len(pairs),lag1_pearson=corr,
                valid_edge_span_s=(int(valid[-1]['relative_edge_ticks16m'])-int(valid[0]['relative_edge_ticks16m']))/16000000 if valid else None,
                first_recorded_edge_elapsed_s=ticks[0]/16000000,last_recorded_edge_elapsed_s=ticks[-1]/16000000,
                dependence_note='Descriptive serial dependence; no iid confidence interval or independence assumed.')
    if 'nominal40000_check32' in cohorts:
        errors = [int(r['phase_ns'])+6965 for r in rows if r['cohort']=='nominal40000_check32' and r['valid']=='1']
        cohorts['counterfactual46965_on_check32'] = metrics(errors)
    calibration = cohorts['nominal40000_train32']
    if -round(calibration['mean_ns']) != 6965:
        raise AssertionError('Training32 no longer selects6965ns')
    if 46965-round(cohorts['actual46965_training48']['mean_ns']) != 46993:
        raise AssertionError('Training48 no longer selects46993ns')
    with (evidence / 'internal-quality-sanitized.csv').open(newline='') as stream:
        internal_rows = list(csv.DictReader(stream))
    internal = {}
    for name in sorted({r['cohort'] for r in internal_rows}):
        selected = [r for r in internal_rows if r['cohort'] == name]
        values = [int(r['rolling_causal_rms_ns']) for r in selected]
        if name=='actual46993_fixed660s_in_window' and any(not(0<=int(r['bracket_before_elapsed_ticks16m'])<=int(r['bracket_after_elapsed_ticks16m'])<660*16000000) for r in selected):
            raise ValueError('Internal quality snapshot not bracketed inside fixed window')
        internal[name] = dict(sampled_snapshots=len(selected),
            rolling_causal_rms_min_ns=min(values),rolling_causal_rms_max_ns=max(values),
            states={state:sum(r['state']==state for r in selected) for state in sorted({r['state'] for r in selected})},
            scope_mismatch=sum(r['same_scope']!='1' for r in selected),
            meaning='Sampled recent-history causal quality; not an independent PPS error or a whole-run RMS')
    historical = json.loads((evidence / 'historical-nordic.json').read_text())
    if historical['nominal_boundaries'] != historical['reference_valid_causal']['n']+historical['invalid_reference']+historical['reference_valid_expired']+historical['unobserved_conservative']:
        raise AssertionError('Historical Nordic opportunity denominator')
    if historical['observed_edges'] != historical['all_causal']['n']+historical['reference_valid_expired']:
        raise AssertionError('Historical Nordic observed denominator')
    chain = json.loads((evidence / 'capture-chain.json').read_text())
    native = chain['native_RW']
    if native['callbacks'] != native['RX_callbacks']+native['TX_callbacks'] or native['callbacks'] != native['exported_unique']+native['interior_missing']+native['terminal_unread']:
        raise AssertionError('Native callback/export denominator')
    if sum(chain['synthetic']['raw80_mod5'].values()) != chain['synthetic']['unique']:
        raise AssertionError('Synthetic grid denominator')
    if chain['wrap_s'] != 2**32/chain['actual_counter_hz'] or chain['half_wrap_s'] != chain['wrap_s']/2:
        raise AssertionError('Counter wrap scale')
    return dict(schema=1, author='Jonathan Yantis', evidence_license='CC-BY-4.0',
                quantiles='nearest-rank of absolute residuals; no demeaning or detrending',
                cohorts=cohorts,internal_quality=internal)


def compare(expected, actual, path=''):
    if isinstance(expected, dict):
        for key, value in expected.items():
            if key not in actual:
                raise AssertionError(f'missing {path}/{key}')
            compare(value, actual[key], f'{path}/{key}')
    elif isinstance(expected, float):
        if not math.isclose(expected, actual, rel_tol=1e-10, abs_tol=1e-7):
            raise AssertionError(f'{path}: {expected} != {actual}')
    elif expected != actual:
        raise AssertionError(f'{path}: {expected} != {actual}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--write', action='store_true', help='regenerate derived summary')
    parser.add_argument('--require-long-cohort', action='store_true')
    args = parser.parse_args()
    evidence = args.root / 'evidence/s3/2026-10-22'
    result = compute(args.root)
    protocol = json.loads((evidence / 'protocol.json').read_text())
    for name in ('actual46965_training48','actual46993_heldout48'):
        expected=protocol[name]
        measured=result['cohorts'][name]
        for field in ('recorded','valid','invalid'):
            if expected[field] != measured[field]:
                raise AssertionError(f'{name}: protocol {field}')
        if expected['missing'] != measured['interior_missing']:
            raise AssertionError(f'{name}: protocol interior missing')
    nominal=protocol['nominal_calibration']
    if nominal['recorded'] != result['cohorts']['nominal40000_train32']['recorded']+result['cohorts']['nominal40000_check32']['recorded']:
        raise AssertionError('Nominal calibration protocol denominator')
    if args.require_long_cohort:
        long = result['cohorts'].get('actual46993_fixed660s')
        if not long or long['edge_span_s'] <= 600:
            raise ValueError('A >600 s fixed prospective cohort is required')
        fixed=protocol['new660s']
        if fixed['status']!='FAILED' or fixed['post_window_sequence_fence'] or fixed['complete_window_native_record_population']:
            raise AssertionError('Original failed/incomplete outcome was not preserved')
        for field in ('recorded','valid','invalid'):
            if long[field]!=fixed[field]:
                raise AssertionError(f'Fixed660 protocol {field}')
        if fixed['nominal_1Hz_opportunities']!=long['valid']+long['invalid']+fixed['nominal_unrecorded_opportunities']:
            raise AssertionError('Fixed660 nominal opportunity denominator')
        if not math.isclose(long['valid_edge_span_s'],fixed['valid_recorded_edge_span_s'],abs_tol=1e-10):
            raise AssertionError('Fixed660 valid edge span')
        if fixed['total_shadow_responses']!=fixed['recorded']+fixed['empty_polls']+fixed['duplicates']:
            raise AssertionError('Fixed660 shadow response denominator')
    summary = evidence / 'results.json'
    if args.write:
        summary.write_text(json.dumps(result, indent=2, sort_keys=True)+'\n')
    else:
        compare(json.loads(summary.read_text()), result)
    manifest = evidence / 'manifest.json'
    if manifest.exists():
        for record in json.loads(manifest.read_text())['files']:
            path = (args.root / record['path']).resolve()
            if not path.is_relative_to(args.root.resolve()):
                raise AssertionError('Manifest path escapes root')
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if digest != record['sha256']:
                raise AssertionError(f'Hash mismatch: {record["path"]}')
    edition=args.root/'papers/editions/s3-timing-2026-10-22.json'
    if edition.exists():
        for record in json.loads(edition.read_text())['files']:
            path=(args.root/record['path']).resolve()
            if not path.is_relative_to(args.root.resolve()):
                raise AssertionError('Edition path escapes root')
            blob=path.read_bytes()
            if len(blob)!=record['bytes'] or hashlib.sha256(blob).hexdigest()!=record['sha256']:
                raise AssertionError(f'Edition identity mismatch: {record["path"]}')
        source=(args.root/'papers/ptob-esp32s3-timing-preprint.tex').read_text()
        if args.require_long_cohort and any(term in source for term in ('@@MACROS@@','Results pending','awaiting collection','methods version')):
            raise AssertionError('Final paper still contains a pending methods placeholder')
    report={name:{k:row[k] for k in ('n','recorded','valid','invalid','mean_ns','rms_ns','max_abs_ns') if k in row}
            for name,row in result['cohorts'].items()}
    print(json.dumps({'status':'PASS', 'cohorts':report}, sort_keys=True))


if __name__ == '__main__':
    main()
