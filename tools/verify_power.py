#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Yantis.
"""Offline PC117 derived-data checks and explicitly hypothetical energy models.

Default: verify selected public bytes and recomputed results, without hardware,
network, private captures, or nonstandard Python packages. --out writes a fresh
analysis/PGFPlots export directory. Figures/data: CC BY 4.0, Jonathan Yantis.
"""
import argparse
import collections
import csv
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'evidence/power/2026-10-19'
HZ = 16000000

def read(name):
    return json.loads((DATA / name).read_text())

def rows(name):
    with (DATA / name).open(newline='') as f:
        return list(csv.DictReader(f))

def yes(x):
    return x == 'True'

def stats(values):
    if not values:
        return dict(n=0, mean_ns=None, rms_ns=None, max_abs_ns=None)
    return dict(n=len(values), mean_ns=sum(values)/len(values),
                rms_ns=math.sqrt(sum(v*v for v in values)/len(values)),
                max_abs_ns=max(abs(v) for v in values))

def close(a, b):
    if a is None or b in ('', None):
        assert a is None and b in ('', None)
    else:
        assert math.isclose(float(a), float(b), rel_tol=1e-11, abs_tol=1e-8), (a,b)

def energy(hold, aux=0, sleep=.03, listen=15, wake=.05, start_power=10,
           start_s=.005, compute_power=5, compute_s=.002,
           reacquire_mj=10, reacquire_probability=.01):
    """mW, seconds, mJ. Disjoint times inside fixed C=H+0.125 seconds."""
    window=.125
    cycle=hold+window
    assert cycle >= window+start_s+compute_s
    overhead=(wake+(start_power-sleep)*start_s+
              (compute_power-sleep)*compute_s+
              reacquire_mj*reacquire_probability)
    p=sleep+(listen-sleep)*window/cycle+overhead/cycle+aux
    baseline=listen+aux
    return dict(hold_s=hold, aux_mw=aux, average_mw=p, baseline_mw=baseline,
                relative_reduction_percent=100*(1-p/baseline),
                overhead_mj=overhead, scheduled_training_percent=100*window/cycle)

def models():
    drift=[]
    for target in (1,10,100):
        for rho in (.01,.1,1,10):
            for q in (0,1e6/32768/math.sqrt(12)):
                budget=target*target-.1**2-.2**2-q*q
                drift.append(dict(target_us=target,rho_ppm=rho,
                                  quantization_rms_us=q,
                                  max_hold_s=math.sqrt(budget)/rho if budget>0 else None))
    central=[energy(h,aux)for aux in (0,5,20)for h in (5,10,20)]
    # Corner scenarios, deliberately not a confidence interval or data fit.
    corners=[]
    for h in (5,10,20):
        for label,kw in [('low_overhead',dict(sleep=.003,listen=5,wake=.001,
                                            start_power=5,start_s=.0001,
                                            compute_power=1,compute_s=.0001,
                                            reacquire_mj=1,reacquire_probability=0)),
                         ('high_overhead',dict(sleep=1,listen=30,wake=1,
                                              start_power=30,start_s=.05,
                                              compute_power=20,compute_s=.02,
                                              reacquire_mj=100,reacquire_probability=.1))]:
            corners.append(dict(scenario=label,**energy(h,**kw)))
    return dict(label='HYPOTHETICAL_NOT_MEASURED_BOARD_POWER',central=central,
                sensitivity_corners=corners,drift_budget=drift,
                assumptions=dict(window_s=.125,initial_rms_us=.1,
                                 wake_jitter_rms_us=.2,zero_mean_independent=True,
                                 ppm_equals_us_per_s=True),
                examples=dict(split_wake_break_even_mj=15*.0625/3,
                              turnover_iid_cycle_count_at_p08=16/.8,
                              turnover_iid_seconds_at_p08=16/.8*5.125,
                              three_iid_attempt_success_at_p05=1-(1-.5)**3))

