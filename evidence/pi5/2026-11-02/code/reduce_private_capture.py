"""Private reduction. Copyright 2026 Jonathan Yantis. Code MIT; observations CC BY 4.0."""
import bisect,csv,hashlib,json,math,os,pathlib,statistics,sys,datetime,collections
from decimal import Decimal
os.umask(0o077)
B=pathlib.Path(__file__).resolve().parent
def stamp(raw):
 t,s=raw.split('#');n=Decimal(t)*1000000000
 if n!=n.to_integral_value():raise ValueError('not integral nanoseconds')
 return int(n),int(s)
def stats(values):
 if not values:return dict(n=0)
 n=len(values);mean=math.fsum(values)/n;a=sorted(abs(v) for v in values)
 return dict(n=n,mean_ns=mean,rms_ns=math.sqrt(math.fsum(v*v for v in values)/n),sd_population_ns=math.sqrt(math.fsum((v-mean)**2 for v in values)/n),
  p99_abs_ns=a[math.ceil(.99*n)-1],max_abs_ns=a[-1],above_1us=sum(v>1000 for v in a),above_2us=sum(v>2000 for v in a),above_5us=sum(v>5000 for v in a))
def describe(values):
 return dict(n=len(values),min=min(values),median=statistics.median(values),max=max(values),mean=statistics.mean(values)) if values else dict(n=0)
