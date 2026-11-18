#!/usr/bin/env python3
"""Analyze validated GNSS-free snapshots. Never opens USB or changes offsets.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT

Inputs are private validated capture JSONL, frozen protocol JSON and event JSON.
Outputs contain selected aggregates and sequential context aliases, no identities.
No independent clock error is inferred from model disagreement.
"""
import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

CONTRACT = 'TWATCH_GNSS_FREE_SNAPSHOT_V1'
MODES = ('UNSYNCED', 'BLE_FINE', 'LORA_COARSE', 'HOLD')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def distribution(values):
    """Descriptive distribution of dependent observations, never accuracy."""
    if not values:
        return dict(n=0, mean_ns=None, rms_ns=None, p99_abs_ns=None, max_abs_ns=None)
    absolute = sorted(abs(v) for v in values)
    return dict(n=len(values), mean_ns=statistics.fmean(values),
                rms_ns=math.sqrt(statistics.fmean(v*v for v in values)),
                p99_abs_ns=absolute[math.ceil(.99*len(absolute))-1],
                max_abs_ns=absolute[-1], min_ns=min(values), max_ns=max(values))


def differences(x):
    """Subtract integer UTC before any conversion to floating point."""
    result = {}
    b, l, c = x['ble'], x['lora'], x['clock']
    if b['eligible'] and l['eligible']:
        result['lora_minus_ble'] = int(l['utc_ns']) - int(b['utc_ns'])
    for name, radio in [('ble', b), ('lora', l)]:
        if c['valid'] and radio['eligible']:
            result['clock_minus_' + name] = int(c['utc_ns']) - int(radio['utc_ns'])
    return result


def load_samples(path):
    rows, errors = [], []
    for line_number, line in enumerate(Path(path).read_text().splitlines(), 1):
        try:
            row = json.loads(line)
            x = row['sample']
            assert x['contract'] == CONTRACT and x['gnss_witness'] is False
            assert x['tick'] == x['clock']['counter_tick'] == x['ble']['edge_tick'] == x['lora']['edge_tick']
            assert x['clock']['mode'] in MODES
            for name in ['ble', 'lora']:
                assert type(x[name]['utc_ns']) is int
                if x[name]['eligible']:
                    assert x[name]['available_tick'] < x['tick'] < x[name]['until_tick']
                    assert x[name]['observation_id'] and x[name]['authenticated']
            rows.append(row)
        except (ValueError, KeyError, AssertionError, TypeError) as error:
            errors.append(dict(line=line_number, reason=type(error).__name__))
    return rows, errors