def analyze():
    ble,pps,cycles,blocks=map(rows,['ble-records.csv','pps-records.csv','cycles.csv','blocks.csv'])
    assert len(ble)==7326 and len(pps)==1414 and len(cycles)==120 and len(blocks)==6
    results=[]
    for block in blocks:
        t=block['trial'];B=[b for b in ble if b['trial']==t];P=[p for p in pps if p['trial']==t]
        C=[c for c in cycles if c['trial']==t];L=int(block['listen_ms'])*16000
        H=int(block['hold_ms'])*16000;period=L+H;end=20*period
        assert int(block['elapsed_observed_end_tick'])==end==int(block['elapsed_planned_run_end_tick'])
        assert int(block['elapsed_commanded_tick'])<=-80000
        assert (128000-int(block['elapsed_alignment_rx_tick']))%1000000==0
        scopes={b['scope_alias'] for b in B if yes(b['valid'])}
        assert len(scopes)==1 and len({p['scope_alias']for p in P if yes(p['eligible'])})==1
        assert scopes=={p['scope_alias'] for p in P if yes(p['eligible'])}
        selected=sorted((b for b in B if yes(b['train'])),key=lambda b:int(b['sequence']))
        assert len(selected)==int(block['training_pairs'])
        assert int(block['final_map_generation'])-int(block['initial_map_generation'])==len(selected)
        turn_seq=int(selected[15]['sequence'])if len(selected)>=16 else None
        for b in B:
            rx=int(b['elapsed_rx_tick']);av=int(b['elapsed_available_tick'])
            c=rx//period+1;held=0<=rx<end and rx%period>=L
            assert yes(b['in_run'])==(0<=rx<end)
            assert int(b['cycle'])==c and yes(b['in_hold'])==held
            assert yes(b['after_16_selected'])==(turn_seq is not None and int(b['sequence'])>turn_seq)
            if yes(b['valid']):assert int(b['normalized_error_ns'])==int(b['raw_error_ns'])+10543
            else:assert b['raw_error_ns']==b['normalized_error_ns']==''
            if yes(b['train']):
                lo=(int(b['completion_cycle'])-1)*period
                assert lo<=rx<=av<lo+L
                close(b['rx_offset_ms'],(rx-lo)/16000)
                close(b['completion_margin_ms'],(lo+L-av)/16000)
        for p in P:
            tick=int(p['elapsed_edge_tick']);assert 0<=tick<end
            held=tick%period>=L;assert yes(p['in_hold'])==held
            eligible=all(yes(p[k])for k in ['map_valid','reference_seen','reference_fix','reference_pps']) and p['reason']=='0' and (not held or yes(p['pc_projection']))
            assert yes(p['eligible'])==eligible
        valid=[b for b in B if yes(b['valid'])and yes(b['in_run'])]
        hb=[b for b in valid if yes(b['in_hold'])]
        hp=[p for p in P if yes(p['eligible'])and yes(p['in_hold'])]
        post=[b for b in hb if yes(b['after_16_selected'])]
        bs=stats([int(b['normalized_error_ns'])for b in hb]);ps=stats([int(p['phase_ns'])for p in hp]);ts=stats([int(b['normalized_error_ns'])for b in post])
        for k,col in [('n','hold_ble_n'),('rms_ns','hold_ble_rms_ns'),('max_abs_ns','hold_ble_max_abs_ns')]:close(bs[k],block[col])
        for k,col in [('n','hold_pps_n'),('rms_ns','hold_pps_rms_ns'),('max_abs_ns','hold_pps_max_abs_ns')]:close(ps[k],block[col])
        close(ts['n'],block['after16_hold_ble_n']);close(ts['rms_ns'],block['after16_hold_ble_rms_ns'])
        bins={(int(b['cycle']),int(float(b['hold_age_s'])))for b in hb}
        close(len(bins),block['observed_hold_second_bins'])
        assert len(C)==20
        outcome=[]
        for c in C:
            cc=int(c['cycle']);cb=[b for b in B if int(b['cycle'])==cc and yes(b['in_run'])]
            picked=sum(yes(b['train'])for b in cb)
            assert len(cb)==int(c['received_timing_pairs'])and picked==int(c['training_pairs'])
            outcome.append('refresh'if picked else 'no_pairs'if not cb else 'no_complete_selection')
        results.append(dict(block_number=int(block['block_number']),trial=int(t),replicate=int(block['replicate']),scope_alias=next(iter(scopes)),hold_s=H/HZ,
            selected=len(selected),empty=20-len(selected),outcomes=outcome,
            no_pairs=outcome.count('no_pairs'),no_complete_selection=outcome.count('no_complete_selection'),
            ble_hold=bs,ble_hold_raw=stats([int(b['raw_error_ns'])for b in hb]),
            pps_hold=ps,pps_hold_observed=sum(yes(p['in_hold'])for p in P),
            pps_run_observed=len(P),pps_run_ineligible=sum(not yes(p['eligible'])for p in P),
            occupied_hold_second_bins=len(bins),expected_hold_second_bins=20*int(H/HZ),
            last_valid_ble_elapsed_s=max(int(b['elapsed_rx_tick'])for b in hb)/HZ,
            unknown_ble_second_bins=20*int(H/HZ)-len(bins),
            post16_hold=ts,turnover_s=int(selected[15]['elapsed_available_tick'])/HZ if turn_seq else None,
            remaining_after_turnover_s=(end-int(selected[15]['elapsed_available_tick']))/HZ if turn_seq else None,
            rx_offset_min_ms=min(float(b['rx_offset_ms'])for b in selected),
            rx_offset_max_ms=max(float(b['rx_offset_ms'])for b in selected),
            completion_margin_min_ms=min(float(b['completion_margin_ms'])for b in selected),
            initial_map_generation=int(block['initial_map_generation']),final_map_generation=int(block['final_map_generation'])))
    audit=rows('hold-audit.csv');assert len(audit)==123 and all(yes(a['frozen'])and yes(a['fixed_anchor'])for a in audit)
    attempts=read('attempts.json')['attempts'];assert collections.Counter(a['status']for a in attempts)=={'COMPLETE':7,'FAILED':2}
    assert sum(r['selected']for r in results)==42 and sum(r['no_pairs']for r in results)==69 and sum(r['no_complete_selection']for r in results)==9
    # Counts are campaign accounting only. No cross-scope RMS is calculated.
    return dict(schema=1,status='PASS_PUBLIC_DERIVED_RECOMPUTATION',blocks=results,
                total_selected=42,total_empty=78,total_hold_states=123,
                total_ble_rows=len(ble),total_pps_rows=len(pps),
                model=models(),limitations=read('provenance.json')['reconstruction_limit'])