def writecsv(path,rows):
 if not rows:return
 with path.open('x',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
def unitmap(row):
 result={}
 for block in row.get('stdout','').strip().split('\n\n'):
  item=dict(line.split('=',1) for line in block.splitlines() if '=' in line)
  if 'Id' in item:result[item['Id']]=item
 return result
def reduce(label):
 path=B/(label+'.json');d=json.loads(path.read_text());rows=d['snapshots'];start=d['start'];end=d['end']
 # Boundary source observations improve descriptive bracketing only; the fixed
 # 1Hz snapshot denominator remains the acquisition population.
 status=[d['initial']['status']]+rows+[d['post_status']]
 status=sorted(status,key=lambda r:r['boottime']);times=[r['boottime'] for r in status]
 cohorts={}
 def cohort(r):
  key=tuple(r.get('context') or [])
  if key not in cohorts:cohorts[key]='cohort%02d'%(len(cohorts)+1)
  return cohorts[key]
 for r in status:cohort(r)
 edges=[];previous=None;gaps=[];skips=0;period=[];seeds=[]
 for e in d['edges']:
  tick,seq=stamp(e['raw'])
  if e['seed']:seeds.append(e);previous=(tick,seq,True);continue
  j=bisect.bisect_left(times,e['boottime']);eligible=False;co=None;bracket_s=None
  if 0<j<len(status):
   a,b=status[j-1],status[j];bracket_s=b['boottime']-a['boottime']
   eligible=bool(a['eligible'] and b['eligible'] and a['context']==b['context'] and bracket_s<=1.6)
   if a['context']==b['context']:co=cohort(a)
  row=dict(elapsed_s=e['boottime']-start,sequence_relative=seq-stamp(seeds[0]['raw'])[1],phase_ns=(tick+500000000)%1000000000-500000000,
   status_bracket_eligible=eligible,cohort=co,status_bracket_s=bracket_s,interval_ns=None,sequence_delta=None)
  if previous is not None:
   interval=tick-previous[0];delta=seq-previous[1];row.update(interval_ns=interval,sequence_delta=delta)
   if not previous[2] and delta==1 and 500000000<interval<1500000000:period.append(interval-1000000000)
   elif not (previous[2] and delta==1):
    skipped=max(0,delta-1);skips+=skipped
    gaps.append(dict(elapsed_s=e['boottime']-start,sequence_delta=delta,interval_ns=interval,unobserved_assertions=skipped,starts_at_seed=previous[2],
     nominal_missing_slots=max(0,(interval+500000000)//1000000000-delta) if not previous[2] and delta>0 and interval>0 else None))
  edges.append(row);previous=(tick,seq,False)
 writecsv(B/(label+'-edges.csv'),edges)
 snaps=[];updates=[];seen=set();event_counts={}
 for r in rows:
  s=r['status'];rx=s.get('receiver',{}).get('data') or {};fit=rx.get('fit') or {};tr=s.get('tracking') or {};src=next((x for x in s.get('source_stats',[]) if x.get('name')=='PTOB'),{})
  row=dict(elapsed_s=r['boottime']-start,eligible=r['eligible'],workload_good=r.get('workload_good'),chain_complete=s.get('chain',{}).get('complete'),cohort=cohort(r),firmware=rx.get('version'),source_sha256=rx.get('source'),
   receiver_fresh=s.get('receiver',{}).get('fresh'),pps_fresh=s.get('pps',{}).get('fresh'),pps_emitted=rx.get('pps_emitted'),pps_rejected=rx.get('pps_rejected'),
   error=rx.get('error'),receiver_error=rx.get('receiver_error'),capture_error=rx.get('capture_error'),uart_error=rx.get('uart_error'),
   hat_fit_locked=fit.get('locked'),hat_fit_samples=fit.get('samples'),hat_fit_rms_valid=fit.get('rms_valid'),
   hat_fit_rms_ns=fit.get('rms_ns') if fit.get('rms_valid') is True else None,
   hat_fit_age_ms=fit.get('age_ms') if fit.get('age_valid') is True else None,hat_drift_ppb=fit.get('drift_ppb') if fit.get('drift_valid') is True else None,
   chrony_reference=tr.get('reference'),chrony_leap=tr.get('leap'),chrony_rms_estimator_ns=tr.get('tracking_rms_ns'),chrony_last_offset_ns=tr.get('tracking_offset_ns'),
   chrony_pending_offset_ns=tr.get('system_offset_ns'),chrony_frequency_ppm=tr.get('frequency_ppm'),chrony_source_residual_sd_ns=src.get('stddev_ns'),
   chrony_source_samples=src.get('samples'),chrony_source_span_s=src.get('span_s'),temperature_mC=r.get('temperature'),
   server_ready=r.get('server',{}).get('ready'),server_running=r.get('server',{}).get('running'),server_reason=r.get('server',{}).get('reason'),
   transfer_ready=r.get('transfer',{}).get('ready'),transfer_running=r.get('transfer',{}).get('running'),transfer_reason=r.get('transfer',{}).get('reason'),
   transfer_fresh=r.get('transfer_fresh'),server_fresh=r.get('server_fresh'),
   transfer_capture_midpoint_offset_ns=r.get('transfer',{}).get('capture',{}).get('offset_ns'),transfer_capture_bracket_ns=r.get('transfer',{}).get('capture',{}).get('bracket_ns'),
   server_capture_midpoint_offset_ns=r.get('server',{}).get('capture',{}).get('offset_ns'),server_capture_bracket_ns=r.get('server',{}).get('capture',{}).get('bracket_ns'),
   ethernet_link=s.get('ethernet_link'),clock_read_bracket_ns=r['clock_bracket_ns'][1]-r['clock_bracket_ns'][0])
  snaps.append(row)
  if tr.get('reference_time') is not None and tr.get('reference_time') not in seen:seen.add(tr.get('reference_time'));updates.append(row)
  for ev in s.get('receiver',{}).get('events',[]):event_counts[(ev.get('boot'),ev.get('sequence'))]=ev
 writecsv(B/(label+'-snapshots.csv'),snaps)
 writecsv(B/(label+'-chrony-updates.csv'),updates)
 u0=unitmap(d['initial']['units']);u1=unitmap(d['final']['units'])
 service_obs=d.get('service_observations',[])
 service_same=bool(u0 and u0==u1 and all(unitmap(r['units'])==u0 for r in service_obs))
 configs_same=d['initial']['files']==d['final']['files'];effective_same=d['initial']['effective_chrony']==d['final']['effective_chrony']
 core_cohorts={tuple(r['context']) for r in rows}
 source_changes=[dict(elapsed_s=b['boottime']-start,from_cohort=cohort(a),to_cohort=cohort(b)) for a,b in zip(status,status[1:]) if a['context']!=b['context']]
 journal_lines=d.get('journal',{}).get('stdout','').splitlines();journal=[]
 for line in journal_lines:
  try:journal.append(json.loads(line))
  except ValueError:pass
 anomalies=[dict(unit=x.get('_SYSTEMD_UNIT'),message=x.get('MESSAGE')) for x in journal if any(k in str(x.get('MESSAGE','')).lower() for k in ('step','restart','stopp','fail','watchdog','segfault'))]
 phase=stats([e['phase_ns'] for e in edges]);eligiblephase=stats([e['phase_ns'] for e in edges if e['status_bracket_eligible']])
 eligible_count=sum(r['eligible'] for r in rows);complete_count=sum(r['status'].get('chain',{}).get('complete') is True for r in rows)
 transitions={}
 for r in status:
  for role in ['transfer','server']:
   for t in r.get(role,{}).get('transitions',[]):
    at=t.get('at_boottime_s')
    if at is not None and start<=at<=end:transitions[(role,at)]=dict(role=role,elapsed_s=at-start,ready=t.get('ready'),running=t.get('running'),reason=t.get('reason'))
 generations={role:len({r.get(role,{}).get('generation') for r in rows}) for role in ['transfer','server']}
 workload_good=sum(r.get('workload_good') is True for r in rows)
 source_available=bool(599.9<=end-start<=600.25 and len(rows)==600 and eligible_count==600 and len(edges)==600 and len(core_cohorts)==1 and not source_changes and not gaps and skips==0 and service_same and configs_same and effective_same and not d['errors'] and not d.get('failure') and not d.get('operator_stop'))
 available=bool(source_available and workload_good==600 and d['initial']['status'].get('workload_good') is True and d['post_status'].get('workload_good') is True and all(v==1 for v in generations.values()) and not transitions)
 summary=dict(schema=1,run=label,evidence_status='Private unreviewed observations; no public evidence or accuracy qualification',created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
  attribution='Jonathan Yantis',observation_license='CC BY 4.0',code_license='MIT',raw_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),observer_source_sha256=d['observer_source_sha256'],
  capture_duration_s=end-start,start_utc=datetime.datetime.fromtimestamp(d['start_utc_ns']/1e9,datetime.timezone.utc).isoformat(),end_utc=datetime.datetime.fromtimestamp(d['end_utc_ns']/1e9,datetime.timezone.utc).isoformat(),
  cached_seed_count=len(seeds),new_assertions=len(edges),expected_nominal_pulses=600,status_snapshots=len(rows),eligible_snapshots=eligible_count,chain_complete_snapshots=complete_count,
  source_cohorts=len(core_cohorts),source_changes=source_changes,firmware_versions=sorted({s['firmware'] for s in snaps}),firmware_sources=sorted({s['source_sha256'] for s in snaps}),
  all_new_edge_phase=phase,source_bracket_eligible_phase=eligiblephase,consecutive_new_edge_period_error=stats(period),observed_sequence_skips=skips,gaps=gaps,
  config_hashes_unchanged=configs_same,effective_chrony_unchanged=effective_same,service_identity_and_restart_counts_unchanged=service_same,service_observation_count=len(service_obs)+2,
  service_summary={k:{a:v.get(a) for a in ['ActiveState','SubState','MainPID','NRestarts','ExecMainStartTimestampMonotonic']} for k,v in u0.items()},
  source_window_availability_criteria_met=source_available,measured_window_availability_criteria_met=available,availability_scope='PToB source through M-HAT GPIO PPS to selected Pi chrony, with fresh running PHC transfer and ready/running Ethernet PTP guard; physical Ethernet link/endpoint accuracy remains separate',independent_output_accuracy_measured=False,
  chrony_distinct_reference_updates=len(updates),chrony_distinct_last_offset=stats([s['chrony_last_offset_ns'] for s in updates if s['chrony_last_offset_ns'] is not None]),
  chrony_rms_estimator_snapshots=describe([s['chrony_rms_estimator_ns'] for s in snaps if s['chrony_rms_estimator_ns'] is not None]),
  chrony_rms_estimator_distinct_reference_updates=describe([s['chrony_rms_estimator_ns'] for s in updates if s['chrony_rms_estimator_ns'] is not None]),
  chrony_source_residual_sd_snapshots=describe([s['chrony_source_residual_sd_ns'] for s in snaps if s['chrony_source_residual_sd_ns'] is not None]),
  hat_fit_rms_snapshots=describe([s['hat_fit_rms_ns'] for s in snaps if s['hat_fit_rms_ns'] is not None]),
  server_ready_snapshots=sum(s['server_ready'] is True for s in snaps),transfer_ready_snapshots=sum(s['transfer_ready'] is True for s in snaps),
  server_running_snapshots=sum(s['server_running'] is True for s in snaps),transfer_running_snapshots=sum(s['transfer_running'] is True for s in snaps),
  server_fresh_snapshots=sum(s['server_fresh'] is True for s in snaps),transfer_fresh_snapshots=sum(s['transfer_fresh'] is True for s in snaps),
  transfer_ready_raw_unknown_snapshots=sum(s['transfer_ready'] is None for s in snaps),workload_good_snapshots=workload_good,clock_generation_counts=generations,workload_transitions_within_window=list(transitions.values()),
  transfer_capture_midpoint_offset_ns=describe([s['transfer_capture_midpoint_offset_ns'] for s in snaps if s['transfer_capture_midpoint_offset_ns'] is not None]),
  transfer_capture_bracket_ns=describe([s['transfer_capture_bracket_ns'] for s in snaps if s['transfer_capture_bracket_ns'] is not None]),
  server_reason_counts=dict(collections.Counter(s['server_reason'] for s in snaps)),transfer_reason_counts=dict(collections.Counter(s['transfer_reason'] for s in snaps)),
  ethernet_link_snapshots=sum(s['ethernet_link'] is True for s in snaps),journal_records=len(journal),journal_anomaly_candidates=anomalies,
  retained_receiver_events=len(event_counts),acquisition_errors=d['errors'],acquisition_failure=d.get('failure'),export_errors=d['export_errors'],
  log_export_errors={k:v for k,v in d.get('logs',{}).items() if 'error' in v},
  limits=['Raw phase is GPIO IRQ time relative to the chrony-disciplined Pi UTC second, including IRQ latency and clock slew.',
   'No independent UTC, upstream GM phase, electrical PPS output or Ethernet accuracy measurement.',
   'One-Hz eligibility brackets describe lineage around an observed edge; they cannot prove exact-event authentication.',
   'Cached seed is retained and excluded from new-edge phase and seed-to-first period statistics.',
   'No filtering or error-magnitude selection. P99 is nearest rank; this finite population cannot bound rare tails.',
   'Status snapshots repeat estimator values. Chrony estimator RMS, source regression SD and HAT fit RMS differ from raw PPS phase.',
   'Pulse-gap counts are inferred from integer realtime intervals; clock steps can imitate gaps. Sequence skips count unseen assertions separately.',
   'Availability criteria apply only to this measured600s window; no endurance or guaranteed submicrosecond claim.'])
 (B/(label+'-summary.json')).write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n')
 report=f'''# Independent Pi/M-HAT clean-window observation — {label}

Private, unreviewed laboratory observations. Copyright 2026 Jonathan Yantis. Observations and derived CSV/figures: CC BY 4.0; observer/reduction code: MIT.

The fixed window lasted {end-start:.6f}s and retained {len(edges)}/600 expected new PPS assertions plus {len(seeds)} excluded cached seed. There were {len(rows)} status observations; {eligible_count} eligible and {complete_count} complete-chain snapshots. The full source cohort count was {len(core_cohorts)}. Measured-window availability criteria met: **{available}**.

Raw PPS phase, all {phase.get('n',0)} new assertions: bias {phase.get('mean_ns',float('nan')):.3f}ns, RMS {phase.get('rms_ns',float('nan')):.3f}ns, centered population SD {phase.get('sd_population_ns',float('nan')):.3f}ns. Nearest-rank |phase|P99 {phase.get('p99_abs_ns')}ns; maximum {phase.get('max_abs_ns')}ns. Strict counts above1/2/5us: {phase.get('above_1us')}/{phase.get('above_2us')}/{phase.get('above_5us')}. All tails remain. Descriptively source-bracket-eligible edges: {eligiblephase.get('n',0)}/{len(edges)}.

Unobserved assertion sequence skips: {skips}. Gap intervals: {len(gaps)}. Source transitions: {len(source_changes)}. Configuration hashes unchanged: {configs_same}; effective chrony unchanged: {effective_same}; service PIDs/start identities/restart counts unchanged: {service_same} across {len(service_obs)+2} observations. Acquisition errors: {len(d['errors'])}; export errors: {len(d['export_errors'])}.

Chrony estimator RMS, source regression residual SD and HAT fit RMS are separate snapshot metrics in the summary and CSV. Chrony distinct reference-time updates: {len(updates)}. Ethernet-link-up snapshots: {summary['ethernet_link_snapshots']}/{len(rows)}. No network transport accuracy is inferred.

Fresh running PHC transfer: {summary['transfer_running_snapshots']}/{len(rows)} running and {summary['transfer_fresh_snapshots']}/{len(rows)} fresh. Raw transfer.ready field unknown/absent: {summary['transfer_ready_raw_unknown_snapshots']}/{len(rows)}; that schema does not assert a Boolean ready when running. Ethernet PTP guard ready: {summary['server_ready_snapshots']}/{len(rows)}, running: {summary['server_running_snapshots']}/{len(rows)}. Combined source/PHC/PTP workload health: {workload_good}/{len(rows)}. Transfer reasons: {json.dumps(summary['transfer_reason_counts'])}. Server reasons: {json.dumps(summary['server_reason_counts'])}. Physical Ethernet link and downstream endpoint measurements remain separate.

Raw phase is the kernel GPIO IRQ timestamp against the disciplined Pi system second. It combines the transferred source timing, HAT output/route, Pi interrupt latency and chrony slew. It is not an independent UTC or electrical output error measurement. The one-Hz lineage brackets cannot authenticate an exact pulse. Finite-window availability is not endurance qualification and does not guarantee submicrosecond tails.

The preceding operator-confirmed GM handover belongs to the retained earlier population and is not pooled here. Selection for this window used source/service/pulse continuity, never phase magnitude. No firmware, mesh selection, clock configuration, service restart or trace probe was changed for this capture.
'''
 (B/(label+'-report.md')).write_text(report)
 outputs=[path,B/(label+'-edges.csv'),B/(label+'-snapshots.csv'),B/(label+'-chrony-updates.csv'),B/(label+'-summary.json'),B/(label+'-report.md')]
 provenance=dict(schema=1,attribution='Jonathan Yantis',observation_license='CC BY 4.0',code_license='MIT',redactions='Derived CSV removes absolute timestamps, enrolled hardware IDs, root/parent/boot/context IDs and network addresses. Cohort labels preserve transitions; firmware source hashes remain.',
  files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in outputs},code={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [B/'observer_remote.py',B/'run.py',B/'analyze.py']})
 (B/(label+'-provenance.json')).write_text(json.dumps(provenance,indent=2)+'\n')
 print(json.dumps(summary,indent=2))
if __name__=='__main__':reduce(sys.argv[1])
