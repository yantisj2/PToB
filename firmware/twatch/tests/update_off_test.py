#!/usr/bin/env python3
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""No hardware: reject incomplete, stale-generation and late OFF receipts."""
import copy,importlib.util
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'tools/framed_console.py'
s=importlib.util.spec_from_file_location('watch_console',p);c=importlib.util.module_from_spec(s);s.loader.exec_module(c)
manifest={'ble_power_lifecycle':'FAULT_CLEANUP_OFF_V2'}
f={'request_delta':{'lost':0},'rows':{
'TWATCH_UI':dict(ble_power_stage='6',ble_power_requested='0',ble_controller='0',ble_host_running='0',ble_power_worker='0',ble_power_off_ack='1',ble_off_proof='1',ble_cleanup_rc='0',ble_power_generation='4',ble_off_gen='4',ble_off_start_us='100',ble_off_deadline_us='2000100',ble_off_at_us='1000',ble_power_rc='-12',ble_operating_fault='1'),
'W12_V12_LORA_STATUS':dict(enabled='0',requested='0',worker_busy='0',off_ack='1',off_proof='15',cleanup_fault='0',TX='OFF',generation='2',acknowledged_generation='2',failed='1',errors='1'),
'W12_V12_COMMAND':dict(update_stage='2',application_drained='1',update_deadline_us='2000100',update_completed_us='1500')}}
c.require_update_off(f,manifest)
def reject(frame,contract=manifest):
 try:c.require_update_off(frame,contract)
 except (AssertionError,ValueError,RuntimeError,KeyError):return
 raise AssertionError('Unsafe proof accepted')
for row,fields in f['rows'].items():
 for key in fields:
  if key in ('ble_power_rc','ble_operating_fault','failed','errors'):continue
  bad=copy.deepcopy(f);del bad['rows'][row][key];reject(bad)
for row,key,value in [('TWATCH_UI','ble_controller','1'),('TWATCH_UI','ble_host_running','1'),('TWATCH_UI','ble_power_worker','1'),('TWATCH_UI','ble_off_proof','0'),('TWATCH_UI','ble_cleanup_rc','-110'),('TWATCH_UI','ble_off_gen','3'),('TWATCH_UI','ble_off_at_us','2000100'),('TWATCH_UI','ble_off_deadline_us','2000200'),('W12_V12_LORA_STATUS','acknowledged_generation','1'),('W12_V12_LORA_STATUS','cleanup_fault','1'),('W12_V12_COMMAND','update_completed_us','2000100')]:
 bad=copy.deepcopy(f);bad['rows'][row][key]=value;reject(bad)
for proof in range(15):
 bad=copy.deepcopy(f);bad['rows']['W12_V12_LORA_STATUS']['off_proof']=str(proof);reject(bad)
bad=copy.deepcopy(f);bad['request_delta']['lost']=1;reject(bad)
reject(f,{'ble_power_lifecycle':'CONTROLLER_OFF_V1'})
reject(f,{})
print('PASS: physical OFF proof, retained history, missing fields, generation, cleanup, original deadlines and old-image guards')

new=copy.deepcopy(f);new_manifest=dict(manifest,control_contract='BOUNDED_CONTROL_V1')
proof=dict(control_version='1',display_off_proof='1',fine_off_proof='1',fine_cleanup_fault='0',shadow_requested='0',shadow_powered='0',shadow_transition='0',shadow_owner='0',shadow_uart='0',shadow_capture='0',shadow_off_proof='1',shadow_cleanup_fault='0')
new['rows']['TWATCH_UI'].update(proof);c.require_update_off(new,new_manifest)
for key,value in proof.items():
 bad=copy.deepcopy(new);del bad['rows']['TWATCH_UI'][key];reject(bad,new_manifest)
 bad=copy.deepcopy(new);bad['rows']['TWATCH_UI'][key]='0' if value=='1' else '1';reject(bad,new_manifest)
print('PASS compact maintenance: every physical display/fine/GNSS proof and missing/false/transition guard')
