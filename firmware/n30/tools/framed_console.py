#!/usr/bin/env python3
"""N30 ten-row CRC console parser. Import opens no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import importlib.util,re,zlib
from pathlib import Path
p=Path(__file__).resolve().parents[2]/'w12/tools/framed_console.py'
spec=importlib.util.spec_from_file_location('n30_w12_console',p);base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
fields=base.fields;fresh_nonce=base.fresh_nonce;status_command=base.status_command

# New N30 TX-only LOGv2. The wire CRC excludes only its own field; the
# protected bytes include the leading delimiter, deterministic header, exact
# normalized payload byte length and final newline. Partial expiry is invalid.
_TX_LOG=re.compile(rb'W12_V12_LOG version=2 mode=TX capture_id=([0-9a-f]{16}) log_seq=([1-9][0-9]*) enqueue_us=([1-9][0-9]*) record_bytes=([1-9][0-9]*) record_crc32=([0-9a-f]{8}) record=(.*)\Z',re.S)
def parse_tx_log(line,capture_id=None):
 base.require(isinstance(line,bytes) and len(line)<1536,'TX LOG bounded complete bytes required')
 match=_TX_LOG.fullmatch(line);base.require(match is not None,'TX LOGv2 envelope required')
 cid,seq,enqueue,length,crc,payload=match.groups();seq,enqueue,length=int(seq),int(enqueue),int(length)
 base.require(int(cid,16)>0 and seq<2**64 and enqueue<2**64 and length==len(payload) and 0<length<1536,'TX LOG identity/length bounds')
 base.require(capture_id is None or cid.decode()==capture_id,'TX LOG capture scope changed')
 base.require(payload.endswith(b'\n') and b'\n' not in payload[:-1] and all(32<=b<127 or b==10 for b in payload),'TX LOG normalized single line required')
 base.require(payload.startswith((b'W12_G1_TX_EST ',b'W12_G1_TX_PATH ')) and payload.endswith(b' bound=UNKNOWN\n'),'TX LOG complete selected record required')
 header=line.split(b' record_crc32=',1)[0]
 base.require(zlib.crc32(b'\n'+header+b' record='+payload)==int(crc,16),'TX LOG header/payload CRC mismatch')
 return dict(capture_id=cid.decode(),log_seq=seq,enqueue_us=enqueue,record_bytes=length,record_crc32=crc.decode(),record=payload.decode().rstrip('\n'))

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
TIMING_ROWS=('W12_R2_STATUS','W12_V12_STATUS','N30_TIMING_STATUS',*(f'N30_TIMING_SAMPLE{i}' for i in range(4)),'N30_TIMING_CONTRACT','W12_V12_COMMAND','N30_UI')
FINE_ROWS=('W12_R2_STATUS','W12_V12_STATUS','N30_FINE_STATUS',*(f'N30_FINE_EDGE{i}' for i in range(4)),'N30_FINE_CONTRACT','W12_V12_COMMAND','N30_UI')
def validate_fine(rows):
 s=rows['N30_FINE_STATUS'];base.require(s.get('schema')=='1' and bool(base.HEX16.fullmatch(s.get('id',''))),'Fine schema/id')
 for key in ('every','seen','RX','TX','sampled','overwritten','oldest','after','count','incarnation','hz','current','timeout','invalid','poll_max','cycles_max','service_max_us','first32','latest32','wraps','callbacks','budget_refused','expired_refused','inactive_callbacks','rx_out','tx_out','rx_in','tx_in','OE','channels','IRQ'):timing_number(s,key)
 for key in ('until_us','latest80','first_us','last_us'):timing_number(s,key,64)
 timing_number(s,'error',32,True)
 for key in ('active','pins','released','contained'):base.require(s.get(key) in ('0','1'),'Fine boolean '+key)
 base.require(int(s['OE'])<=3 and int(s['channels'])<=7 and int(s['IRQ'])<=3,'Fine hardware masks')
 base.require(int(s['every'])<=64 and int(s['count'])<=4 and int(s['RX'])+int(s['TX'])==int(s['seen']) and int(s['sampled'])<=int(s['seen']),'Fine bounds/counters')
 if s['active']=='1':base.require(s['hz']=='80000000' and s['error']=='0' and int(s['current'])>=2 and int(s['incarnation'])>0,'Fine active latch/domain')
 mode=s.get('mode')
 if mode is not None:
  base.require(mode in ('NATIVE_SIGNAL','NATIVE_RW_SIGNAL','SYNTHETIC_GPIO','CURRENT_ONLY') and s.get('start_mode') in ('NATIVE_SIGNAL','NATIVE_RW_SIGNAL','SYNTHETIC_GPIO','CURRENT_ONLY'),'Fine provenance mode')
  for key in ('probe_spacing_ms','probe_goal','attempted','issued0','issued1','rise0','fall0','rise1','fall1','initial_low','verified_levels','input_levels','level_poll_max','start_reads','start_timeouts','start_poll_max','start_cycles_max'):timing_number(s,key)
  for key in ('last_issue_us','stopped_us','quiet_us'):timing_number(s,key,64)
  base.require(all(int(s[k])<=3 for k in ('initial_low','verified_levels','input_levels')) and int(s['level_poll_max'])<=128,'Probe level masks/polls')
  base.require(int(s['issued0'])==int(s['rise0'])+int(s['fall0']) and int(s['issued1'])==int(s['rise1'])+int(s['fall1']) and int(s['attempted'])>=int(s['issued0'])+int(s['issued1']),'Probe issued/polarity counts')
  if mode=='SYNTHETIC_GPIO':
   base.require(s['pins']=='1' and s['every']=='1' and 50<=int(s['probe_spacing_ms'])<=1000 and 4<=int(s['probe_goal'])<=24 and int(s['probe_goal'])%4==0 and int(s['attempted'])<=int(s['probe_goal']),'Probe fixed bounded pattern')
  else:base.require(all(s[k]=='0' for k in ('probe_spacing_ms','probe_goal','attempted','issued0','issued1','initial_low','verified_levels','last_issue_us','stopped_us','quiet_us')),'Nonprobe carries synthetic intent')
 count=0;last=int(s['after'])
 for i in range(4):
  r=rows[f'N30_FINE_EDGE{i}'];base.require(r.get('present') in ('0','1'),'Fine edge presence')
  if r['present']=='0':continue
  base.require(i==count,'Fine batch hole');count+=1
  for key in ('n','raw32','incarnation'):timing_number(r,key)
  for key in ('raw80','tick20','seen_us'):timing_number(r,key,64)
  for key in ('kind','extended'):base.require(r.get(key) in ('0','1'),'Fine edge boolean '+key)
  base.require(r.get('polarity') in ('0','1','2') and r.get('association')=='UNESTABLISHED','Fine polarity/association')
  base.require(last<int(r['n'])<=int(s['sampled']) and r['incarnation']==s['incarnation'],'Fine cursor/incarnation');last=int(r['n'])
  if mode is not None:base.require(r.get('provenance')==mode,'Edge immutable provenance')
  if r['extended']=='1':base.require(int(r['raw80'])%2**32==int(r['raw32']) and int(r['tick20'])>=1000000,'Fine raw counter identity')
 base.require(count==int(s['count']),'Fine count')
 contract={'observer':'RAW_ONLY','domain':'MCPWM1_80MHZ','RX':'SYNCFOUND_230','TX':'PKT_TX_ON_235','CURRENT':'CH2_SW128','capture_channels':'0/1/2','edge':'RISING_AND_FALLING','endpoint':'UNKNOWN','coalesced':'UNKNOWN','packet_association':'NONE','RF_bound':'UNKNOWN','UTC_bound':'UNKNOWN','clock_update':'NONE','production':'GPTIMER20','offset':'LEGACY_ONLY'}
 if mode=='SYNTHETIC_GPIO':contract.update(RX='GPIO4_SYNTHETIC',TX='GPIO5_SYNTHETIC')
 if mode=='NATIVE_RW_SIGNAL':contract.update(RX='RW_RX_ON_238',TX='RW_TX_ON_237')
 if mode is not None:contract.update(provenance=mode,expected='SYNTHETIC_VERIFIED_PAD_TRANSITIONS_ONLY' if mode=='SYNTHETIC_GPIO' else 'NONE')
 for key,value in contract.items():base.require(rows['N30_FINE_CONTRACT'].get(key)==value,'Fine contract '+key)

def timing_number(row,key,bits=32,signed=False):
 value=row.get(key,'');base.require(bool(re.fullmatch(r'-?(?:0|[1-9][0-9]*)' if signed else r'(?:0|[1-9][0-9]*)',value)),'Invalid timing number: '+key)
 n=int(value);base.require(-(2**(bits-1))<=n<2**(bits-1) if signed else 0<=n<2**bits,'Timing number range: '+key);return n
def validate_timing(rows):
 s=rows['N30_TIMING_STATUS'];base.require(s.get('schema')=='1' and bool(base.HEX16.fullmatch(s.get('id',''))),'Timing schema/id')
 for key in ('every','seen','sampled','matched','overwritten','pair_missing','oldest','after','count'):timing_number(s,key)
 for key in ('active','queued'):timing_number(s,key,8)
 timing_number(s,'until_us',64)
 base.require(int(s['active'])<=1 and int(s['queued'])<=16 and int(s['count'])<=4 and int(s['every'])<=64,'Timing configuration bounds')
 count=0;last=int(s['after'])
 for i in range(4):
  r=rows[f'N30_TIMING_SAMPLE{i}'];base.require(r.get('valid') in ('0','1'),'Timing validity')
  if r['valid']=='0':continue
  base.require(i==count,'Timing batch has a hole');count+=1
  for key in ('n','epoch','seq','coarse','bt_slot','bt_hus','enclosure_ns','source','train','publication'):timing_number(r,key)
  for key in ('received_us','local20','available20','before20','after20','remote_ns','corrected20','created_ns','expires_ns'):timing_number(r,key,64)
  timing_number(r,'predicted_ns',64,True)
  for key in ('fine','event','raw_metadata'):timing_number(r,key,16)
  for key in ('channel','phy','extra_us'):timing_number(r,key,8)
  for key in ('refine20','rssi'):timing_number(r,key,8,True)
  for key in ('refined','matched','causal','associated'):base.require(r.get(key) in ('0','1'),'Timing boolean: '+key)
  for key in ('root','boot'):base.require(bool(base.HEX16.fullmatch(r.get(key,''))),'Timing identity: '+key)
  base.require(int(r['n'])>last and int(r['n'])<=int(s['sampled']),'Timing cursor/order');last=int(r['n'])
  base.require(0<int(r['local20'])<=int(r['available20']) and 0<int(r['before20'])<=int(r['after20']),'Timing timestamp order')
  base.require(int(r['bt_hus'])<=624 and int(r['coarse'])<2**28 and int(r['bt_slot'])<2**28 and (int(r['fine'])&1023)<=624 and (int(r['channel'])<=36 or r['channel']=='255'),'Timing descriptor range')
  if r['matched']=='1':base.require(int(r['corrected20'])>=int(r['available20']) and int(r['created_ns'])<=int(r['remote_ns'])<int(r['expires_ns']),'Timing correction causality/envelope')
  base.require(r['matched']=='1' or (r['causal']=='0' and r['remote_ns']=='0'),'Unmatched timing sample supplied a correction')
 base.require(count==int(s['count']),'Timing batch count')
 contract={'observer':'WORKER_ONLY','cursor':'NONCONSUMING','records':'16','batch':'4','max_ms':'600000','timestamp':'DESCRIPTOR_500NS_TO_TIMER_50NS','RF_bias':'UNKNOWN','UTC_bound':'UNKNOWN','clock_update':'NONE'}
 for key,value in contract.items():base.require(rows['N30_TIMING_CONTRACT'].get(key)==value,'Timing contract: '+key)
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
  if manifest.get('n30_frame_kind')=='TIMING':self.expected_rows=TIMING_ROWS
  if manifest.get('n30_frame_kind')=='FINE':self.expected_rows=FINE_ROWS
 def feed(self,data):
  frames=super().feed(data)
  for f in frames:
   if self.manifest.get('n30_frame_kind')=='LORA':validate_lora(f['rows'])
   if self.manifest.get('n30_frame_kind')=='TIMING':validate_timing(f['rows'])
   if self.manifest.get('n30_frame_kind')=='FINE':validate_fine(f['rows'])
   mesh=f['rows']['W12_R2_STATUS'];ui=f['rows']['N30_UI'];status=f['rows']['W12_V12_STATUS'];m=self.manifest
   if 'xf' in f['end']:base.require(f['end']['xf'].isdigit() and 0<=int(f['end']['xf'])<2**32,'TX filtered counter range')
   if m.get('n30_tx_capture_crc'):base.require('xf' in f['end'],'TX capture mode/counter missing')
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
 tx_capture=bool(re.fullmatch(r'capture tx [1-9][0-9]*',action))
 if tx_capture:base.require(1000<=int(action.split()[2])<=10000,'TX capture duration bound')
 fine=action.startswith('fine ')
 probe=bool(re.fullmatch(r'fine probe [1-9][0-9]* [1-9][0-9]*',action))
 if probe:
  spacing,edges=map(int,action.split()[2:]);base.require(50<=spacing<=1000 and 4<=edges<=24 and edges%4==0,'Fixed synthetic probe bounds')
 checked='timing '+action[5:] if fine else action
 timing=checked=='timing stop' or bool(re.fullmatch(r'timing read (0|[1-9][0-9]*)',checked)) or bool(re.fullmatch(r'timing start [1-9][0-9]* [1-9][0-9]*',checked))
 if timing:
  nums=[int(x) for x in checked.split()[2:]]
  base.require(not nums or (0<=nums[0]<2**32 if checked.startswith('timing read ') else 1000<=nums[0]<=600000 and 1<=nums[1]<=64),'Timing command bounds')
 if not timing and not probe and not tx_capture and action not in ('ui page 0','ui page 1','ui page 2','lora diag','lora auto','lora on','lora off'):return base.mutation_command(frame,nonce,action)
 base.require(base.HEX16.fullmatch(nonce) and int(nonce,16) and nonce!=frame['begin']['nonce'],'Fresh UI request nonce required')
 b=frame['begin'];return f"v12 do {b['ticket']} {nonce} {b['factory']} {b['session']} {action}\n".encode()
