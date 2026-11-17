#!/usr/bin/env python3
"""Offline preparation/analysis for the T-Watch dual-radio PPS campaign.

Never imports a serial driver, opens a device, changes a clock or applies a
calibration. Input records must come from the reviewed parallel-PPS observer;
ordinary display snapshots are not sufficient measurement records.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import subprocess

CONTRACT = 'TWATCH_DUAL_RADIO_PPS_V1'
COMMON_CLOCK = 'TWATCH_COMMON_CLOCK_V1'
MODES = ('BLE_FINE', 'LORA_COARSE', 'HOLD', 'UNSYNCED')
PHASES = (
    ('parallel_train', 1200, 'BOTH', 'Measure both independent receiver models; no live GNSS steering'),
    ('parallel_validate', 1200, 'BOTH', 'After a reviewed correction image and fresh GNSS reacquisition; held-out data'),
    ('shared_clock_ble', 300, 'BOTH', 'Only after both offsets are confirmed; establish the common clock with BLE'),
    ('ble_loss_lora', 900, 'LORA_ONLY', 'Keep the same clock instance; LoRa supplies coarse discipline'),
    ('ble_return', 300, 'BOTH', 'Fresh admission and stable return dwell; slew the same clock back to BLE fine'),
    ('both_loss_holdover', 600, 'BOTH_OFF', 'Keep the trained clock running; observe 1s/10s/60s/300s/600s holdover'),
    ('holdover_return', 300, 'BOTH', 'Observe reacquisition without restarting the running clock'),
    ('repeated_handoffs', 600, 'SCRIPTED', 'Two cycles: BOTH/LoRa-only/BOTH/both-off/BOTH, 60s each'),
)


def plan(receipt=None, ble_correction_ns=0, lora_correction_ns=0):
    receipt = receipt or {}
    contracts = receipt.get('dual_radio_test_contracts', [])
    missing = [c for c in (CONTRACT, COMMON_CLOCK) if c not in contracts]
    return {
        'schema': 1, 'status': 'PREPARED_OFFLINE', 'hardware_access': False,
        'automatic_flash': False, 'automatic_calibration': False,
        'active_measurement_seconds': sum(p[1] for p in PHASES),
        'initial_acquisition_timeout_seconds': 900,
        'post_flash_acquisition_timeout_seconds': 900,
        'gnss_stable_seconds_before_measurement': 60,
        'phase_blind_settling_seconds': 10,
        'existing_empirical_correction_ns': {'ble': ble_correction_ns, 'lora': lora_correction_ns},
        'lora_engineering_event_delay_ns': 99328000,
        'lora_event_delay_is_empirical_calibration': False,
        'required_measurement_contract': CONTRACT,
        'required_shared_clock_contract': COMMON_CLOCK,
        'receipt_version': receipt.get('version'),
        'missing_firmware_contracts': missing,
        'capture_ready': CONTRACT in contracts,
        'shared_clock_firmware_ready': COMMON_CLOCK in contracts,
        'shared_clock_activation': 'BLOCKED_UNTIL_BOTH_OFFSETS_CONFIRMED_AND_CANDIDATE_REVIEWED',
        'minimum_training': {'valid_PPS_per_radio': 1080, 'coverage_fraction': 0.9,
                             'distinct_ble_observations': 300, 'distinct_lora_pairs': 30},
        'operator_checkpoints': [
            'Exact watch, ownership, unlocked desktop, verified recovery and diagnostic capacity/archive',
            'Confirmed GNSS fix/coherent reference/recurring PPS; keep Shadow visible through radio toggles',
            'Freeze separate BLE/LoRa corrections after the fixed training window; preserve all failures',
            'Review/build/app-only readback, then reacquire GNSS before the held-out validation window',
            'Confirm offsets and origin/path applicability before enabling shared-clock discipline',
            'Restore requested radios and verify USB/TTY/unit-lock release at completion or failure',
        ],
        'phases': [dict(name=n, seconds=s, radios=r, purpose=p) for n, s, r, p in PHASES],
        'limits': ['Measurement goals must be frozen before held-out validation; no accuracy threshold is invented here',
                   'PPS samples from one LoRa prediction are correlated; report distinct physical timing pairs',
                   'GNSS is an independent witness, not a live input to the delivered clock',
                   'Prepared plans and synthetic tests are not hardware acceptance'],
    }


def integer(value, name):
    if type(value) is not int:
        raise ValueError('Integer required: ' + name)
    return value


def channel_reasons(record, channel):
    ref = record['pps']
    reasons = []
    for key in ('reference_valid', 'capture_valid'):
        if ref.get(key) is not True:
            reasons.append('GNSS_' + key.upper())
    if ref.get('aided') is not False:
        reasons.append('GNSS_AID_NOT_EXCLUDED')
    value = record.get(channel)
    if not isinstance(value, dict):
        return reasons + ['MISSING_CHANNEL']
    for key in ('eligible', 'authenticated', 'strict', 'map_valid', 'requested', 'owner_active'):
        if value.get(key) is not True:
            reasons.append(key.upper())
    if not all(isinstance(value.get(k), str) and value[k] for k in ('scope', 'counter_domain')):
        reasons.append('UNKNOWN_SCOPE_OR_COUNTER')
    if type(value.get('settled_ms')) is not int or value['settled_ms'] < 10000:
        reasons.append('SETTLING')
    for left, right in (('capture_epoch', 'sample_epoch'), ('owner_generation', 'sample_generation')):
        if type(value.get(left)) is not int or value[left] <= 0 or type(value.get(right)) is not int or value[left] != value[right]:
            reasons.append(left.upper() + '_MISMATCH')
    try:
        edge = integer(value['edge_tick'], 'edge_tick')
        available = integer(value['available_tick'], 'available_tick')
        until = integer(value['until_tick'], 'until_tick')
        permission = integer(value['permission_tick'], 'permission_tick')
        if not 0 < available < edge < min(until, permission):
            reasons.append('RACING_FUTURE_OR_EXPIRED_MODEL')
        if integer(value['mapping_enclosure_ns'], 'mapping_enclosure_ns') < 0:
            reasons.append('UNKNOWN_MAPPING_ENCLOSURE')
        phase = integer(value['phase_ns'], 'phase_ns')
        if not -500000000 <= phase < 500000000:
            reasons.append('PHASE_OUTSIDE_SIGNED_SECOND')
        integer(value['current_correction_ns'], 'current_correction_ns')
    except (KeyError, ValueError):
        reasons.append('MALFORMED_MEASUREMENT')
    return reasons


def statistics(values):
    if not values:
        return dict(samples=0, mean_ns=None, p99_abs_ns=None, rms_ns=None, maximum_abs_ns=None)
    magnitudes = sorted(abs(v) for v in values)
    index, fraction = divmod((len(values) - 1) * 99, 100)
    p99 = magnitudes[index] if not fraction else (
        magnitudes[index] * (100 - fraction) + magnitudes[index + 1] * fraction) / 100
    return dict(samples=len(values), mean_ns=sum(values) / len(values), p99_abs_ns=p99,
                rms_ns=math.sqrt(sum(v * v for v in values) / len(values)),
                maximum_abs_ns=magnitudes[-1])


def analyze(records):
    groups = {}
    coverage = Counter()
    runs = {}
    missing = 0
    clock_checks = {'instances': set(), 'domains': set(), 'utc_backsteps': 0,
                    'counter_reversals': 0, 'hold_authority_claims': 0,
                    'coarse_fine_claims': 0, 'handoffs': []}
    clock_last = {}
    clock_groups = {}
    holdover_points = {}
    for line, record in enumerate(records, 1):
        if record.get('schema') != 1 or record.get('contract') != CONTRACT:
            raise ValueError('Missing parallel-PPS evidence contract at row ' + str(line))
        run, stage, boot, firmware = (record.get(k) for k in ('run', 'stage', 'boot', 'firmware'))
        if not all(isinstance(v, str) and v for v in (run, stage, boot, firmware)):
            raise ValueError('Run/stage/boot/firmware identity required')
        ref = record['pps']
        number = integer(ref['number'], 'PPS number')
        seen = integer(ref['seen_us'], 'PPS seen_us')
        epoch = integer(ref['capture_epoch'], 'PPS capture_epoch')
        if number <= 0 or seen <= 0 or epoch <= 0:
            raise ValueError('Positive PPS identities required')
        previous = runs.get(run)
        if previous:
            if previous[:2] != (boot, firmware):
                raise ValueError('Mixed boot or image in a run; start a new run identity')
            if seen <= previous[4] or (epoch == previous[2] and number <= previous[3]):
                raise ValueError('Duplicate or backwards PPS record')
            if epoch == previous[2]:
                missing += number - previous[3] - 1
        runs[run] = (boot, firmware, epoch, number, seen)
        coverage[(run, stage)] += 1
        for channel in ('ble', 'lora'):
            value = record.get(channel, {})
            scope = value.get('scope', 'UNKNOWN')
            correction = value.get('current_correction_ns', 'UNKNOWN')
            domain = value.get('counter_domain', 'UNKNOWN')
            key = (run, stage, channel, str(scope), str(domain), str(correction), epoch)
            group = groups.setdefault(key, dict(values=[], seen=[], observations=set(), invalid=Counter()))
            reasons = channel_reasons(record, channel)
            if reasons:
                group['invalid'].update(reasons)
            else:
                group['values'].append(value['phase_ns'])
                group['seen'].append(seen)
                if value.get('observation_id'):
                    group['observations'].add(str(value['observation_id']))
        clock = record.get('clock')
        if isinstance(clock, dict) and clock.get('valid') is True:
            mode = clock.get('mode')
            if mode not in MODES or not clock.get('instance') or not clock.get('counter_domain'):
                raise ValueError('Common-clock mode/instance/domain required')
            instance = (run, clock['instance'])
            clock_checks['instances'].add(instance)
            clock_checks['domains'].add((run, clock['counter_domain']))
            utc = integer(clock['utc_ns'], 'clock UTC')
            tick = integer(clock['counter_tick'], 'clock counter')
            old = clock_last.get(run)
            if old:
                clock_checks['utc_backsteps'] += utc < old[0]
                clock_checks['counter_reversals'] += tick <= old[1]
                if mode != old[2]:
                    clock_checks['handoffs'].append(dict(run=run, stage=stage, before=old[2], after=mode,
                        phase_change_ns=clock['phase_ns'] - old[3]))
            clock_last[run] = (utc, tick, mode, clock['phase_ns'])
            clock_checks['hold_authority_claims'] += mode == 'HOLD' and clock.get('authoritative') is not False
            clock_checks['coarse_fine_claims'] += mode == 'LORA_COARSE' and clock.get('fine') is not False
            if (ref.get('reference_valid') is True and ref.get('capture_valid') is True
                    and ref.get('aided') is False and mode != 'UNSYNCED'):
                phase = integer(clock['phase_ns'], 'clock phase_ns')
                clock_groups.setdefault((run, stage, clock['instance'], mode), []).append(phase)
                if mode == 'HOLD' and type(clock.get('holdover_age_us')) is int:
                    age = clock['holdover_age_us']
                    for seconds in (1, 10, 60, 300, 600):
                        distance = abs(age - seconds * 1000000)
                        key = (run, stage, clock['instance'], seconds)
                        if distance <= 500000 and (key not in holdover_points or distance < holdover_points[key]['distance_us']):
                            holdover_points[key] = dict(age_us=age, phase_ns=phase, distance_us=distance)
    rows, candidates = [], []
    for key, group in sorted(groups.items()):
        run, stage, channel, scope, domain, correction, ref_epoch = key
        values = group['values']
        summary = dict(run=run, stage=stage, channel=channel, scope=scope, counter_domain=domain,
                       reference_capture_epoch=ref_epoch,
                       current_correction_ns=correction, observed_stage_rows=coverage[(run, stage)],
                       distinct_observations=len(group['observations']), invalid_reasons=dict(group['invalid']),
                       **statistics(values))
        if group['seen']:
            midpoint = (min(group['seen']) + max(group['seen'])) / 2
            first = [v for t, v in zip(group['seen'], values) if t <= midpoint]
            second = [v for t, v in zip(group['seen'], values) if t > midpoint]
            summary['first_half_mean_ns'] = sum(first) / len(first)
            summary['second_half_mean_ns'] = sum(second) / len(second) if second else None
        rows.append(summary)
        minimum_updates = 300 if channel == 'ble' else 30
        if stage == 'parallel_train' and len(values) >= 1080 and len(group['observations']) >= minimum_updates:
            span = max(group['seen']) - min(group['seen'])
            if span >= 1190000000 and max(values) - min(values) <= 500000000:
                incremental = -round(summary['mean_ns'])
                step = 50 if channel == 'ble' else 1  # Configuration units, not claimed RF resolution.
                incremental = (1 if incremental >= 0 else -1) * ((abs(incremental) + step // 2) // step) * step
                candidates.append(dict(run=run, channel=channel, scope=scope,
                    reference_capture_epoch=ref_epoch,
                    status='CANDIDATE_REQUIRES_HELD_OUT_VALIDATION', samples=len(values),
                    existing_correction_ns=int(correction), incremental_correction_ns=incremental,
                    candidate_total_correction_ns=int(correction) + incremental,
                    automatic_apply=False))
    by_run_instances = Counter(r for r, _ in clock_checks['instances'])
    by_run_domains = Counter(r for r, _ in clock_checks['domains'])
    clock_checks['single_instance_per_run'] = bool(by_run_instances) and all(n == 1 for n in by_run_instances.values())
    clock_checks['single_counter_domain_per_run'] = bool(by_run_domains) and all(n == 1 for n in by_run_domains.values())
    clock_checks['instances'] = sorted([list(x) for x in clock_checks['instances']])
    clock_checks['domains'] = sorted([list(x) for x in clock_checks['domains']])
    return dict(schema=1, status='OFFLINE_ANALYSIS_COMPLETE', hardware_access=False,
                records=sum(coverage.values()), missing_PPS_records=missing, statistics=rows,
                calibration_candidates=candidates, shared_clock_evidence=clock_checks,
                clock_statistics=[dict(run=r, stage=s, instance=i, mode=m, **statistics(v))
                                  for (r, s, i, m), v in sorted(clock_groups.items())],
                holdover_observations=[dict(run=r, stage=s, instance=i, target_seconds=t, **v)
                                       for (r, s, i, t), v in sorted(holdover_points.items())],
                offsets_confirmed=False, shared_clock_activation_authorized=False,
                limits='Reported firmware observations; no independent UTC/RF endpoint bound. '
                       'No outlier trimming. LoRa PPS projections are correlated within each timing pair. '
                       'Candidates do not authorize application; held-out validation and origin/path review remain required.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    prepare = sub.add_parser('prepare')
    prepare.add_argument('--receipt', type=Path)
    prepare.add_argument('--ble-correction-ns', type=int, default=0)
    prepare.add_argument('--lora-correction-ns', type=int, default=0)
    prepare.add_argument('--out', required=True, type=Path)
    report = sub.add_parser('analyze')
    report.add_argument('--input', required=True, type=Path)
    report.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    output = args.out.resolve()
    if not output.is_relative_to(repo / 'artifacts') or subprocess.run(
            ['git', '-C', str(repo), 'check-ignore', '--quiet', str(output)]).returncode:
        parser.error('Ignored repository artifacts output required; raw analysis stays local')
    if args.out.exists():
        parser.error('Fresh output required; existing evidence is never overwritten')
    if args.action == 'prepare':
        receipt = json.loads(args.receipt.read_text()) if args.receipt else None
        result = plan(receipt, args.ble_correction_ns, args.lora_correction_ns)
    else:
        raw = args.input.read_bytes()
        records = [json.loads(line) for line in raw.decode().splitlines() if line.strip()]
        result = analyze(records)
        result['input_sha256'] = hashlib.sha256(raw).hexdigest()
    args.out.mkdir(parents=True)
    (args.out / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ('status', 'hardware_access')}))


if __name__ == '__main__':
    main()
