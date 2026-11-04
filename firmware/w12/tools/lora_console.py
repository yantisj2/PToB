#!/usr/bin/env python3
"""Passive LR2021 diagnostics under original nonce/CRC/boot/deadline guards."""
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import re
import framed_console as base
class Parser(base.Parser):
 def __init__(self,nonce,manifest,session=None,capture_base=None):
  super().__init__(nonce,manifest,session,capture_base)
  self.expected_rows=('W12_R2_STATUS','W12_V12_STATUS','W12_LORA_CONTEXT','W12_LORA_ORIGINAL','W12_LORA_QUALITY','W12_LORA_COUNTERS','W12_LORA_HARDWARE','W12_LORA_POLICY','W12_UI_CLOCK','W12_V12_COMMAND')
 def feed(self,data):
  frames=super().feed(data)
  for f in frames:
   rows=f['rows'];m=self.manifest;mesh=rows['W12_R2_STATUS']
   for key,value in [('profile','r2-g1'),('enrollment_sha',m['r2_enrollment_sha256']),('group_id',m['r2_group_id']),('key_epoch',str(m['r2_key_epoch'])),('client_identity',m['r2_client_identity'])]:base.require(mesh.get(key)==value,'LoRa diagnostic identity: '+key)
   ctx=rows['W12_LORA_CONTEXT'];p=rows['W12_LORA_POLICY'];q=rows['W12_LORA_QUALITY'];h=rows['W12_LORA_HARDWARE']
   for key in ('emitter','emitter_boot','root','root_boot','parent','parent_boot','stream'):base.require(base.HEX16.fullmatch(ctx.get(key,'')),'LoRa scope '+key)
   base.require(re.fullmatch('[0-9a-f]{32}',ctx.get('context','')),'LoRa context')
   for key in ('source_epoch','seed','meta_cursor','meta_utc_ns'):base.require(ctx.get(key,'').isdigit() and int(ctx[key])<2**64,'LoRa META '+key)
   for key in ('trained','model_valid','slow_fit','time_valid'):base.require(q.get(key) in ('0','1'),'LoRa boolean '+key)
   for key in ('rate_ppb','phase_ns'):base.require(re.fullmatch(r'-?(?:0|[1-9][0-9]*)',q.get(key,'')) and -2**31<=int(q[key])<2**31,'LoRa signed '+key)
   for key in ('link_rms_ns','link_samples','reference_age_ms','snapshot_age_ms'):base.require(q.get(key,'').isdigit() and int(q[key])<2**32,'LoRa model '+key)
   base.require(int(q['link_samples'])<=16 and (q['trained']=='1' or q['link_rms_ns']==str(2**32-1)),'Untrained LoRa fit quality')
   for key,value in {'wire':'3','schedule':'8','frequency_hz':'922400000','SF':'11','BW_hz':'500000','CR':'8','RX_ONLY':'1','model_delay_ns':'99328000','calibration':'LR2021_ENGINEERING_UNVERIFIED'}.items():base.require(p.get(key)==value,'LoRa profile '+key)
   base.require(p.get('mode') in ('AUTO','KEEP_ON','KEEP_OFF') and p.get('state') in ('0','1','2','3'),'LoRa state/mode')
   base.require(rows['W12_LORA_COUNTERS'].get('TX')=='FORBIDDEN' and rows['W12_LORA_ORIGINAL'].get('bound')=='UNKNOWN','LoRa authority/role')
   base.require(h.get('timestamp')=='HARDWARE_HEADER_VALID_FINE20' and h.get('physical_bias')=='UNKNOWN','LoRa physical endpoint')
  return frames