def figure_bodies(result):
    b=result['blocks']
    grid=[r'\begin{tikzpicture}[x=0.39cm,y=0.60cm]']
    colors={'refresh':'studyblue','no_pairs':'gray!20','no_complete_selection':'studyorange!65'}
    for row,r in enumerate(b):
        y=5-row
        grid.append(r'\node[anchor=east,font=\small] at (-.25,%s) {B%d: %g s, r%d};'%(y,r['block_number'],r['hold_s'],r['replicate']))
        for col,outcome in enumerate(r['outcomes']):
            grid.append(r'\fill[%s] (%s,%s) rectangle ++(.90,.65);'%(colors[outcome],col,y-.325))
    for c in range(0,20,2):grid.append(r'\node[font=\scriptsize] at (%s,5.8) {%s};'%(c+.45,c+1))
    grid.append(r'\node[font=\small] at (9.5,6.6) {Scheduled window (1--20)};')
    for x,color,label in [(0,'studyblue','Refresh'),(6,'gray!20','No accepted pairs'),(14,'studyorange!65','RX, no selection')]:
        grid.append(r'\fill[%s] (%s,-1.1) rectangle ++(.55,.35);\node[anchor=west,font=\scriptsize] at (%s,-.925) {%s};'%(color,x,x+.7,label))
    grid.append(r'\end{tikzpicture}')
    rms=[r'\begin{tikzpicture}\begin{semilogyaxis}[width=.94\linewidth,height=5.7cm,ymin=.05,ymax=10,xmin=.5,xmax=6.5,xtick={1,2,3,4,5,6},xticklabels={B1,B2,B3,B4,B5,B6},ylabel={Conditional HOLD RMS ($\mu$s)},xlabel={Chronological run (separate populations)},legend style={column sep=.8em,font=\small,at={(.5,1.03)},anchor=south,legend columns=2},grid=major]']
    for color,key,label in [('studyblue','ble_hold','BLE + fixed constant'),('studyorange','pps_hold','Eligible PPS')]:
        rms.append(r'\addplot[%s,thick,only marks,mark=*] coordinates {%s};\addlegendentry{%s}'%(color,' '.join('(%d,%.10g)'%(r['block_number'],r[key]['rms_ns']/1000)for r in b),label))
    rms.extend([r'\addplot[black,dashed,forget plot] coordinates {(.5,1)(6.5,1)};',r'\end{semilogyaxis}\end{tikzpicture}'])
    energyfig=[r'\begin{tikzpicture}\begin{axis}[width=.94\linewidth,height=6cm,xmode=log,ymode=log,xmin=1,xmax=60,xtick={1,2,5,10,20,60},xticklabels={1,2,5,10,20,60},ymin=.04,ymax=50,xlabel={HOLD $H$ (s)},ylabel={Hypothetical average power (mW)},legend style={column sep=.8em,font=\small,at={(.5,1.03)},anchor=south,legend columns=3},grid=major]']
    for aux,color in [(0,'studyblue'),(5,'studyorange'),(20,'black')]:
        energyfig.append(r'\addplot[%s,thick] coordinates {%s};\addlegendentry{$P_{aux}=%g$ mW}'%(color,' '.join('(%g,%.10g)'%(h,energy(h,aux)['average_mw'])for h in [1,2,5,10,20,30,60]),aux))
    energyfig.extend([r'\end{axis}\end{tikzpicture}'])
    drift=[r'\begin{tikzpicture}\begin{loglogaxis}[width=.94\linewidth,height=5.8cm,xmin=.1,xmax=100,ymin=.1,ymax=1000,xlabel={Time since last complete refresh (s)},ylabel={Hypothetical RMS ($\mu$s)},legend style={column sep=.8em,font=\small,at={(.5,1.03)},anchor=south,legend columns=4},grid=major]']
    for rho,color in [(.01,'studyblue'),(.1,'studyorange'),(1,'black'),(10,'gray')]:
        drift.append(r'\addplot[%s,thick] coordinates {%s};\addlegendentry{%g ppm}'%(color,' '.join('(%g,%.10g)'%(h,math.sqrt(.05+(rho*h)**2))for h in [.1,.2,.5,1,2,5,10,20,50,100]),rho))
    for y in [1,10,100]:drift.append(r'\addplot[dashed,gray,forget plot] coordinates {(.1,%d)(100,%d)};'%(y,y))
    drift.extend([r'\end{loglogaxis}\end{tikzpicture}'])
    return {'window-outcomes':'\n'.join(grid),'conditional-rms':'\n'.join(rms),'energy-model':'\n'.join(energyfig),'drift-model':'\n'.join(drift)}