def analyze(rows, protocol, events, invalid_lines=None):
    specs = [dict(name='warmup', seconds=protocol['warmup_seconds'])] + protocol['stages']
    by_stage = defaultdict(list)
    for row in rows:
        by_stage[row['stage']].append(row)
    aliases = defaultdict(dict)

    def alias(kind, identity):
        if identity in ('0000000000000000', '', None):
            return 'unknown'
        table = aliases[kind]
        if identity not in table:
            table[identity] = kind + '-' + str(len(table)+1)
        return table[identity]

    result = dict(schema=1, evidence_type='GNSS_FREE_HARDWARE_MODEL_AND_SERVICE_OBSERVATIONS',
                  author='Jonathan Yantis', license='CC-BY-4.0', independent_reference=False,
                  estimator='ONE_CLOCK_PRIORITY_BLE_LORA_HOLD_NOT_STATISTICAL_AVERAGING',
                  correction_policy=protocol['fixed_prior'],
                  populations='All fixed windows, no phase trimming or passing-only selection',
                  observation_count=len(rows), invalid_input_lines=invalid_lines or [],
                  stages=[], transitions=[], continuity={}, contexts=[], hold_recoveries=[], _histograms=[], _availability=[])
    all_sequences, all_ticks, all_utc = [], [], []
    instances, domains, boots, firmware = set(), set(), set(), set()
    timeline, context_values = [], defaultdict(lambda: defaultdict(list))
    previous = None
    previous_authenticated_root = previous_authenticated_parent = None
    previous_hold = None
    elapsed_origin = 0
    for spec in specs:
        name, seconds = spec['name'], spec['seconds']
        stage_rows = [r for r in by_stage[name] if 0 <= r['stage_elapsed_s'] < seconds]
        stage_events = [e for e in events if e['stage'] == name]
        first_event = next((e for e in stage_events if e['kind'] == 'STAGE_START'), None)
        end_event = next((e for e in stage_events if e['kind'] == 'STAGE_END'), None)
        mode_samples, mode_seconds = Counter(), Counter()
        metrics, physical = defaultdict(list), defaultdict(set)
        innovations, innovation_ids = defaultdict(list), defaultdict(set)
        reasons = defaultdict(Counter)
        bins = {}
        paths, context_samples = Counter(), Counter()
        calibration = defaultdict(Counter);corrections = defaultdict(Counter);projected_corrections = defaultdict(Counter);eligible_corrections = defaultdict(Counter)
        available = Counter()
        available_seconds = Counter()
        observed_seconds = 0.
        root_changes = parent_changes = root_field_changes = parent_field_changes = 0
        first_auth = first_ready = first_fine = first_rc = None
        split_values = {'first_half': defaultdict(list), 'second_half': defaultdict(list)}
        for index, row in enumerate(stage_rows):
            x = row['sample']; c = x['clock']; at = row['stage_elapsed_s']
            bp, lp = x['ble_lineage'], x['lora_lineage']
            ba = alias('BLE', x['ble']['scope']) if x['ble']['authenticated'] else 'unknown'
            la = alias('LoRa', x['lora']['scope']) if x['lora']['authenticated'] else 'unknown'
            root = alias('root', bp['root'])
            parent = alias('parent', bp['parent'])
            mode_samples[c['mode']] += 1
            delta = differences(x)
            b,l=x['ble'],x['lora']
            if (not b['eligible'] or not l['eligible']) and all(r['map_valid'] and r['authenticated'] and r['utc_ns'] and r['available_tick']<x['tick']<r['until_tick'] for r in [b,l]):
                metrics['unqualified_lora_minus_ble'].append(int(l['utc_ns'])-int(b['utc_ns']))
            for key, value in delta.items():
                metrics[key].append(value)
                context_values[(ba, la)][key].append(value)
                split_values['first_half' if at < seconds/2 else 'second_half'][key].append(value)
            context_samples[(ba, la)] += 1
            following_rc = bool(x['ble']['authenticated'] and bp['depth_known'] and bp['depth'] > 0)
            following_gm = bool(x['ble']['authenticated'] and bp['depth_known'] and bp['depth'] == 0)
            paths['authenticated_rc' if following_rc else 'authenticated_gm' if following_gm else 'not_authenticated'] += 1
            if x['ble']['authenticated'] and first_auth is None: first_auth = at
            if x['ble']['eligible'] and first_ready is None: first_ready = at
            if c['mode'] == 'BLE_FINE' and first_fine is None: first_fine = at
            if following_rc and first_rc is None: first_rc = at
            state = dict(clock_valid=bool(c['valid']), authoritative=bool(c['authoritative']),
                         fine=bool(c['fine']), ble=bool(x['ble']['eligible']), lora=bool(x['lora']['eligible']),
                         lora_bridge=bool(not x['ble']['eligible'] and x['lora']['eligible'] and c['mode']=='LORA_COARSE' and c['authoritative']))
            for key, yes in state.items(): available[key] += yes
            for radio_name in ['ble','lora']:
                calibration[radio_name][str(x[radio_name]['calibration_match'])] += 1
                corrections[radio_name][str(x[radio_name]['current_correction_ns'])] += 1
                if x[radio_name]['map_valid'] and x[radio_name]['utc_ns']:projected_corrections[radio_name][str(x[radio_name]['current_correction_ns'])] += 1
                if x[radio_name]['eligible']:eligible_corrections[radio_name][str(x[radio_name]['current_correction_ns'])] += 1
            calibration['clock'][str(c['calibrated'])] += 1
            bins.setdefault(int(at), []).append(dict(state,_mode=c['mode']))
            # A capped zero-order observation summary, not continuous proof.
            next_at = stage_rows[index+1]['stage_elapsed_s'] if index+1 < len(stage_rows) else seconds
            weight = max(0, min(next_at-at, 1.5, seconds-at))
            observed_seconds += weight
            mode_seconds[c['mode']] += weight
            for key, yes in state.items(): available_seconds[key] += weight*yes
            for radio_name, scope in [('ble', ba), ('lora', la)]:
                radio = x[radio_name]
                reasons[radio_name][str(x.get('ble_refusal',radio['reason']) if radio_name=='ble' else radio['reason'])] += 1
                if radio['eligible']:
                    physical[radio_name].add((radio['scope'], radio['counter_domain'], radio['observation_id']))
                event = x[radio_name + '_causal_event']
                event_id = (event['scope'], event['observation_id'], event['tick'])
                event_elapsed = at - (x['tick']-event['tick'])/20000000 if event['tick'] else -1
                if event['tick'] and 0 <= event_elapsed < seconds and event['scope'] == radio['scope'] and event_id not in innovation_ids[radio_name]:
                    assert event['tick'] <= x['tick']
                    innovation_ids[radio_name].add(event_id)
                    innovations[radio_name].append(event['innovation_ns'])
            all_sequences.append(x['sequence']); all_ticks.append(x['tick'])
            if c['valid']: all_utc.append(c['utc_ns'])
            instances.add(c['instance']); domains.add(c['counter_domain']); boots.add(row['boot']); firmware.add(row['firmware'])
            if previous:
                old = previous['sample']; old_path = old['ble_lineage']
                root_field_changes += bp['root'] != old_path['root']
                parent_field_changes += bp['parent'] != old_path['parent']
                if c['mode'] != old['clock']['mode'] or bp['parent'] != old_path['parent'] or bp['root'] != old_path['root']:
                    result['transitions'].append(dict(stage=name,stage_elapsed_s=at,mode=c['mode'],
                        previous_mode=old['clock']['mode'],root=root,parent=parent,depth=bp['depth'],
                        sampling_interval_s=row['host_elapsed_s']-previous['host_elapsed_s']))
            if x['ble']['authenticated'] and root!='unknown':
                root_changes += previous_authenticated_root is not None and bp['root']!=previous_authenticated_root
                parent_changes += previous_authenticated_parent is not None and bp['parent']!=previous_authenticated_parent
                previous_authenticated_root=bp['root'];previous_authenticated_parent=bp['parent']
            if c['mode'] == 'HOLD':
                loss_stage=previous_hold['stage'] if previous_hold else name
                previous_hold = dict(stage=loss_stage,stage_elapsed_s=at,age_us=c['holdover_age_us'],rate_ppb=c['rate_ppb'],root=root)
            elif previous_hold is not None and c['authoritative']:
                result['hold_recoveries'].append(dict(loss_stage=previous_hold['stage'],return_stage=name,
                    last_sampled_hold_age_us=previous_hold['age_us'],mode=c['mode'],root=root,
                    clock_minus_returning_input_ns=delta.get('clock_minus_ble' if c['mode']=='BLE_FINE' else 'clock_minus_lora'),
                    independent_holdover_error_ns=None))
                previous_hold = None
            timeline.append(dict(elapsed_s=elapsed_origin+at,stage=name,stage_elapsed_s=at,mode=c['mode'],
                clock_valid=int(c['valid']),authoritative=int(c['authoritative']),ble_ready=int(x['ble']['eligible']),
                lora_ready=int(x['lora']['eligible']),root=root,parent=parent,depth=bp['depth'],
                rate_ppb=c['rate_ppb'],holdover_age_s=c['holdover_age_us']/1e6,
                lora_minus_ble_ns=delta.get('lora_minus_ble'),clock_minus_ble_ns=delta.get('clock_minus_ble'),
                clock_minus_lora_ns=delta.get('clock_minus_lora'),ble_context=ba,lora_context=la))
            previous = row
        grid = {}
        for key in ['clock_valid','authoritative','fine','ble','lora','lora_bridge']:
            yes = sum(any(x[key] for x in observations) for observations in bins.values())
            no = sum(not any(x[key] for x in observations) for observations in bins.values())
            grid[key] = dict(nominal_opportunities=seconds,observed_available_bins=yes,
                             observed_unavailable_bins=no,missing_bins=seconds-len(bins),
                             observed_available_fraction=yes/seconds,
                             missing_agnostic_upper_fraction=(yes+seconds-len(bins))/seconds)
        def histogram(population,metric,values,ble='',lora=''):
            for value,count in sorted(Counter(values).items()):
                result['_histograms'].append(dict(population=population,stage=name,ble_context=ble,lora_context=lora,metric=metric,value_ns=value,count=count))
        for key,values in metrics.items():histogram('stage',key,values)
        for key,values in innovations.items():histogram('innovation',key,values)
        for half,population in split_values.items():
            for key,values in population.items():histogram(half,key,values)
        for second in range(seconds):
            observations=bins.get(second,[])
            entry=dict(stage=name,second=second,observed_snapshots=len(observations))
            for key in ['clock_valid','authoritative','fine','ble','lora','lora_bridge']:
                entry[key]=int(any(x[key] for x in observations)) if observations else None
            for mode in MODES:entry[mode]=sum(x['_mode']==mode for x in observations)
            result['_availability'].append(entry)
        result['stages'].append(dict(name=name,seconds=seconds,started=bool(first_event),completed=bool(end_event),
            actual_window_seconds=end_event.get('actual_elapsed_s') if end_event else None,
            snapshots=len(stage_rows),outside_fixed_window_snapshots=len(by_stage[name])-len(stage_rows),nominal_bins=seconds,observed_bins=len(bins),missing_bins=seconds-len(bins),
            mode_snapshots=dict(mode_samples),sampled_mode_seconds=dict(mode_seconds),
            supported_seconds=observed_seconds,unobserved_seconds=max(0,seconds-observed_seconds),
            sampled_availability=dict(available),sampled_availability_seconds=dict(available_seconds),grid=grid,
            calibration_match_samples={key:dict(value) for key,value in calibration.items()},
            correction_policy_samples={key:dict(value) for key,value in corrections.items()},
            projected_correction_samples={key:dict(value) for key,value in projected_corrections.items()},
            eligible_correction_samples={key:dict(value) for key,value in eligible_corrections.items()},
            authenticated_paths=dict(paths),first_sampled_authenticated_ble_s=first_auth,
            first_sampled_ble_ready_s=first_ready,first_sampled_ble_fine_s=first_fine,
            first_sampled_authenticated_rc_s=first_rc,root_changes=root_changes,parent_changes=parent_changes,root_field_changes=root_field_changes,parent_field_changes=parent_field_changes,
            distinct_physical_observations={key:len(physical[key]) for key in ['ble','lora']},
            causal_innovations={key:distribution(innovations[key]) for key in ['ble','lora']},
            model_disagreement={key:distribution(value) for key,value in metrics.items()},
            halves={half:{key:distribution(value) for key,value in values.items()} for half,values in split_values.items()},
            refusal_samples={key:dict(value) for key,value in reasons.items()},
            contexts=[dict(ble=key[0],lora=key[1],snapshots=value) for key,value in context_samples.items()],
            max_holdover_age_s=max((r['sample']['clock']['holdover_age_us']/1e6 for r in stage_rows),default=None),
            lora_pair_counter_min=min((r['sample']['lora_physical_pairs'] for r in stage_rows),default=None),
            lora_pair_counter_max=max((r['sample']['lora_physical_pairs'] for r in stage_rows),default=None),
            rate_ppb_min=min((r['sample']['clock']['rate_ppb'] for r in stage_rows),default=None),
            rate_ppb_max=max((r['sample']['clock']['rate_ppb'] for r in stage_rows),default=None)))
        elapsed_origin += seconds
    result['contexts'] = [dict(ble=key[0],lora=key[1],disagreement={name:distribution(values) for name,values in population.items()}) for key,population in context_values.items()]
    all_sequences=[r['sample']['sequence'] for r in rows]
    all_ticks=[r['sample']['tick'] for r in rows]
    all_utc=[r['sample']['clock']['utc_ns'] for r in rows if r['sample']['clock']['valid']]
    instances={r['sample']['clock']['instance'] for r in rows};domains={r['sample']['clock']['counter_domain'] for r in rows}
    boots={r['boot'] for r in rows};firmware={r['firmware'] for r in rows}
    for (ble,lora),population in context_values.items():
        for metric,values in population.items():
            for value,count in sorted(Counter(values).items()):
                result['_histograms'].append(dict(population='context',stage='',ble_context=ble,lora_context=lora,metric=metric,value_ns=value,count=count))
    result['continuity'] = dict(clock_instances=len(instances),counter_domains=len(domains),boots=len(boots),firmware_builds=len(firmware),
        counter_nonadvancing=sum(b<=a for a,b in zip(all_ticks,all_ticks[1:])),
        snapshot_nonadvancing=sum(b<=a for a,b in zip(all_sequences,all_sequences[1:])),
        utc_nonadvancing=sum(b<=a for a,b in zip(all_utc,all_utc[1:])),
        valid_utc_observations=len(all_utc),scope='Sampled internal common counter and UTC; no independently measured output')
    result['event_counts'] = dict(Counter(e['kind'] for e in events))
    return result, timeline


