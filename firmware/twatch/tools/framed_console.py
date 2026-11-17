#!/usr/bin/env python3
"""Watch Console V3 envelope; importing this module never accesses hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import importlib.util,re
from pathlib import Path
p=Path(__file__).resolve().parents[2]/'w12/tools/framed_console.py'
spec=importlib.util.spec_from_file_location('watch_console_base',p)
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
np=Path(__file__).resolve().parents[2]/'n30/tools/framed_console.py'
ns=importlib.util.spec_from_file_location('watch_timing_validation',np);timing=importlib.util.module_from_spec(ns);ns.loader.exec_module(timing)
fresh_nonce=base.fresh_nonce
# A reconnect may inherit a partial command or the parser's binary/overflow
# discard state. NUL invalidates any pending line before LF clears that state;
# LF alone could execute a complete mutation whose delimiter was never sent.
INPUT_ABORT=b'\x00\n'
def status_command(nonce):
 return INPUT_ABORT+base.status_command(nonce)
def control_status_command(nonce):
 base.require(bool(base.HEX16.fullmatch(nonce)) and int(nonce,16)!=0,'Fresh nonzero nonce required')
 return INPUT_ABORT+f"v12 control {nonce}\n".encode()
class Parser(base.Parser):
 def __init__(self,nonce,manifest,session=None):
  super().__init__(nonce,manifest,session)
  base.require(manifest.get('r2_profile')=='r2-g1','Watch G1 contract required')
  self.expected_rows=(*self.expected_rows,'TWATCH_UI')
  self.fine_profile=manifest.get('fine_capture_profile')
  if manifest.get('watch_frame')=='LORA':self.expected_rows=('W12_R2_STATUS','W12_V12_STATUS','N30_LORA_CONTEXT','N30_LORA_ORIGINAL','N30_LORA_QUALITY','N30_LORA_COUNTERS','N30_LORA_HARDWARE','N30_LORA_POLICY','W12_V12_COMMAND','TWATCH_UI')
  if manifest.get('watch_frame')=='LORA' and manifest.get('lora_shadow_schedule')=='SHADOW_ONLY_V1':self.expected_rows=(*self.expected_rows[:-2],'TWATCH_LORA_SHADOW',*self.expected_rows[-2:])
  if manifest.get('watch_frame')=='LORA' and manifest.get('lora_shadow_schedule') in ('GUARDED_SLEEP_MISS_RELEARN_V2','EARLY_SLEEP_COMPLETE_DATA_V3'):self.expected_rows=(*self.expected_rows[:-2],'TWATCH_LORA_SHADOW','TWATCH_LORA_SLEEP',*self.expected_rows[-2:])
  if manifest.get('watch_frame','').startswith('WINDOW_'):self.expected_rows=(*getattr(timing,manifest['watch_frame']+'_ROWS')[:-1],'TWATCH_UI')
  if manifest.get('watch_frame')=='FINE':self.expected_rows=(*timing.FINE_ROWS[:-1],'TWATCH_UI')
  if manifest.get('watch_frame')=='TIMING':self.expected_rows=(*timing.TIMING_ROWS[:-1],'TWATCH_UI')
  if manifest.get('watch_frame')=='IMU':self.expected_rows=('W12_R2_STATUS','W12_V12_STATUS','TWATCH_IMU','W12_V12_COMMAND','TWATCH_UI')
  if manifest.get('watch_frame')=='SHADOW':self.expected_rows=('W12_R2_STATUS','W12_V12_STATUS','TWATCH_SHADOW','W12_V12_COMMAND','TWATCH_UI')
  if manifest.get('watch_frame')=='DUAL':self.expected_rows=('W12_R2_STATUS','W12_V12_STATUS','TWATCH_SOURCE','W12_V12_COMMAND','TWATCH_UI')
 def feed(self,data):
  frames=super().feed(data)
  for frame in frames:
   if 'N30_WINDOW_STATUS' in frame['rows']:validate_window(frame['rows'])
   if 'N30_FINE_STATUS' in frame['rows']:validate_fine(frame['rows'],self.fine_profile)
   if 'N30_TIMING_STATUS' in frame['rows']:timing.validate_timing(frame['rows'])
   if 'TWATCH_LORA_SLEEP' in frame['rows']:validate_lora_sleep(frame['rows'],self.manifest.get('lora_shadow_schedule'))
   if 'TWATCH_IMU' in frame['rows']:
    imu=frame['rows']['TWATCH_IMU'];base.require(imu.get('version')=='1' and imu.get('diagnostic_only')=='1','Unexpected IMU diagnostic contract')
   ui=frame['rows']['TWATCH_UI']
   for key,value in [('version','1'),('width','410'),('height','502'),('GNSS','FACE_AUTO' if self.manifest.get('version') in ('0.1.7','0.1.8','0.1.9','0.1.10') or self.manifest.get('GNSS')=='SHADOW_FACE_AUTO_RX_ONLY_V1' or self.manifest.get('control_contract')=='BOUNDED_CONTROL_V1' else 'OFF'),('precision','UNQUALIFIED')]:
    base.require(ui.get(key)==value,'Unexpected watch UI contract: '+key)
   if ui.get('GNSS')=='FACE_AUTO':base.require(ui.get('shadow_version')=='1','Missing shadow diagnostic contract')
   base.require(ui.get('ready') in ('0','1'),'Missing display readiness')
   if ui.get('control_version')=='1':
    mesh=frame['rows']['W12_R2_STATUS']
    for key,value in [('profile','r2-g1'),('self','3'),('control','1'),('enrollment_sha',self.manifest['r2_enrollment_sha256'])]:base.require(mesh.get(key)==value,'Compact control binding/'+key)
    for key in ('display_off_proof','fine_off_proof','fine_cleanup_fault','shadow_requested','shadow_powered','shadow_transition','shadow_owner','shadow_uart','shadow_capture','shadow_off_proof','shadow_cleanup_fault'):base.require(ui.get(key) in ('0','1'),'Missing physical control proof/'+key)

  return frames

def validate_window(rows):
 k=rows['N30_WINDOW_CONTRACT'];s=rows['N30_WINDOW_STATUS']
 for key,value in dict(marker='RW238_RX17_ONLY',tx='NONE_CONSTANT0',production='GPTIMER20',offset='LEGACY_ONLY').items():
  base.require(k.get(key)==value,'Watch window contract '+key)
 for key,mask in (('pending',1),('IRQ',1),('channels',5)):
  base.require(timing.timing_number(s,key)&~mask==0,'Watch window RX-only '+key)
 for i in range(4):
  e=rows.get(f'N30_WINDOW_EDGE{i}',{})
  if e.get('present')=='1':base.require(e.get('kind')=='0','Watch window TX edge')
 copy={name:dict(row) for name,row in rows.items()};copy['N30_WINDOW_CONTRACT']['marker']='RW238_237'
 timing.validate_window(copy,edges='N30_WINDOW_EDGE0' in rows,late='N30_WINDOW_LATE0' in rows)

def window_action(action):
 if action in ('window packet','window stop'):return True
 m=re.fullmatch(r'window (edges|ack|late) (0|[1-9][0-9]*)',action)
 if m:return 1<=int(m[2])<=128 if m[1]=='ack' else int(m[2])<=(128 if m[1]=='late' else 0xffffffff)
 m=re.fullmatch(r'window start ([1-9][0-9]*) ([1-9][0-9]*)',action)
 return bool(m and 1000<=int(m[1])<=600000 and 1<=int(m[2])<=128)

def validate_fine(rows,profile=None):
 # Check watch ownership first, then share numeric/cursor validation on a copy.
 s=rows['N30_FINE_STATUS'];k=rows['N30_FINE_CONTRACT']
 mode=s.get('mode');allowed=('TWATCH_NATIVE_RW_RX17_SHADOW','TWATCH_NATIVE_RW_RX17_G1','CURRENT_ONLY')
 base.require(mode in allowed and s.get('start_mode') in allowed,'Watch fine mode')
 production=mode=='TWATCH_NATIVE_RW_RX17_G1'
 if production:
  base.require(isinstance(profile,dict) and profile.get('mode')=='TWATCH_ULTRA_RX17_RELATIVE_FINE','Watch fine production profile required')
  base.require(profile.get('endpoint_class')==dict(phy=0,pdu_length=191,ext_header=0,extra_us=4),'Watch fine endpoint class')
  phase=profile.get('phase80')
  base.require(type(phase)==int and 0<phase<2**32 and phase%4==0,'Watch fine phase convention')
  evidence=profile.get('phase_evidence_sha256','')
  base.require(bool(re.fullmatch('[0-9a-f]{64}',evidence)) and int(evidence,16)!=0,'Watch fine phase evidence')
 else:phase=0
 base.require(s.get('pins')==('0' if mode=='CURRENT_ONLY' else '1'),'Watch fine pin profile')
 base.require(s.get('tx_out')=='0','Watch must not expose a TX pad')
 for key in ('latch_max_us','start_latch_max_us'):
  if key in s:
   value=timing.timing_number(s,key,64)
   if s.get('active')=='1' and s.get('error')=='0':base.require(value<=100,'Watch latch exceeded wall deadline')
 base.require(k.get('pps_aux','0')=='0','Leave Shadow face before finite fine diagnostics')
 if mode!='CURRENT_ONLY' and int(s.get('incarnation','0')):
  base.require(s.get('tx_in')=='188','Watch TX capture input must be constant zero')
  if s.get('active')=='1':
   base.require(s.get('rx_in')=='145' and int(s.get('rx_out','0'))&511==238 and s.get('OE')=='1','Watch active RX17 route readback')
 base.require(k.get('capture_channels') in ('0/2','RX0/CURRENT2'),'Watch radio capture channels')
 if k.get('capture_channels')=='RX0/CURRENT2':base.require(k.get('auxiliary')=='GNSS_PPS13_CH1','Watch auxiliary capture contract')
 else:base.require(k.get('tx_channel')=='1_CONSTANT0','Legacy watch TX channel')
 for key,value in dict(RX='RW_RX_ON_238_GPIO17',TX='NONE_CONSTANT0',capture_channels=k.get('capture_channels'),backend=str(int(production)),phase80=str(phase),provenance=mode).items():
  base.require(k.get(key)==value,'Watch fine contract '+key)
 for key in ('TX','issued1','rise1','fall1'):
  base.require(s.get(key)=='0','Watch TX must remain inactive: '+key)
 for key,mask in (('OE',1),('channels',5),('IRQ',1)):
  base.require(timing.timing_number(s,key)&~mask==0,'Watch RX-only mask '+key)
 copy={name:dict(row) for name,row in rows.items()}
 normalize=lambda x:'NATIVE_RW_SIGNAL' if x in ('TWATCH_NATIVE_RW_RX17_SHADOW','TWATCH_NATIVE_RW_RX17_G1') else x
 copy['N30_FINE_STATUS'].update(mode=normalize(mode),start_mode=normalize(s['start_mode']))
 for i in range(4):
  edge=copy[f'N30_FINE_EDGE{i}']
  if edge.get('present')=='1':
   base.require(edge.get('kind')=='0' and edge.get('provenance')==mode,'Watch RX edge provenance')
   edge['provenance']=normalize(mode)
 copy['N30_FINE_CONTRACT'].update(RX='RW_RX_ON_238' if mode!='CURRENT_ONLY' else 'SYNCFOUND_230',TX='RW_TX_ON_237' if mode!='CURRENT_ONLY' else 'PKT_TX_ON_235',capture_channels='0/1/2',provenance=normalize(mode))
 # The watch phase was checked against its bound receipt above. Normalize
 # only that validated convention for N30's common counter/cursor checks.
 if production:copy['N30_FINE_CONTRACT']['phase80']='125440'
 timing.validate_fine(copy)

def timing_action(action):
 if action=='timing stop':return True
 m=re.fullmatch(r'timing read (0|[1-9][0-9]*)',action)
 if m:return int(m[1])<=0xffffffff
 m=re.fullmatch(r'timing start ([1-9][0-9]*) ([1-9][0-9]*)',action)
 return bool(m and 1000<=int(m[1])<=600000 and 1<=int(m[2])<=64)

def fine_action(action):
 return action.startswith('fine ') and timing_action('timing '+action[5:])

def capture_action(action):
 if action=='capture stop':return True
 m=re.fullmatch(r'capture rolling ([1-9][0-9]*)',action)
 return bool(m and 10000<=int(m[1])<=600000)

def mutation_command(frame,nonce,action):
 base.require(timing_action(action) or fine_action(action) or window_action(action) or capture_action(action) or re.fullmatch(r'ui face [0-3]',action) or action in ('prepare_update','bootrom','ui page 0','ui page 1','ui page 2','ui page 3','ui page 4','lora diag','imu diag','imu restart','shadow diag','ui sleep','dual read 0 0','upstream auto','upstream relay','rc-parent auto','gm-parent auto','lora auto','ui radio ble','ui radio lora') or re.fullmatch(r'dual read (?:[0-9]|10) 0',action) or re.fullmatch(r'(rc|gm)-parent [0-9a-f]{16}',action),'Unsupported watch action')
 base.require(base.HEX16.fullmatch(nonce) and int(nonce,16) and nonce!=frame['begin']['nonce'],'Fresh nonce required')
 b=frame['begin']
 return INPUT_ABORT+f"v12 do {b['ticket']} {nonce} {b['factory']} {b['session']} {action}\n".encode()

CONTROLLER_OFF_CONTRACTS=('CONTROLLER_OFF_V1','FAULT_CLEANUP_OFF_V2')
def require_update_off(frame,manifest):
 """Physical V2 receipt permits retained operating errors, never failed cleanup.
 The parser has already authenticated identity/session, CRC, nonce and coverage.
 Older images retain their original strict logical-owner checks.
 """
 if manifest.get('ble_power_lifecycle')!='FAULT_CLEANUP_OFF_V2':
  return base.require_off(frame,controller_off=manifest.get('ble_power_lifecycle')=='CONTROLLER_OFF_V1')
 rows=frame['rows'];power=rows['TWATCH_UI'];lor=rows['W12_V12_LORA_STATUS'];cmd=rows['W12_V12_COMMAND']
 if manifest.get('control_contract')=='BOUNDED_CONTROL_V1':
  for key,value in dict(control_version='1',display_off_proof='1',fine_off_proof='1',fine_cleanup_fault='0',shadow_requested='0',shadow_powered='0',shadow_transition='0',shadow_owner='0',shadow_uart='0',shadow_capture='0',shadow_off_proof='1',shadow_cleanup_fault='0').items():base.require(power.get(key)==value,'Bounded control physical OFF/'+key)

 def exact(row,rules):
  for key,value in rules.items():base.require(row.get(key)==value,'Physical OFF proof/'+key)
 def number(row,key):
  value=row.get(key,'');base.require(bool(re.fullmatch(r'0|[1-9][0-9]*',value)),'Missing/invalid OFF proof/'+key)
  value=int(value);base.require(value<2**64,'Overflow OFF proof/'+key);return value
 exact(power,dict(ble_power_stage='6',ble_power_requested='0',ble_controller='0',ble_host_running='0',ble_power_worker='0',ble_power_off_ack='1',ble_off_proof='1',ble_cleanup_rc='0'))
 generation=number(power,'ble_power_generation')
 base.require(0<generation<2**32 and generation==number(power,'ble_off_gen'),'BLE OFF generation mismatch')
 start=number(power,'ble_off_start_us');deadline=number(power,'ble_off_deadline_us');completed=number(power,'ble_off_at_us')
 base.require(start>0 and deadline-start==2000000 and start<=completed<deadline,'BLE original cleanup deadline')
 exact(lor,dict(enabled='0',requested='0',worker_busy='0',off_ack='1',off_proof='15',cleanup_fault='0',TX='OFF'))
 generation=number(lor,'generation')
 base.require(generation<2**32 and generation==number(lor,'acknowledged_generation'),'LoRa OFF generation mismatch')
 exact(cmd,dict(update_stage='2',application_drained='1'))
 deadline=number(cmd,'update_deadline_us');completed=number(cmd,'update_completed_us')
 base.require(0<completed<deadline and deadline-completed<=2000000,'Original application drain deadline')
 base.require(not any(frame.get('request_delta',{'missing':1}).values()),'OFF proof coverage loss')
 return frame

def validate_lora_sleep(rows,contract='EARLY_SLEEP_COMPLETE_DATA_V3'):
 s=rows['TWATCH_LORA_SHADOW'];p=rows['TWATCH_LORA_SLEEP']
 base.require(s.get('version')=='2' and s.get('shadow_only')=='0','Unexpected LoRa window contract')
 base.require(p.get('version')=='1' and p.get('enabled')=='1','Unexpected LoRa sleep contract')
 for k in ('sleeping','active'):base.require(p.get(k) in ('0','1'),'Invalid sleep state: '+k)
 base.require(p.get('sleeping')!='1' or p.get('active')=='1','Inactive receiver cannot be asleep')
 for k in ('sleeps','wakes','refused','sleep_us','rx_us','transition_us'):
  base.require(p.get(k,'').isdigit(),'Missing sleep diagnostic: '+k)
 base.require(p.get('wake_lead_us')==('1000000' if contract=='GUARDED_SLEEP_MISS_RELEARN_V2' else '0') and p.get('poll_ms')=='50','Unexpected sleep guard')
