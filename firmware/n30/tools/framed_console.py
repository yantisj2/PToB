#!/usr/bin/env python3
"""N30 ten-row CRC console parser. Import opens no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import importlib.util
from pathlib import Path
p=Path(__file__).resolve().parents[2]/'w12/tools/framed_console.py'
spec=importlib.util.spec_from_file_location('n30_w12_console',p);base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
fields=base.fields;fresh_nonce=base.fresh_nonce;status_command=base.status_command
# Alternate frame carries logical receiver diagnostics under the same envelope.
LORA_NUMBERS={
 'CONTEXT':'source_epoch kind scale metadata_known',
 'ORIGINAL':'sequence corrected captured_tick16 capture_epoch event_local_us event_ns created_ns expires_ns map_generation map_age_us until_us age_ms assurance authority BLE_update',
 'QUALITY':'companion snapshot_age_ms received_us events time_valid known values role hop class accuracy parent_bound_ns correction_age_ms map_life_ms rate_ppb residual_ns losses recoveries link_samples link_rms_ns parent_quality_current',
 'COUNTERS':'accepted rejected replayed unmatched overflow metadata metadata_misses RXdone CRCbad malformed SPIok SPIerror invalid_capture capture_overflow map_accepted map_rejected',
 'HARDWARE':'requested active fault error stage opcode generation off_ack_generation off_ack SPI_retired capture_released reset_asserted capture_enclosure_ns work_max_us stack_unused heartbeat_ms RSSI_dbm SNR_quarter_db',
 'POLICY':'remaining_ms acquire_us recover_us state sampled_us UTC_us frequency_hz SF BW_hz CR preamble RX_ONLY'}
LORA_HEX='emitter emitter_boot stream root root_boot parent parent_boot'.split()
LORA_FIXED={'ORIGINAL':{'bound':'UNKNOWN','BLE_update':'0'},'COUNTERS':{'TX':'FORBIDDEN'},'HARDWARE':{'timestamp':'HARDWARE_DIO1_MAPPED','physical_bias':'UNKNOWN','durable_fault_journal':'UNAVAILABLE'},'POLICY':{'acquire_us':'180000000','recover_us':'180000000','frequency_hz':'915000000','SF':'10','BW_hz':'500000','CR':'6','preamble':'12','RX_ONLY':'1','GPS':'ABSENT','source_freshness':'UNKNOWN','uncertainty':'UNKNOWN'}}
def validate_lora(rows):
 for suffix,names in LORA_NUMBERS.items():
  row=rows['N30_LORA_'+suffix]
  for key in names.split():
   value=row.get(key,'');signed=key in ('rate_ppb','error','RSSI_dbm','SNR_quarter_db')
   base.require(bool(__import__('re').fullmatch(r'-?(?:0|[1-9][0-9]*)' if signed else r'(?:0|[1-9][0-9]*)',value)),'Invalid LoRa number: '+key)
   number=int(value);limit=2**64 if key in ('captured_tick16','capture_epoch','event_local_us','event_ns','created_ns','expires_ns','until_us','received_us','sampled_us','UTC_us') else 2**32
   base.require(-(2**31) <= number < 2**31 if signed else 0 <= number < limit,'LoRa number range: '+key)
  for key,value in LORA_FIXED.get(suffix,{}).items():base.require(row.get(key)==value,'LoRa contract mismatch: '+key)
 for key in LORA_HEX:base.require(bool(base.HEX16.fullmatch(rows['N30_LORA_CONTEXT'].get(key,''))),'LoRa identity syntax: '+key)
 base.require(bool(__import__('re').fullmatch('[0-9a-f]{32}',rows['N30_LORA_CONTEXT'].get('context',''))),'LoRa context syntax')
 base.require(rows['N30_LORA_POLICY'].get('mode') in ('AUTO','KEEP_ON','KEEP_OFF'),'LoRa override mode')
 for suffix,keys in [('CONTEXT','metadata_known'),('ORIGINAL','BLE_update'),('QUALITY','time_valid parent_quality_current'),('HARDWARE','requested active fault off_ack SPI_retired capture_released reset_asserted')]:
  for key in keys.split():base.require(rows['N30_LORA_'+suffix][key] in ('0','1'),'LoRa boolean: '+key)
 base.require(int(rows['N30_LORA_POLICY']['state'])<=3 and int(rows['N30_LORA_QUALITY']['link_samples'])<=16,'LoRa state/fit range')

class Parser(base.Parser):
 def __init__(self,nonce,manifest,session=None,capture_base=None):
  manifest=dict(manifest)
  manifest.setdefault('console_frame_bytes',8704)
  manifest.setdefault('fault_watchdog','SUSTAINED_FAULT_120S_V1')
  super().__init__(nonce,manifest,session,capture_base);base.require(manifest.get('r2_profile')=='r2-g1','N30 G1 only')
  self.expected_rows=(*self.expected_rows,'N30_UI')
  if manifest.get('n30_frame_kind')=='LORA':self.expected_rows=('W12_R2_STATUS','W12_V12_STATUS','N30_LORA_CONTEXT','N30_LORA_ORIGINAL','N30_LORA_QUALITY','N30_LORA_COUNTERS','N30_LORA_HARDWARE','N30_LORA_POLICY','W12_V12_COMMAND','N30_UI')
 def feed(self,data):
  frames=super().feed(data)
  for f in frames:
   if self.manifest.get('n30_frame_kind')=='LORA':validate_lora(f['rows'])
   mesh=f['rows']['W12_R2_STATUS'];ui=f['rows']['N30_UI'];status=f['rows']['W12_V12_STATUS'];m=self.manifest
   # The inherited whole-frame/nonce/CRC/boot/source/deadline checks still run.
   # Its historical nine-row G1 branch does not apply to this ten-row target;
   # validate the exact N30 G1 identity/capability separately here.
   for key,value in [('profile','r2-g1'),('enrollment_sha',m['r2_enrollment_sha256']),('self','3'),('client_capabilities','0'),('client_control','0'),('source','0'),('serving','0'),('group_id',m['r2_group_id']),('key_epoch',str(m['r2_key_epoch'])),('client_identity',m['r2_client_identity'])]:base.require(mesh.get(key)==value,'N30 G1 identity/capability mismatch: '+key)
   base.validate_provisional_profile(mesh,m)
   base.require(ui.get('profile')=='RC' and ui.get('GPS')=='ABSENT' and ui.get('display')=='64x128' and ui.get('page') in (('0','1','2') if m.get('n30_ui_revision',0)>=3 else ('0','1')),'N30 display/capability mismatch')
   base.require(status.get('GPS_inputs')=='0','N30 physical GNSS unexpectedly enabled')
   if m.get('n30_ui_revision',0)>=2 or 'path' in ui or 'upstream' in ui:
    base.require(ui.get('path') in (('BLE','RC1','RC2','LOR','HOLD','WAIT','--') if m.get('n30_ui_revision',0)>=3 else ('BLE','RC1','RC2','HOLD','WAIT','--')) and ui.get('upstream') in ('AUTO','RELAY','PENDING'),'Invalid N30 source/upstream mode')
  return frames

def mutation_command(frame,nonce,action):
 if action not in ('ui page 0','ui page 1','ui page 2','lora diag','lora auto','lora on','lora off'):return base.mutation_command(frame,nonce,action)
 base.require(base.HEX16.fullmatch(nonce) and int(nonce,16) and nonce!=frame['begin']['nonce'],'Fresh UI request nonce required')
 b=frame['begin'];return f"v12 do {b['ticket']} {nonce} {b['factory']} {b['session']} {action}\n".encode()
