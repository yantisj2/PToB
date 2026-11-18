#!/usr/bin/env python3
"""Selected operating-condition aggregates from private validated CRC frames.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
from collections import Counter
import hashlib,json
from pathlib import Path


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    frames=[json.loads(line) for line in a.input.read_text().splitlines()]
    keys=['cpu_mhz','cpu_ble','cpu_panel','cpu_rc','asleep','face','usb','ble_power_requested','ble_controller','ble_host_running','shadow_powered','shadow_owner','shadow_capture','shadow_requested','errors','ble_operating_fault','ble_cleanup_rc','fine_cleanup_fault','shadow_cleanup_fault']
    populations={key:Counter() for key in keys};minimum={};maximum={};latencies=[]
    shadow=Counter();radio=Counter();r2_fault=Counter();journal=Counter()
    for frame in frames:
        rows=frame['rows'];ui=rows.get('TWATCH_UI',{})
        for key in keys:
            if key in ui:populations[key][ui[key]]+=1
        for key in ['ui_stack_min_bytes','heap_internal_min','heap_internal_largest','touch_errors','pmu_key_errors','imu_errors']:
            if key in ui:
                value=int(ui[key]);minimum[key]=min(minimum.get(key,value),value);maximum[key]=max(maximum.get(key,value),value)
        if 'main_stack_free_words' in rows.get('W12_V12_STATUS',{}):
            key='main_stack_free_bytes';value=int(rows['W12_V12_STATUS']['main_stack_free_words'])
            minimum[key]=min(minimum.get(key,value),value);maximum[key]=max(maximum.get(key,value),value)
        if 'TWATCH_SHADOW' in rows:
            for key in ['shadow_uart_bytes','shadow_pps','shadow_requested','shadow_powered','shadow_owner']:
                shadow[key+'='+rows['TWATCH_SHADOW'][key]]+=1
        if 'W12_V12_LORA_STATUS' in rows:
            q=rows['W12_V12_LORA_STATUS'];radio['requested='+q['requested']+',enabled='+q['enabled']+',cleanup_fault='+q['cleanup_fault']]+=1
        if 'W12_R2_STATUS' in rows:r2_fault[rows['W12_R2_STATUS'].get('fault','missing')]+=1
        if 'TWATCH_SOURCE' in rows:
            q=rows['TWATCH_SOURCE'];journal['valid='+q.get('rf_slot_valid','missing')]+=1
        if 'host_request_seconds' in frame:latencies.append(frame['host_request_seconds'])
    result=dict(schema=1,author='Jonathan Yantis',license='CC-BY-4.0',frame_count=len(frames),
                conditions={key:dict(value) for key,value in populations.items()},minimum=minimum,maximum=maximum,
                shadow_checks=dict(shadow),lora_owner_samples=dict(radio),ble_fault_samples=dict(r2_fault),
                journal_read_samples=dict(journal),temperature='No independently calibrated ambient/crystal temperature measurement',
                load='Native USB logging and current display/radio CPU locks; snapshots add main-task work',
                policy='CPU_DFS80_240_USB_DORMANT_PANEL_SLEEP_V1 unchanged',
                latency_seconds=dict(n=len(latencies),maximum=max(latencies,default=None)),
                provenance=dict(private_frames_sha256=hashlib.sha256(a.input.read_bytes()).hexdigest(),source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()))
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print('Operating-condition aggregates:',len(frames),'validated frames')


if __name__=='__main__':main()
