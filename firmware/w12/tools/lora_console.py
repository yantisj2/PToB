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
   if 'dual_schema' in mesh:
    base.require(mesh['dual_schema'] in ('1','2') and mesh.get('authority')=='UNCHANGED' and mesh.get('parent_ring')=='8' and mesh.get('parent_view')==('RING8' if mesh['dual_schema']=='2' else 'LATEST_ONLY'),'Dual source/parent diagnostic contract')
    def num(key,bits=64):
     value=mesh.get(key,'');base.require(re.fullmatch(r'(?:0|[1-9][0-9]*)',value) and int(value)<2**bits,'Dual diagnostic integer '+key);return int(value)
    refusal=num('ble_refusal',32);base.require(refusal<4096,'BLE refusal range')
    names=((1,'DISABLED'),(2,'FAULT'),(4,'MODE_DRAIN'),(32,'STALE_SNAPSHOT'),(2048,'RETAINED_HOLD'),(64,'COARSE_PERMISSION'),(128,'CORRECTED_PERMISSION'),(256,'REPLAY_CONTEXT'),(16,'NO_CONTEXT'),(8,'NO_CURRENT_FOLLOW'),(512,'RAW_QUALITY'),(1024,'QUALITY_FORECAST'))
    base.require(mesh.get('ble_why')==next((name for bit,name in names if refusal&bit),'READY') and mesh.get('ble_lock')==str(int(refusal==0)),'Current BLE refusal/lock agreement')
    for key in ('observed_us','qualify_since_us','permission_pause_us','max_observer_gap_us','first_loss_us','parent_at_us'):num(key)
    for key in ('qualifying','lora_selected','display_lora'):base.require(mesh.get(key) in ('0','1'),'Selection boolean '+key)
    for key in ('losses','resets','first_refusal','first_epoch','parent_seq','parent_epoch','parent_train'):num(key,32)
    base.require(num('remaining_us',32)<=30000000 and num('first_refusal',32)<4096 and num('first_reason',8)<=3,'Bounded return qualifier')
    base.require(mesh.get('permission_gap_limit_us')=='1500000' and mesh.get('observer_limit_us')=='250000','Separate observation/permission limits')
    for key in ('first_root','first_boot','parent_peer','parent_root','parent_boot'):base.require(base.HEX16.fullmatch(mesh.get(key,'')),'Dual source identity '+key)
    reasons=('SCOPE_CHANGE','STARTUP','HOLD_GM','RECOVER_GM','RETURN_GM','LINK_POOR','CLOCK_UNAVAILABLE','CLOCK_STEP','SOURCE_CHANGE','REPLAY_EXPIRED','ROOT_HINT','MANUAL_PARENT','RELAY_ONLY','STOP','BEST_CANDIDATE','LOCK_TRIAL_EXPIRED','PERMISSION_GAP','REPLAY_CONTEXT','SUCCESSOR','PACKET_SCOPE')
    parent=num('parent_reason',8);base.require(parent<len(reasons) and num('parent_depth',8)<=2 and mesh.get('parent_why')==(reasons[parent] if int(mesh['parent_seq']) else 'NONE'),'Parent withdrawal reason')
    if mesh['dual_schema']=='2':
     for key in ('wd_sampled_us','wd_main_at_us','wd_host_at_us','wd_sync_until_us','wd_last_sync_us','wd_fault_since_us','wd_fault_elapsed_us','wd_no_time_since_us','wd_no_time_elapsed_us','wd_previous_elapsed_us'):num(key)
     for key in ('wd_mask','wd_first_mask','wd_previous_mask','wd_restarts','ble_recoveries'):num(key,32)
     for key in ('wd_host_expected','wd_sync_expected','absence_learned','absence_rx_healthy','absence_expected'):base.require(mesh.get(key) in ('0','1'),'Watchdog boolean '+key)
     base.require(num('wd_previous_api',8)<256 and base.HEX16.fullmatch(mesh.get('wd_previous_boot','')),'Watchdog RTC identity/API')
     rc=mesh.get('wd_previous_rc','');base.require(re.fullmatch(r'-?(?:0|[1-9][0-9]*)',rc) and -2**31<=int(rc)<2**31,'Watchdog RTC result')
     for key,value in dict(wd_no_time_limit_us='720000000',wd_minimum_us='5000000',wd_stall_us='5000000').items():base.require(mesh.get(key)==value,'Watchdog timer contract '+key)
     for key in ('ble_root','ble_boot'):base.require(base.HEX16.fullmatch(mesh.get(key,'')),'Current BLE scope '+key)
     for key in ('ble_epoch','ble_publication','ble_quality_scope'):num(key,32)
     num('ble_origin')
     base.require(mesh.get('parent_encoding')=='SEQ_AT_REASON_FROM6_TO6_V1','Parent ring encoding')
     count=num('parent_returned',32);base.require(count<=8 and num('parent_overwritten',32)==max(0,num('parent_seq',32)-8),'Parent bounded ring accounting')
     num('parent_read_lost',32);events=[];previous=0
     keys=('sequence','at_us','reason','from_peer','from_root','from_boot','from_epoch','from_train','from_depth','peer','root','boot','epoch','train','depth')
     for index in range(8):
      raw=mesh.get('parent'+str(index),'').split('/');base.require(len(raw)==15,'Parent vector width')
      event=dict(zip(keys,raw))
      for key in ('from_peer','from_root','from_boot','peer','root','boot'):base.require(base.HEX16.fullmatch(event[key]),'Parent vector identity')
      for key in ('sequence','at_us','reason','from_epoch','from_train','from_depth','epoch','train','depth'):
       value=event[key];base.require(re.fullmatch(r'(?:0|[1-9][0-9]*)',value) and int(value)<2**(64 if key=='at_us' else 32),'Parent vector integer')
      base.require(int(event['reason'])<20 and int(event['depth'])<=2 and int(event['from_depth'])<=2,'Parent vector reason/depth')
      seq=int(event['sequence'])
      if index<count:
       base.require(previous<seq<=int(mesh['parent_seq']),'Parent vector ordering');previous=seq;events.append(event)
      else:base.require(all(int(value,16 if key in ('from_peer','from_root','from_boot','peer','root','boot')else 10)==0 for key,value in event.items()),'Nonempty unused parent slot')
     mesh['parent_events']=events
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
