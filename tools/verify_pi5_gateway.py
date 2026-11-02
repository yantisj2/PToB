#!/usr/bin/env python3
"""Recompute final Pi/M-HAT selected evidence offline. Jonathan Yantis. MIT."""
import csv,hashlib,json,math,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'evidence/pi5/2026-11-02';PAPER=ROOT/'papers/pi5-mhat-gateway'
def require(value,message):
 if not value:raise SystemExit('FAIL: '+message)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(name):
 with (BASE/name).open(newline='') as f:return list(csv.DictReader(f))
def stats(values):
 n=len(values);mean=math.fsum(values)/n;a=sorted(map(abs,values))
 return dict(n=n,mean_ns=mean,rms_ns=math.sqrt(math.fsum(v*v for v in values)/n),sd_population_ns=math.sqrt(math.fsum((v-mean)**2 for v in values)/n),p99_abs_ns=a[math.ceil(.99*n)-1],max_abs_ns=a[-1],above_1us=sum(v>1000 for v in a),above_2us=sum(v>2000 for v in a),above_5us=sum(v>5000 for v in a))
def compare(observed,expected,label):
 for key,value in observed.items():require(abs(value-expected[key])<1e-8,label+' '+key)
def describe(values):
 a=sorted(values);n=len(a)
 return dict(n=n,min=a[0],median=(a[(n-1)//2]+a[n//2])/2,max=a[-1],mean=math.fsum(values)/n)
def main():
 m=json.loads((BASE/'manifest.json').read_text());r=m['run']
 require(m['status']=='AUTHOR_REVIEW_DRAFT_NOT_PUBLICATION_APPROVED','draft status')
 require(m['observation_license']=='CC BY 4.0' and m['code_license']=='MIT','licenses')
 require(set(x['file'] for x in m['files'])=={'final-edges.csv','final-status.csv','protocol-ptp.csv','protocol-health.json'},'final-only selection')
 for entry in m['files']+m['provenance']['code']:
  path=(BASE/entry['file']).resolve();require(path.is_relative_to(BASE),'bounded selected path')
  require(digest(path)==entry['sha256'],entry['file']+' identity')
  if 'rows' in entry:require(len(read(entry['file']))==entry['rows'],entry['file']+' denominator')
 edges=read('final-edges.csv');status=read('final-status.csv');packets=read('protocol-ptp.csv')
 require(len(edges)==len(status)==r['new_assertions']==r['status_snapshots']==600,'fixed complete populations')
 require(599.9<=r['capture_duration_s']<=600.25 and r['cached_seed_count']==1,'duration and excluded seed')
 edge_fields={'index','elapsed_ns','phase_ns','interval_ns','sequence_delta','nominal_missing_before','status_bracket_eligible','cohort'}
 require(set(edges[0])==edge_fields,'edge schema excludes absolute times and identifiers')
 for rows in (edges,status):
  require([int(x['index']) for x in rows]==list(range(1,601)),'ordered full indexes')
  times=[int(x['elapsed_ns']) for x in rows];require(all(0<=x<r['capture_duration_s']*1e9 for x in times),'capture boundaries')
  require(all(a<b for a,b in zip(times,times[1:])),'strict sample order')
  require(all(x['cohort']=='cohort01' for x in rows),'one deidentified source cohort')
 require(all(x['status_bracket_eligible']=='1' for x in edges),'all descriptive status brackets')
 require(all(edges[0][k]=='' for k in ('interval_ns','sequence_delta','nominal_missing_before')),'exclude seed-to-first interval')
 for x in edges[1:]:
  require(int(x['sequence_delta'])==1 and int(x['nominal_missing_before'])==0,'no skipped sequence or nominal gap')
  require(500000000<int(x['interval_ns'])<1500000000,'consecutive one-second intervals')
 phase=[int(x['phase_ns']) for x in edges]
 require(all(-500000000<=v<500000000 for v in phase),'signed integer phase range')
 compare(stats(phase),r['all_new_edge_phase'],'all retained phase')
 compare(stats(phase),r['source_bracket_eligible_phase'],'descriptive bracket phase')
 compare(stats([int(x['interval_ns'])-1000000000 for x in edges[1:]]),r['consecutive_new_edge_period_error'],'599 new-to-new periods')
 flags=['eligible','complete','receiver_fresh','pps_fresh','clock_valid','transfer_fresh','transfer_running','server_fresh','server_ready','server_running']
 require(all(x[k]=='1' for x in status for k in flags),'600 current source and serving workload observations')
 require(all(x[k]=='0' for x in status for k in ('error','receiver_error','capture_error','uart_error','ethernet_link')),'zero errors and Ethernet absent')
 require(all(x['firmware']=='0.5.5' and x['transfer_tai_s']==x['server_tai_s']=='37' for x in status),'recorded image and operating TAI')
 for key in status[0]:
  if key not in ('cohort','firmware'):require(all(x[key]=='' or math.isfinite(float(x[key])) for x in status),'finite '+key)
 updates={}
 for x in status:updates.setdefault(int(x['chrony_update']),x)
 require(len(updates)==r['chrony_distinct_reference_updates']==597,'distinct chrony reference updates')
 compare(stats([int(x['chrony_last_offset_ns']) for x in updates.values()]),r['chrony_distinct_last_offset'],'distinct chrony last-offset population')
 for col,key in [('hat_fit_rms_ns','hat_fit_rms_snapshots'),('chrony_tracking_rms_ns','chrony_rms_estimator_snapshots'),('chrony_source_stddev_ns','chrony_source_residual_sd_snapshots')]:
  compare(describe([float(x[col]) for x in status if x[col]!='']),r[key],key)
 require(r['source_cohorts']==1 and r['source_window_availability_criteria_met'] and r['measured_window_availability_criteria_met'],'recorded availability criteria')
 require(r['observed_sequence_skips']==0 and not r['gaps'] and not r['workload_transitions_within_window'],'explicit zero gaps/transitions')
 require(not r['acquisition_errors'] and not r['export_errors'] and not r['log_export_errors'],'retained acquisition/export accounting')
 h=json.loads((BASE/'protocol-health.json').read_text());summary=h['summary']
 require(len(h['ntp'])==4 and all(x['valid_response'] and x['refid_ptob'] and x['stratum']==1 and x['leap']==0 for x in h['ntp']),'4 local NTP PTOB replies')
 require(len(packets)==summary['ptp_egress_packets']==204,'PTP egress denominator')
 require(all(x['egress']=='1' and x['domain']=='24' and x['source_alias']=='port01' for x in packets),'egress domain and deidentified port')
 sync={x['sequence_relative'] for x in packets if x['message_type']=='0'};follow={x['sequence_relative'] for x in packets if x['message_type']=='8'}
 require(len(sync&follow)==summary['ptp_egress_sync_follow_up_pairs']==96,'matched Sync/FollowUp pairs')
 require(sum(x['message_type']=='11' for x in packets)==summary['ptp_egress_announces']==12,'Announce denominator')
 require(not h['errors'] and not h['clock_settings_changed'],'bounded read-only protocol checks')
 require(not summary['remote_client_delivery_measured'] and not summary['independent_accuracy_measured'],'protocol measurement boundary')
 for row in h['tai_brackets']:
  require(row['tai_minus_realtime_lower_ns']<=37000000000<=row['tai_minus_realtime_upper_ns'],'recorded TAI read bracket')
 edition=json.loads((PAPER/'edition.json').read_text());require(edition['status']==m['status'] and 6<=edition['pages']<=8,'6–8 page author-review draft')
 publication_path=PAPER/'publication.json';publication=None;companions={}
 if publication_path.exists():
  publication=json.loads(publication_path.read_text())
  require(publication['status']=='APPROVED_FOR_PUBLIC_DRAFT_REVIEW' and not publication['draft_promoted_to_final'],'public draft approval')
  require(publication['prepared_edition_sha256']==digest(PAPER/'edition.json'),'unchanged prepared edition')
  companions={x['path']:x for x in publication['updated_companions']}
  allowed={'papers/pi5-mhat-gateway/README.md','evidence/pi5/2026-11-02/README.md','tools/verify_pi5_gateway.py'}
  require(set(companions)<=allowed,'bounded companion-only updates')
 for entry in edition['files']:
  p=(ROOT/entry['path']).resolve();expected=entry['sha256']
  if entry['path'] in companions:
   replacement=companions[entry['path']]
   require(replacement['prepared_sha256']==expected,'preserved companion checkpoint')
   expected=replacement['current_sha256']
  require(p.is_relative_to(ROOT) and digest(p)==expected,'edition identity '+entry['path'])
 if publication:
  for entry in publication['publication_files']:
   p=(ROOT/entry['path']).resolve()
   require(p.is_relative_to(ROOT) and digest(p)==entry['sha256'],'publication identity '+entry['path'])
  require(not publication['hardware_accuracy_qualified'],'accuracy remains unqualified')
  screenshots=json.loads((ROOT/'gateways/pi5/dashboard/images/manifest.json').read_text())
  require(not screenshots['lab_identifiers_included'] and not screenshots['tailnet_details_included'],'generic documentation screenshots')
  require(len(screenshots['images'])==2,'overview and chart screenshots')
  for entry in screenshots['images']+screenshots['frontend']:
   p=(ROOT/entry['path']).resolve()
   require(p.is_relative_to(ROOT) and digest(p)==entry['sha256'],'screenshot source identity '+entry['path'])
 source=(PAPER/'main.tex').read_text();require('capture pending' not in source and 'TODO' not in source,'completed draft text')
 cites={v.strip() for c in re.findall(r'\\cite\{([^}]+)\}',source) for v in c.split(',')};bibs=set(re.findall(r'\\bibitem\{([^}]+)\}',source));require(cites<=bibs,'resolved citations')
 print('PASS: final 600 pulses / 600 snapshots, all RMS/tails, 599 periods, 597 chrony updates, source/workload flags, protocol populations and draft identities.')
 print('Offline arithmetic and identity only; private raw extraction, UTC/electrical output and Ethernet endpoint accuracy are not reproduced or qualified.')
if __name__=='__main__':main()
