#!/usr/bin/env python3
"""Audit pinned public S3 timestamp/capture leads without operating hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Upstream SDK sources/binaries retain their original terms.
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def audit(sdk,objdump):
 lock=json.loads((APP/'dependencies.lock.json').read_text())['esp_idf']
 archive=sdk/'components/bt/controller/lib_esp32c3_family/esp32s3/libbtdm_app.a'
 if sha(archive)!=lock['controller_sha256']:raise ValueError('Pinned controller archive mismatch')
 dis=subprocess.check_output([str(objdump),'-d',str(archive)],text=True,timeout=30,stderr=subprocess.DEVNULL)
 names=('r_rwip_time_get','r_lld_con_rx_sync_time_update','r_lld_sync_process_pkt_rx_aux_sync_ind')
 functions={}
 for name in names:
  marker='Disassembly of section .text.'+name+':'
  if marker not in dis:raise ValueError('Missing pinned implementation: '+name)
  body=dis.split(marker,1)[1].split('Disassembly of section ',1)[0]
  functions[name]={'implementation':'PINNED_ARCHIVE_NOT_A_LIVE_ROM_READ','disassembly_sha256':hashlib.sha256(body.encode()).hexdigest(),'fine_10bit_extract':bool(re.search(r'extui\s+a\d+,\s*a\d+,\s*0,\s*10\b',body)),'constant624':bool(re.search(r'\b0x270\b',body)),'constant625':bool(re.search(r'\b0x271\b',body))}
 if not all(functions[n]['fine_10bit_extract'] and functions[n]['constant624'] for n in names[1:]):raise ValueError('Receive fine-field implementation changed')
 sig=sdk/'components/soc/esp32s3/include/soc/gpio_sig_map.h'
 signals={name:int(index) for name,index in re.findall(r'#define\s+((?:BB_DIAG\d+|GPIO_LC_DIAG\d+|BLE_AUDIO_SYNC\d+_P)_IDX)\s+(\d+)',sig.read_text())}
 cap=sdk/'components/esp_driver_mcpwm/src/mcpwm_cap.c'
 text=cap.read_text();apb='cap_timer->resolution_hz = esp_clk_apb_freq();' in text
 if not apb or 'cap_timer resolution is not adjustable in current target' not in text:raise ValueError('Pinned S3 APB capture path changed')
 return {'schema':1,'scope':'OFFLINE_PUBLIC_SDK_IMPLEMENTATION_AUDIT','hardware_operations':False,'controller_sha256':sha(archive),'sdk_commit':lock['commit'],'functions':functions,'gpio_signal_names':signals,'mcpwm':{'source_sha256':sha(cap),'APB_clock_resolution':apb,'requested_resolution_adjustable_on_this_path':False,'actual_resolution_must_be_queried':True,'interrupt_latency_does_not_set_hardware_capture_tick':True},'conclusions':[
  'Current receive synchronization implementations consume10 low fine bits and624/625 half-microsecond arithmetic. No higher-resolution RX timestamp is established by these implementations.',
  'Diagnostic GPIO signal names are routing leads only. No BLE ADDRESS selector, edge timing, ownership or delay calibration has been verified. Audio-sync names do not establish periodic-advertisement packet events.',
  'MCPWM can latch a verified edge using its APB capture counter; the selected driver path ignores requested resolution. A faster counter alone does not add phase to a500ns descriptor.',
  'The existing N30 LoRa DIO1 capture is a different modem endpoint. It cannot establish a finer BLE receive timestamp.',
  'Do not write undocumented baseband registers or route an unverified marker on an enrolled field image. A validated fine latch/marker is still required before replacing descriptor timing.',
  'Next hardware experiment: establish an Espressif-supported RX/TX marker selector and semantics, bind a verified free route/capture resource, then compare paired hardware captures against TC-04 shadow PPS and an RF/output reference when available.',
 ],'sources':{'SDK':'https://github.com/espressif/esp-idf/tree/'+lock['commit'],'MCPWM':'https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/peripherals/mcpwm.html'},'unknowns':['Live ROM/dispatch implementation identity beyond existing firmware ABI guards','Fine-field high-bit meaning','BLE diagnostic marker selector/semantics','Marker-to-RF delay and variation','Cross-counter mapping error','Absolute UTC/output error']}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--idf',type=Path,required=True);p.add_argument('--objdump',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 a.out.mkdir(parents=True,exist_ok=False);result=audit(a.idf.resolve(),a.objdump.resolve());(a.out/'audit.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS pinned S3 implementation audit; hardware marker remains unverified')
if __name__=='__main__':main()