def aggregate_timeline(rows, seconds=5):
    groups = defaultdict(list)
    for row in rows: groups[int(row['elapsed_s']//seconds)].append(row)
    output = []
    for index, population in groups.items():
        row = dict(elapsed_s=index*seconds,bin_seconds=seconds,snapshots=len(population),stage=population[0]['stage'])
        for mode in MODES:row[mode]=sum(x['mode']==mode for x in population)
        for key in ['clock_valid','authoritative','ble_ready','lora_ready']:row[key]=sum(x[key] for x in population)
        for key in ['lora_minus_ble_ns','clock_minus_ble_ns','clock_minus_lora_ns','rate_ppb','holdover_age_s']:
            values=[x[key] for x in population if x[key] is not None]
            for label,func in [('median',statistics.median),('min',min),('max',max)]:row[key+'_'+label]=func(values) if values else None
        output.append(row)
    return output


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['input','protocol','events','out']:parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();rows,errors=load_samples(args.input)
    protocol=json.loads(args.protocol.read_text());events=json.loads(args.events.read_text())
    result,timeline=analyze(rows,protocol,events,errors)
    args.out.mkdir(parents=True,exist_ok=False)
    result['provenance']={key:sha(value) for key,value in [('private_samples_sha256',args.input),('frozen_protocol_sha256',args.protocol),('private_events_sha256',args.events),('analysis_source_sha256',Path(__file__))]}
    for private_name,filename in [('_histograms','distributions.csv'),('_availability','availability.csv')]:
        selected=result.pop(private_name)
        if selected:
            with (args.out/filename).open('w') as stream:
                writer=csv.DictWriter(stream,fieldnames=list(selected[0]));writer.writeheader();writer.writerows(selected)
    (args.out/'summary.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    aggregates=aggregate_timeline(timeline)
    if aggregates:
        with (args.out/'timeline.csv').open('w') as stream:
            writer=csv.DictWriter(stream,fieldnames=list(aggregates[0]));writer.writeheader();writer.writerows(aggregates)
    print(json.dumps(dict(snapshots=len(rows),invalid_lines=len(errors),continuity=result['continuity'],completed_stages=sum(x['completed'] for x in result['stages']))))


if __name__=='__main__':main()
