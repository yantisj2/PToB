#!/usr/bin/env python3
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""Recompute selected hybrid phase metrics, not private raw timestamp replay."""
from collections import defaultdict
import csv
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parent
PAIRS=[('local300','AP300'),('local900','AP900'),('Huber_local900','Huber_AP900'),
       ('servo_local_60','servo_AP_60'),('servo_local_180','servo_AP_180'),('frozen_local','frozen_AP'),
       ('all900','AP900'),('all900','Huber_AP900')]

def stats(rows,key):
    rows=[r for r in rows if r[key] is not None]
    if not rows:return {'n':0}
    values=[r[key] for r in rows];n=len(values);mean=math.fsum(values)/n;absolute=sorted(map(abs,values))
    out={'n':n,'mean_us':mean,'centered_std_us':math.sqrt(math.fsum((v-mean)**2 for v in values)/n),
         'rms_us':math.sqrt(math.fsum(v*v for v in values)/n),'abs_p99_us':absolute[math.ceil(.99*n)-1],
         'max_abs_us':absolute[-1]}
    if n>1:
        xx=[r['local_s'] for r in rows];xm=math.fsum(xx)/n
        den=math.fsum((x-xm)**2 for x in xx)
        slope=math.fsum((x-xm)*(y-mean) for x,y in zip(xx,values))/den if den else 0.
        residual=[y-mean-slope*(x-xm) for x,y in zip(xx,values)]
        out.update(drift_ppm=slope,posthoc_detrended_RMS_us=math.sqrt(math.fsum(v*v for v in residual)/n))
    return out

def metrics(rows):
    known=[r for r in rows if r['verified'] and r['error_us'] is not None]
    fresh=[r for r in known if r['source_age_s'] is not None and r['source_age_s']<60]
    native=[r for r in rows if r['admitted'] and r['error_us'] is not None]
    native_fresh=[r for r in native if r['source_age_s'] is not None and r['source_age_s']<60]
    return {'nominal':900,'native_admitted_n':sum(r['admitted'] for r in rows),'verified_n':len(known),
            'raw_verified_n':sum(r['raw_verified'] for r in rows),
            'unknown_vs_900':900-len(known),'raw':stats([r for r in rows if r['raw_verified']],'raw_phase_us'),
            'fixed_base':stats(known,'error_us'),'trueage_under60':stats(fresh,'error_us'),
            'native_computed_raw':stats([r for r in rows if r['admitted']],'raw_phase_us'),
            'native_computed_fixed_base':stats(native,'error_us'),
            'native_computed_trueage_under60':stats(native_fresh,'error_us')}

def analyze(root=ROOT):
    bases=json.loads((root/'bases.json').read_text());provenance=json.loads((root/'provenance.json').read_text())
    grouped=defaultdict(dict)
    with (root/'future.csv').open(newline='') as f:
        for source in csv.DictReader(f):
            r=dict(source);run=r['run'];name=r['model'];r['pps']=int(r['pps'])
            assert 0<=r['pps']<900
            for k in ['native_local','native_AP','deep_local','deep_AP','admitted','raw_verified','base_verified','verified']:r[k]=bool(int(r[k]))
            for k in ['raw_phase_us','error_us','source_age_s']:r[k]=float(r[k]) if r[k] else None
            r['local_s']=(int(r['local_whole_us'])+float(r['local_fraction_us']))/1e6 if r['local_whole_us'] else None
            r['raw80_relative_ticks']=int(r['raw80_relative_ticks']) if r['raw80_relative_ticks'] else None
            base=bases[run][name]['base_us']
            assert r['admitted']==(r['raw_phase_us'] is not None)
            if r['admitted']:
                assert math.isfinite(r['raw_phase_us']) and -500000<=r['raw_phase_us']<500000
                assert r['source_age_s'] is not None and r['source_age_s']>=0
                if r['regime']=='continuous':assert r['source_age_s']<60
                if base is not None:assert abs(r['raw_phase_us']-base-r['error_us'])<1e-8
            else:assert r['error_us'] is None
            if r['verified']:assert r['admitted'] and r['deep_local'] and r['local_s'] is not None
            assert r['verified']==bool(r['raw_verified'] and r['base_verified'])
            assert r['base_verified']==bases[run][name]['base_deep_verified']
            if name.startswith(('AP','frozen_AP','Huber_AP','servo_AP')) and r['verified']:assert r['deep_AP']
            assert r['pps'] not in grouped[run].setdefault(name,{})
            grouped[run][name][r['pps']]=r
    result={'author':'Jonathan Yantis','license':'CC BY 4.0',
        'metric':'PPS-relative input phase after own prior GNSS fixed base; not calibrated UTC accuracy',
        'posthoc':'Future fitted trend removal is descriptive precision only; never used as corrected forecast accuracy',
        'reproduction_limit':provenance['reproduction_limit'],
        'paper_primary_runs':provenance.get('paper_primary_runs',list(grouped)),
        'runs':{}}
    assert set(result['paper_primary_runs'])<=set(grouped)
    for run,models in grouped.items():
        assert set(models)==set(bases[run])
        for rows in models.values():assert set(rows)==set(range(900))
        calsets={tuple(v['prior_PPS_relative_to_first_future']) for v in bases[run].values()}
        assert len(calsets)==1
        for base in bases[run].values():
            if base['base_us'] is not None:assert base['n']>=8 and len(next(iter(calsets)))>=8
        out={'models':{n:metrics(list(rr.values())) for n,rr in models.items()},'paired':{},
             'independent_replay_limited':not provenance['runs'][run]['all_admitted_method_outputs_deep_verified']}
        for ln,an in PAIRS:
            ids=[i for i in range(900) if all(models[n][i]['verified'] and models[n][i]['error_us'] is not None for n in (ln,an))]
            out['paired'][ln+'__'+an]={'common_n':len(ids),'unknown_vs_900':900-len(ids),
                'methods':{n:metrics([models[n][i] for i in ids]) for n in (ln,an)}}
            native_ids=[i for i in range(900) if all(models[n][i]['admitted'] and models[n][i]['error_us'] is not None for n in (ln,an))]
            out['paired'][ln+'__'+an].update(native_computed_common_n=len(native_ids),
                native_computed_methods={n:metrics([models[n][i] for i in native_ids]) for n in (ln,an)})
        ids=[i for i in range(900) if models['AP900'][i]['verified'] and models['AP900'][i]['error_us'] is not None]
        out['NTP_controls_on_AP900_common']={n:metrics([models[n][i] for i in ids]) for n in ('all300','all900','MIN8')}
        native_ids=[i for i in range(900) if models['AP900'][i]['admitted'] and models['AP900'][i]['error_us'] is not None]
        out['NTP_controls_on_native_AP900_common']={n:metrics([models[n][i] for i in native_ids]) for n in ('all300','all900','MIN8')}
        primary=out['paired']['all900__Huber_AP900']
        baseline=primary['methods']['all900']['fixed_base'];candidate=primary['methods']['Huber_AP900']['fixed_base']
        gain={}
        if baseline['n'] and candidate['n']:
            mse=baseline['rms_us']**2-candidate['rms_us']**2
            bias=baseline['mean_us']**2-candidate['mean_us']**2
            variance=baseline['centered_std_us']**2-candidate['centered_std_us']**2
            assert abs(mse-bias-variance)<1e-6
            gain={'MSE_reduction_us2':mse,'mean_squared_reduction_us2':bias,'variance_reduction_us2':variance,
                  'bias_fraction_of_positive_MSE_gain':bias/mse if mse>0 else None}
        out['primary_comparison']={'baseline':'all900','candidate':'Huber_AP900',
            'scope':'System comparison of locked NTP-only WLS and nominated robust hybrid; coordinate and weighting differ, so this does not isolate the AP oscillator contribution.',
            'metric':'Own fixed prior phase-corrected RMS, no future recentering or detrending; identical scored and prior-calibration PPS identities.',
            'common_n':primary['common_n'],'unknown_vs_900':primary['unknown_vs_900'],
            'decomposition':gain}
        diagnostic_ids=[i for i in ids if models['local900'][i]['verified'] and models['AP900'][i]['raw80_relative_ticks'] is not None]
        if len(diagnostic_ids)>1:
            first=diagnostic_ids[0];reference=models['AP900'][first];drows=[]
            for i in diagnostic_ids:
                row=models['AP900'][i];seconds=i-first
                raw=(row['raw80_relative_ticks']-reference['raw80_relative_ticks'])/80
                local=(row['local_s']-reference['local_s'])*1e6
                drows.append({'local_s':seconds,'raw':raw-seconds*1e6,'local':local-seconds*1e6,'bridge':local-raw})
            out['coordinate_diagnostic']={'n':len(drows),
                'scope':'Posthoc future rate versus nominal1Hz PPS ordinal, identical AP-common witnesses; not steering or UTC calibration',
                'raw80':stats(drows,'raw'),'mapped_local':stats(drows,'local'),'local_minus_raw80':stats(drows,'bridge')}
        result['runs'][run]=out
    return result

def tables(result):
    header='% Generated by hybrid/analyze.py; CC BY 4.0 Jonathan Yantis.\n'
    runs=result.get('paper_primary_runs',list(result['runs']))
    limited=any(result['runs'][run]['independent_replay_limited'] for run in runs)
    def display_run(run):
        return run+(r'$^{\dagger}$' if result['runs'][run]['independent_replay_limited'] else '')
    def population(run,pair):
        p=result['runs'][run]['paired'][pair]
        if result['runs'][run]['independent_replay_limited']:
            return p['native_computed_common_n'],p['native_computed_methods'],'native_computed_'
        return p['common_n'],p['methods'],''
    def value(s,key='rms_us',places=3):
        return f'{s[key]:.{places}f}' if s.get('n',0) else '--'
    qualification=(r'\par\smallskip $\dagger$ Native-computed observations with incomplete independent input or fixed-base history; not a verified comparison.' if limited else '')
    frozen=[header,r'\begin{table}[t]\centering\small',
        r'\caption{Frozen forecasts after 900 s training: PPS-relative errors after the fixed prior phase base. All 900 future opportunities remain in each run. Beyond true source age 60 s these are research holdover, not qualified UTC service.}',
        r'\label{tab:hybrid-frozen}\begin{tabular}{lrrrrr}\toprule',
        r'Run & Known/900 & AP RMS & Local RMS & Fresh $n$ & Fresh AP/local RMS \\ \midrule']
    for run in runs:
        count,p,prefix=population(run,'frozen_local__frozen_AP');a=p['frozen_AP'];l=p['frozen_local']
        fixed=prefix+'fixed_base';fresh=prefix+'trueage_under60'
        frozen.append(f"{display_run(run)} & {count}/900 & {value(a[fixed])} & {value(l[fixed])} & {a[fresh]['n']} & {value(a[fresh])}/{value(l[fresh])} \\\\")
    frozen.extend([r'\bottomrule\end{tabular}',r'\par\smallskip All RMS entries are in $\mu$s; fresh means true source age $<60$ s.',qualification,r'\end{table}'])
    role=('P1 is exploratory; later runs use the locked candidates unchanged.' if 'P1' in runs else
          'All primary runs use the locked candidates unchanged; historical exploratory P1 remains in the selected data.')
    filt=[header,r'\begin{table}[t]\centering\small',
        r'\caption{Continuously updated offline methods, fixed own prior phase bases. Each cell is matched local/AP RMS in $\mu$s on the AP-common population. '+role+r' Huber weights are AP-derived and shared, so its local branch is also AP-assisted. No validation winner is selected.}',
        r'\label{tab:hybrid-filters}\begin{tabular}{l'+'r'*len(runs)+r'}\toprule',
        'Method & '+' & '.join(display_run(run) for run in runs)+r' \\ \midrule']
    labels=['WLS 300 s','WLS 900 s','Shared-AP Huber 900 s','Continuous servo 60 s','Continuous servo 180 s']
    for label,(ln,an) in zip(labels,PAIRS[:5]):
        cells=[]
        for run in runs:
            _,p,prefix=population(run,ln+'__'+an);key=prefix+'fixed_base'
            cells.append(value(p[ln][key],places=2)+'/'+value(p[an][key],places=2))
        filt.append(label+' & '+' & '.join(cells)+r' \\')
    filt.append(r'\midrule')
    for name,label in [('all300','All-NTP WLS 300 s'),('all900','All-NTP WLS 900 s'),('MIN8','Original MIN8')]:
        cells=[]
        for run in runs:
            prefix='native_computed_' if result['runs'][run]['independent_replay_limited'] else ''
            controls='NTP_controls_on_native_AP900_common' if prefix else 'NTP_controls_on_AP900_common'
            cells.append(value(result['runs'][run][controls][name][prefix+'fixed_base'],places=2))
        filt.append(label+' & '+' & '.join(cells)+r' \\')
    filt.extend([r'\bottomrule\end{tabular}',r'\par\smallskip Single values use all-NTP inputs, scored on the same AP-common PPS subset.',qualification,r'\end{table}'])
    accuracy=[header,r'\begin{table}[t]\centering\small',
        r'\caption{Prior-phase-corrected errors against shadow PPS in $\mu$s. NTP-only WLS and nominated robust AP hybrid use identical scored witnesses; ordinary AP WLS and the servo retain their declared paired populations. Each method uses its own fixed phase base on the same prior PPS subset, with no validation recentering or detrending.}',
        r'\label{tab:hybrid-accuracy}\begin{tabular}{llrrrrrr}\toprule',
        r'Run & Method & $n$ & Mean & Scatter & RMS & Abs. P99 & Max. abs. \\ \midrule']
    for run in runs:
        for n,label,pair in [('all900','NTP-only WLS','all900__Huber_AP900'),('Huber_AP900','Robust AP','all900__Huber_AP900'),
                             ('AP900','AP WLS','all900__AP900'),('servo_AP_60','AP servo 60','servo_local_60__servo_AP_60')]:
            _,p,prefix=population(run,pair);s=p[n][prefix+'fixed_base']
            accuracy.append(display_run(run)+' & '+label+' & '+str(s['n'])+' & '+' & '.join(value(s,k,2) for k in ['mean_us','centered_std_us','rms_us','abs_p99_us','max_abs_us'])+r' \\')
    accuracy.extend([r'\bottomrule\end{tabular}',qualification,r'\end{table}'])
    jitter=[header,r'\begin{table}[t]\centering\small',
        r'\caption{Training residual scatter includes NTP source, path, client endpoints and coordinate effects. It cannot isolate source-only jitter. Minima refer to the canonical frozen fit, before Huber downweighting.}',
        r'\label{tab:hybrid-jitter}\begin{tabular}{lrrrr}\toprule',
        r'Run & Accepted NTP (ms) & Local minima ($\mu$s) & AP minima ($\mu$s) & Gate rejects \\ \midrule']
    provenance=json.loads((ROOT/'provenance.json').read_text())
    for run in runs:
        p=provenance['runs'][run];j=p['training_transfer_scatter']
        jitter.append(f"{run} & {j['all']['all_pre_cutoff_accepted_mappable_exchanges']['fit_residual']['centered_std_us']/1000:.3f} & {j['matched']['frozen_retained_burst_minima']['fit_residual']['centered_std_us']:.2f} & {j['ap']['frozen_retained_burst_minima']['fit_residual']['centered_std_us']:.2f} & {p['native_training_gate_counts']['matched_rejected']} \\\\")
    jitter.extend([r'\bottomrule\end{tabular}',r'\end{table}'])
    products={'accuracy-table.tex':'\n'.join(accuracy)+'\n','filter-table.tex':'\n'.join(filt)+'\n',
              'jitter-table.tex':'\n'.join(jitter)+'\n','frozen-table.tex':'\n'.join(frozen)+'\n'}
    products['tables.tex']=header+'\n'.join(r'\input{hybrid/'+n+'}' for n in products)+'\n'
    return products

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--write',action='store_true');a=p.parse_args();result=analyze()
    if a.write:
        (ROOT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
        for name,source in tables(result).items():(ROOT/name).write_text(source)
        print('Recomputed selected hybrid metrics and TeX tables')
    else:print(json.dumps(result,indent=2))