def verify_manifest():
    for name,entry in read('manifest.json')['files'].items():
        p=ROOT/name
        assert hashlib.sha256(p.read_bytes()).hexdigest()==entry['sha256'],name
        assert p.stat().st_size==entry['bytes'],name

def verify_manuscript(result, figs):
    import re
    manuscript=(ROOT/'papers/ptob-power-scheduling-preprint.tex').read_text()
    for name,body in figs.items():
        assert '% BEGIN GENERATED '+name+'\n'+body+'\n% END GENERATED '+name in manuscript,name
    for b in result['blocks']:
        availability='B%d & %g/%d & %d/20 & %d & %d & %d & %d--%d & %.1f'%(
            b['block_number'],b['hold_s'],b['replicate'],b['selected'],b['empty'],
            b['no_pairs'],b['no_complete_selection'],b['initial_map_generation'],
            b['final_map_generation'],20*(b['hold_s']+.125))
        precision='B%d & %d & %.3f & %d/%d & %d/%d & %.3f & %d'%(
            b['block_number'],b['ble_hold']['n'],b['ble_hold']['rms_ns'],
            b['occupied_hold_second_bins'],b['expected_hold_second_bins'],
            b['pps_hold']['n'],b['pps_hold_observed'],b['pps_hold']['rms_ns'],
            b['pps_hold']['max_abs_ns'])
        assert availability in manuscript,availability
        assert precision in manuscript,precision
    for hold in (5,10,20):
        values=[p for p in result['model']['central']if p['hold_s']==hold]
        row=str(hold)+' & '+' & '.join('%.4f & %.2f\\%%'%(p['average_mw'],p['relative_reduction_percent'])for p in values)
        assert row in manuscript,row
    citations={k for group in re.findall(r'\\cite\{([^}]+)\}',manuscript)for k in group.split(',')}
    bib=re.findall(r'\\bibitem\{([^}]+)\}',manuscript)
    labels=re.findall(r'\\label\{([^}]+)\}',manuscript)
    refs=set(re.findall(r'\\ref\{([^}]+)\}',manuscript))
    assert citations<=set(bib) and len(bib)==len(set(bib))==9
    assert refs<=set(labels) and len(labels)==len(set(labels))
    assert not re.search(r'\\(?:input|include|includegraphics)\b',manuscript),'Source must remain self-contained'
    prior=read('prior-context.json')
    assert prior['no_pooling'] and prior['PC112']['planned_treatments']==37
    assert prior['PC112']['started_treatments']==13 and prior['PC112']['unstarted_treatments']==24
    assert prior['PC115']['cadence_outcomes']=={'COMPLETE':6,'NOT_STARTED_BEGIN_REFUSED':1,'SKIPPED_PRIMING_UNAVAILABLE':6}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,help='Fresh output directory for results and standalone figure sources')
    args=parser.parse_args()
    result=analyze();figs=figure_bodies(result)
    verify_manuscript(result,figs)
    if args.out:
        args.out.mkdir(parents=True,exist_ok=False)
        (args.out/'results.json').write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
        for name,body in figs.items():
            head='% CC-BY-4.0; Copyright (c) 2026 Jonathan Yantis. Generated by tools/verify_power.py\n'
            tex=head+'\\documentclass[border=5pt]{standalone}\n\\usepackage{pgfplots}\n\\pgfplotsset{compat=1.18}\n\\definecolor{studyblue}{RGB}{39,83,109}\n\\definecolor{studyorange}{RGB}{180,94,32}\n\\begin{document}\n'+body+'\n\\end{document}\n'
            (args.out/(name+'.tex')).write_text(tex)
        print(json.dumps(dict(status=result['status'],out=str(args.out),blocks=6)))
    else:
        verify_manifest()
        assert result==read('results.json'),'Derived results differ'
        edition=json.loads((ROOT/'papers/editions/power-scheduling-2026-10-19.json').read_text())
        for entry in edition['files']:
            assert hashlib.sha256((ROOT/entry['path']).read_bytes()).hexdigest()==entry['sha256'],entry['path']
        print(json.dumps(dict(status=result['status'],verified_blocks=6,windows=120,hold_pps=1400,selected_pairs=42,precision_pooling=False,hardware_operations=0)))

if __name__=='__main__':
    main()
