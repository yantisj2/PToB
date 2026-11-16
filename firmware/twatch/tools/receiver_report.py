#!/usr/bin/env python3
"""Offline receiver transition summary of captured, verified Console V3 frames.
Snapshot transitions are not an event trace or an RF/accuracy qualification.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json
from pathlib import Path

STATE=('requested','client_running','client_stopping','client_generation','fault','synced','sync_pending','following','joined_peer','joined_boot','source_epoch','rx_epoch','rx_armed','rx_fault')
COUNTERS=('join_attempts','join_accepts','join_rejected','peer_losses','pa_reports','GR_authenticated','GR_refused','rx_calls','rx_candidates','rx_matched','rx_missed','rx_ambiguous','rx_overflow','api_errors','deadline_exceeded','window_misses','quiet_faults')

def summarize(frames):
 full=[f for f in frames if 'client_running' in f.get('rows',{}).get('W12_R2_STATUS',{})]
 sessions={f.get('begin',{}).get('session') for f in frames if f.get('begin',{}).get('session')}
 if len(sessions)>1:raise ValueError('Mixed boot sessions: split captures before comparing counters')
 timeline=[];previous=None
 for f in full:
  r=f['rows']['W12_R2_STATUS'];state={k:r.get(k) for k in STATE}
  if state!=previous:
   timeline.append({'elapsed_s':f.get('capture_elapsed_s'),'state':state,
                    'disconnect_reason':r.get('g1_last_disconnect_reason'),
                    'disconnect_at_us':r.get('g1_last_disconnect_at_us')})
   previous=state
 deltas={}
 if full:
  for k in COUNTERS:
   values=[int(f['rows']['W12_R2_STATUS'][k]) for f in full if k in f['rows']['W12_R2_STATUS']]
   if values:
    reset=any(b<a for a,b in zip(values,values[1:]))
    deltas[k]={'first':values[0],'last':values[-1],'delta':None if reset else values[-1]-values[0],'reset_observed':reset}
 selection=[];previous=None;holdover=[];hold_previous=None;last_hold={}
 faults=[];fault_previous=None;last_fault={}
 for f in frames:
  r=f.get('rows',{}).get('TWATCH_SOURCE')
  if r is None:continue
  last_fault={k:v for k,v in r.items() if k.startswith(('rf_','ble_fault_','lor_fault_')) or k=='ble_recovery'}
  if last_fault and last_fault!=fault_previous:
   faults.append({'elapsed_s':f.get('capture_elapsed_s'),'capture_session':f.get('begin',{}).get('session'),
                  'diagnostics':last_fault,'note':'Persisted record boot may predate this capture; queued/restarted is not timing admission.'})
   fault_previous=last_fault
  last_hold={k:v for k,v in r.items() if k.startswith('p99_')}
  h={k:r.get(k) for k in ('p99_ready','p99_reason','p99_resets','p99_dropped')}
  if last_hold and h!=hold_previous:holdover.append({'elapsed_s':f.get('capture_elapsed_s'),**h});hold_previous=h
  state={k:r.get(k) for k in ('source','refusal','switches','gaps')}
  if state!=previous:selection.append({'elapsed_s':f.get('capture_elapsed_s'),**state});previous=state
 return {'scope':'SNAPSHOT_TRANSITIONS_NOT_RF_OR_PRECISION_QUALIFICATION','full_ble_snapshots':len(full),
         'ble_timeline':timeline,'counter_changes':deltas,'selection_timeline':selection,
         'holdover_timeline':holdover,'last_holdover':last_hold,
         'radio_fault_timeline':faults,'last_radio_faults':last_fault,
         'limitations':'Intermediate transitions may be missed. Strict discipline refusal is distinct from corrected G1-model acceptance. State reasons and RMS alone do not establish RF cause or output accuracy.'}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--frames',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 result=summarize([json.loads(l) for l in a.frames.read_text().splitlines() if l.strip()])
 with a.out.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
if __name__=='__main__':main()
