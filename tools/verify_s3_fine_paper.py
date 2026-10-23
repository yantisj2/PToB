#!/usr/bin/env python3
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""Verify public selected fine-timing data and matching edition; no device access."""
import argparse,csv,hashlib,json,math
from pathlib import Path

def metrics(x):
 n=len(x)
 if not n:return {'n':0}
 m=math.fsum(x)/n;r2=math.fsum(v*v for v in x)/n;a=sorted(map(abs,x))
 return dict(n=n,mean_ns=m,rms_ns=math.sqrt(r2),std_ns=math.sqrt(max(0,r2-m*m)),min_ns=min(x),max_ns=max(x),max_abs_ns=a[-1],abs_p50_ns=a[math.ceil(n*.5)-1],abs_p95_ns=a[math.ceil(n*.95)-1],abs_p99_ns=a[math.ceil(n*.99)-1])
def compare(a,b):
 if isinstance(a,dict):
  for k,v in a.items():compare(v,b[k])
 elif isinstance(a,list):
  assert len(a)==len(b)
  for x,y in zip(a,b):compare(x,y)
 elif isinstance(a,float):assert math.isclose(a,b,rel_tol=1e-10,abs_tol=1e-7),(a,b)
 else:assert a==b,(a,b)
def safe(root,path):
 p=(root/path).resolve();assert p.is_relative_to(root.resolve());return p

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1]);ap.add_argument('--require-final-check',action='store_true');args=ap.parse_args();root=args.root;e=root/'evidence/s3/2026-10-22-fine'
 with (e/'pps-sanitized.csv').open(newline='') as f:rows=list(csv.DictReader(f))
 result=json.loads((e/'results.json').read_text());protocol=json.loads((e/'protocol.json').read_text()); computed={}
 for cohort in sorted({r['cohort'] for r in rows}):
  allrows=[r for r in rows if r['cohort']==cohort];assert [int(r['ordinal']) for r in allrows]==list(range(1,len(allrows)+1))
  for r in allrows:
   flags=['diagnostic_enabled','map_valid','reference_seen','reference_fix','reference_pps','same_scope'];assert all(r[k] in ['0','1'] for k in flags+['valid','absolute_utc_associated'])
   eligible=r['reason']=='0' and all(r[k]=='1' for k in flags);assert eligible==(r['valid']=='1');assert r['absolute_utc_associated']=='0'
   assert math.isclose(float(r['relative_edge_s']),int(r['relative_edge_ticks16m'])/16000000,abs_tol=1e-10)
  ticks=[int(r['relative_edge_ticks16m']) for r in allrows];seq=[int(r['sequence_from_first']) for r in allrows];assert len(seq)==len(set(seq));assert all(b>a for a,b in zip(ticks,ticks[1:]));assert all(b>a for a,b in zip(seq,seq[1:]))
  valid=[r for r in allrows if r['valid']=='1'];score=metrics([int(r['phase_ns']) for r in valid]);score.update(recorded=len(allrows),valid=len(valid),invalid=len(allrows)-len(valid),interior_missing=sum(max(0,b-a-1) for a,b in zip(seq,seq[1:])),duplicate_rows=0,edge_span_s=(ticks[-1]-ticks[0])/16000000)
  if cohort=='fine50909_fixed660s':
   assert len(allrows)==660 and len(valid)==657 and all(0<=t<660*16000000 for t in ticks)
   score['minute_blocks']=[dict(start_s=k*60,end_s=(k+1)*60,**metrics([int(r['phase_ns']) for r in valid if k*60*16000000<=int(r['relative_edge_ticks16m'])<(k+1)*60*16000000])) for k in range(11)]
   subset=dict(subset_of=cohort,not_additional_population=True,**metrics([int(r['phase_ns']) for r in valid[:48]]));compare(result['cohorts']['fine50909_first48_subset'],subset)
  compare(result['cohorts'][cohort],score);computed[cohort]=score
 fixed=protocol['fine660'];assert fixed['status'].startswith('COMPLETE') and fixed['post_window_sequence_fence'];assert fixed['nominal_1Hz_opportunities']==fixed['valid']+fixed['invalid']+fixed['nominal_unrecorded']==660;assert fixed['interior_sequence_missing']==fixed['duplicates']==fixed['nominal_unrecorded']==0
 assert fixed['unread_after_observed_post_window_fence']==3 and fixed['ram_retention_is_not_cursor_loss'];assert not any(fixed['request_loss_deltas'].values())
 offset=result['offset_selection'];assert -round(computed['fine_nominal_training48']['mean_ns'])==offset['nominal_training_additional_ns']==10909;assert 50909-round(computed['fine50909_fixed660s']['mean_ns'])==offset['reused_after_completion_total_ns']==51016;compare(computed['fine50909_fixed660s']['std_ns'],offset['mean_only_counterfactual_rms_floor_ns']);assert offset['counterfactual_is_not_actual_51016_result']
 longrows=[r for r in rows if r['cohort']=='fine50909_fixed660s' and r['valid']=='1'];tails=[r for r in longrows if abs(int(r['phase_ns']))>1000]
 tail=dict(eligible=len(longrows),threshold_absolute_ns=1000,exceedances=len(tails),eligible_fraction=len(tails)/len(longrows),uncentered_squared_error_fraction=sum(int(r['phase_ns'])**2 for r in tails)/sum(int(r['phase_ns'])**2 for r in longrows),all_tails_retained=True,exceedances_over200ns=sum(abs(int(r['phase_ns']))>200 for r in longrows),records=[dict(elapsed_ticks16m=int(r['relative_edge_ticks16m']),elapsed_s=float(r['relative_edge_s']),phase_ns=int(r['phase_ns'])) for r in tails]);compare(result['fine660_tail_accounting'],tail)
 association=json.loads((e/'association.json').read_text());a=association['fixed128'];assert a['heldout']==a['heldout_scored']+a['heldout_unpaired']==64;assert a['selected']==128 and not a['target_below100ns'];assert a['fine_causal_rms_ns']<a['legacy_causal_rms_ns'] and a['fine_causal_rms_ns']>100
 with (e/'internal-quality-sanitized.csv').open(newline='') as f:internal=list(csv.DictReader(f))
 assert [int(r['ordinal']) for r in internal]==list(range(1,len(internal)+1))
 assert all(r['fault']=='0' for r in internal)
 assert all(float(b['uptime_s'])>float(a['uptime_s']) for a,b in zip(internal,internal[1:]))
 assert len(internal)==313 and any(float(r['uptime_s'])>=761.056472 for r in internal)
 finite=[r for r in internal if r['rolling_causal_rms_ns']!=''];assert len(finite)==311 and sum(r['rolling_causal_rms_ns']=='49' for r in finite)==3
 transfer=protocol['final51016'];assert transfer['status'].startswith('COMPLETE') and transfer['recorded']==64 and transfer['valid']==48 and transfer['invalid']==16
 assert transfer['interior_sequence_missing']==transfer['duplicates']==0 and transfer['terminal_unread_after_first48']==1
 assert transfer['not_mean_zero_portable'] and not transfer['sub100ns_target_met'] and transfer['no_within_run_retuning']
 assert not transfer['N31_serial_opened'] and not transfer['N31_mutations'] and transfer['TC04_restored_released']
 assert computed['n31_51016_transfer48']['recorded']==64 and computed['n31_51016_transfer48']['valid']==48
 if args.require_final_check:assert transfer['status'].startswith('COMPLETE'),'Final bounded check not yet incorporated'
 for receipt in [e/'manifest.json',root/'papers/editions/s3-timing-2026-10-22-fine.json']:
  assert receipt.is_file(), f'Missing selected receipt: {receipt.name}'
  if receipt.is_file():
   for r in json.loads(receipt.read_text())['files']:
    data=safe(root,r['path']).read_bytes();assert hashlib.sha256(data).hexdigest()==r['sha256'],r['path'];assert len(data)==r['bytes'],r['path']
 print(json.dumps(dict(status='PASS_PUBLIC_FINE_DATA_AND_SELECTED_HASHES',cohorts={k:{f:v[f] for f in ['recorded','valid','invalid','mean_ns','rms_ns','std_ns','max_abs_ns']} for k,v in computed.items()},final_check=protocol['final51016']['status']),indent=2))
if __name__=='__main__':main()
