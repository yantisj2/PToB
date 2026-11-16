# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from receiver_report import summarize

def frame(at,**kw):return {'begin':{'session':'one'},'capture_elapsed_s':at,'rows':{'W12_R2_STATUS':{'client_running':'1','requested':'1',**kw}}}
a=frame(0,rx_epoch='1',joined_peer='a',rx_matched='10')
compact={'begin':{'session':'one'},'rows':{'W12_R2_STATUS':{'following':'1'}}}
b=frame(1,rx_epoch='2',joined_peer='b',rx_matched='20')
r=summarize([a,compact,b]);assert r['full_ble_snapshots']==2 and len(r['ble_timeline'])==2 and r['counter_changes']['rx_matched']['delta']==10
c=frame(2,rx_epoch='3',rx_matched='0');r=summarize([a,b,c]);assert r['counter_changes']['rx_matched']['delta'] is None and r['counter_changes']['rx_matched']['reset_observed']
c['begin']['session']='two'
try:summarize([a,c]);raise AssertionError('accepted mixed boot')
except ValueError:pass
assert summarize([])['full_ble_snapshots']==0
a['rows']['TWATCH_SOURCE']={'p99_ready':'0','p99_reason':'WAIT_SAMPLES','p99_trials':'5'}
b['rows']['TWATCH_SOURCE']={'p99_ready':'1','p99_reason':'READY','p99_trials':'100'}
r=summarize([a,b]);assert len(r['holdover_timeline'])==2 and r['last_holdover']['p99_trials']=='100'
print('PASS receiver report: compact-row exclusion, churn timeline, counter reset and mixed-boot protection')

a['rows']['TWATCH_SOURCE'].update(rf_version='1',ble_fault_boot='old',ble_fault_rc='-42',ble_recovery='1/0/1/0/3')
r=summarize([a,a]);assert len(r['radio_fault_timeline'])==1 and r['last_radio_faults']['ble_fault_boot']=='old'
assert r['radio_fault_timeline'][0]['capture_session']=='one'
try:summarize([compact,{'begin':{'session':'other'},'rows':{'TWATCH_SOURCE':{}}}]);raise AssertionError('compact mixed boot accepted')
except ValueError:pass
