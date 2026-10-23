#!/usr/bin/env python3
"""Ten-row production parser checks with synthetic nonce/CRC frames only."""
import sys,zlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import framed_console as c
nonce='0000000000000001';ticket='0000000000000002';boot='0000000000000003'
m={'factory_mac':'02:00:00:00:30:01','source_commit':'a'*40,'build_id':'b'*64,'r2_profile':'r2-g1','r2_enrollment_sha256':'c'*64,'r2_group_id':'0123456789abcdef','r2_key_epoch':1,'r2_client_identity':'0000020000003001'}
def frame(group=None,gps='ABSENT',ui='',page=0):
 rows=[f"W12_R2_STATUS profile=r2-g1 enrollment_sha={m['r2_enrollment_sha256']} self=3 client_capabilities=0 client_control=0 source=0 serving=0 group_id={group or m['r2_group_id']} key_epoch=1 client_identity={m['r2_client_identity']}",f"W12_V12_STATUS factory={m['factory_mac']} source={m['source_commit']} build={m['build_id']} session={boot} GPS_inputs=0"]
 rows += [name+' valid=0' for name in c.base.ROWS[1:-1]]
 rows += [f'W12_V12_COMMAND nonce={nonce}',f'N30_UI profile=RC GPS={gps} page={page} display=64x128{ui}']
 body=(f"\nW12_V12_BEGIN version=3 nonce={nonce} ticket={ticket} factory={m['factory_mac']} session={boot} source={m['source_commit']} build={m['build_id']} request_us=1000000 deadline_us=2500000 ticket_deadline_us=3000000\n"+'\n'.join(rows)+'\n').encode()
 end=f'W12_V12_END version=3 nonce={nonce} ticket={ticket} rows=10 crc32={zlib.crc32(body):08x} request_us=1000000 completed_us=1500000 deadline_us=2500000 body_driver_drained=1 counter_exhausted=0 capture_id=0000000000000000 capture_active=0 capture_deadline_us=0'
 for key,value in c.base.COUNTERS.items():end+=f' {key}=0 {value}=0 req_{key}=0'
 end=end.encode();return body+end+f' end_crc32={zlib.crc32(end,zlib.crc32(body)):08x}\n'.encode()
raw=frame();p=c.Parser(nonce,m);frames=[]
for i in range(0,len(raw),17):frames+=p.feed(raw[i:i+17])
assert len(frames)==1 and frames[0]['rows']['N30_UI']['GPS']=='ABSENT'
assert c.mutation_command(frames[0],'0000000000000004','ui page 1').endswith(b' ui page 1\n')
for bad in [frame(group='0123456789abcdee'),frame(gps='PRESENT'),raw.replace(b'page=0',b'page=1')]:
 try:c.Parser(nonce,m).feed(bad)
 except RuntimeError:pass
 else:raise AssertionError('Altered ten-row identity/capability/CRC accepted')
new=dict(m,n30_ui_revision=2)
for path in ('BLE','RC1','RC2','HOLD','WAIT','--'):
 for mode in ('AUTO','RELAY','PENDING'):
  assert len(c.Parser(nonce,new).feed(frame(ui=f' path={path} upstream={mode}')))==1
for ui in ('',' path=GPS upstream=AUTO',' path=BLE',' upstream=RELAY',' path=RC1 upstream=GM'):
 try:c.Parser(nonce,new).feed(frame(ui=ui))
 except RuntimeError:pass
 else:raise AssertionError('Missing or invalid source/upstream accepted')
print('PASS N30 ten-row production parser: nonce/CRC, fragmented delivery, group/GPS refusal, source/upstream and display-only command')

for page in range(4):
 assert len(c.Parser(nonce,dict(m,n30_ui_revision=4)).feed(frame(ui=' path=BLE upstream=AUTO',page=page)))==1
for revision,page in ((3,3),(4,4)):
 try:c.Parser(nonce,dict(m,n30_ui_revision=revision)).feed(frame(ui=' path=BLE upstream=AUTO',page=page))
 except RuntimeError:pass
 else:raise AssertionError('Page outside manifest UI revision accepted')
assert c.mutation_command(frames[0],'0000000000000004','ui page 3').endswith(b' ui page 3\n')
print('PASS four-page UI revision binding and PRECISION page command')

# Build a complete synthetic alternate frame, then re-CRC intentional faults.
def lora_frame(replace=None,omit=None):
 rows=[]
 for suffix,names in c.LORA_NUMBERS.items():
  row={key:'0' for key in names.split()};row.update(c.LORA_FIXED.get(suffix,{}))
  if suffix=='CONTEXT':row.update({key:'0000000000000000' for key in c.LORA_HEX});row['context']='0'*32
  if suffix=='POLICY':row['mode']='AUTO'
  if replace and suffix==replace[0]:row[replace[1]]=replace[2]
  if suffix!=omit:rows.append('N30_LORA_'+suffix+' '+' '.join(k+'='+v for k,v in row.items()))
 original=frame(ui=' path=LOR upstream=AUTO');body=original[:original.index(b'W12_V12_END')].decode().splitlines(True)
 body=''.join(body[:4])+''.join(row+'\n' for row in rows)+''.join(body[-2:]);body=body.encode()
 end=original[original.index(b'W12_V12_END'):].split(b' end_crc32=')[0]
 import re
 end=re.sub(rb'crc32=[0-9a-f]{8}',f'crc32={zlib.crc32(body):08x}'.encode(),end)
 return body+end+f' end_crc32={zlib.crc32(end,zlib.crc32(body)):08x}\n'.encode()
lm=dict(m,n30_ui_revision=3,n30_frame_kind='LORA')
lf=c.Parser(nonce,lm).feed(lora_frame());assert len(lf)==1
for action in ('lora diag','lora on','lora off','lora auto','ui page 2'):assert c.mutation_command(lf[0],'0000000000000004',action).endswith((' '+action+'\n').encode())
for fault in [('COUNTERS','TX','ALLOWED'),('POLICY','GPS','PRESENT'),('POLICY','acquire_us','179000000'),('HARDWARE','fault','2'),('CONTEXT','root','xyz'),('ORIGINAL','sequence',str(2**32)),('QUALITY','rate_ppb',str(-2**31-1))]:
 try:c.Parser(nonce,lm).feed(lora_frame(fault))
 except RuntimeError:pass
 else:raise AssertionError('Altered LoRa diagnostic contract accepted: '+str(fault))
try:c.Parser(nonce,lm).feed(lora_frame(omit='QUALITY'))
except RuntimeError:pass
else:raise AssertionError('Missing LoRa row accepted')
print('PASS LoRa CRC frame schema, bound identity, numeric ranges and RX-only/unknown contracts')

def timing_frame(fault=None):
 status=dict(schema='1',id='0000000000000005',active='1',until_us='60000000',every='1',seen='1',sampled='1',matched='1',overwritten='0',pair_missing='0',queued='1',oldest='1',after='0',count='1')
 sample=dict(valid='1',n='1',received_us='1000000',local20='20000000',available20='20000200',epoch='1',seq='1',coarse='3200',fine='584',raw_metadata='0',associated='1',before20='20000000',after20='20000004',bt_slot='3200',bt_hus='40',refine20='0',refined='0',phy='0',extra_us='0',enclosure_ns='1000',rssi='-70',matched='1',root='000000000000006f',boot='0000000000000037',source='2',train='3',publication='1',event='1',channel='0',remote_ns='1000000000',predicted_ns='999999950',causal='1',corrected20='21250200',created_ns='900000000',expires_ns='1500000000')
 contract=dict(observer='WORKER_ONLY',cursor='NONCONSUMING',records='16',batch='4',max_ms='600000',timestamp='DESCRIPTOR_500NS_TO_TIMER_50NS',RF_bias='UNKNOWN',UTC_bound='UNKNOWN',clock_update='NONE')
 groups=[status,sample,contract]
 if fault:groups[fault[0]][fault[1]]=fault[2]
 extra=[('N30_TIMING_STATUS',status),('N30_TIMING_SAMPLE0',sample),*[(f'N30_TIMING_SAMPLE{i}',{'valid':'0'}) for i in range(1,4)],('N30_TIMING_CONTRACT',contract)]
 original=frame(ui=' path=BLE upstream=AUTO');lines=original[:original.index(b'W12_V12_END')].decode().splitlines(True)
 body=(''.join(lines[:4])+''.join(name+' '+' '.join(k+'='+v for k,v in row.items())+'\n' for name,row in extra)+''.join(lines[-2:])).encode()
 end=original[original.index(b'W12_V12_END'):].split(b' end_crc32=')[0]
 import re
 end=re.sub(rb'crc32=[0-9a-f]{8}',f'crc32={zlib.crc32(body):08x}'.encode(),end)
 return body+end+f' end_crc32={zlib.crc32(end,zlib.crc32(body)):08x}\n'.encode()
tm=dict(m,n30_ui_revision=3,n30_frame_kind='TIMING');tf=c.Parser(nonce,tm).feed(timing_frame());assert len(tf)==1
for action in ('timing start 600000 4','timing read 0','timing read 4294967295','timing stop'):assert c.mutation_command(tf[0],'0000000000000004',action).endswith((' '+action+'\n').encode())
for action in ('timing start 600001 1','timing start 1000 65','timing read 4294967296','timing read -1'):
 try:c.mutation_command(tf[0],'0000000000000004',action)
 except RuntimeError:pass
 else:raise AssertionError('Invalid timing action admitted: '+action)
for fault in [(0,'count','2'),(0,'queued','17'),(1,'n','0'),(1,'coarse',str(2**28)),(1,'after20','19999999'),(1,'corrected20','19999999'),(1,'remote_ns','2000000000'),(1,'root','xyz'),(1,'matched','0'),(1,'rssi','-129'),(2,'clock_update','ENABLED'),(2,'RF_bias','KNOWN')]:
 try:c.Parser(nonce,tm).feed(timing_frame(fault))
 except RuntimeError:pass
 else:raise AssertionError('Invalid timing schema admitted: '+str(fault))
print('PASS timing CRC schema: complete scope, cursor/order, descriptor bounds, original envelope, command limits and observational contract')

# The configured profile must be disclosed in normal, LoRa and timing frames.
# Recompute both CRCs so refusal proves provenance checks, not damaged bytes.
from build import provisional_profile
profile=provisional_profile({'device_id':m['r2_client_identity'],'provisional_path':{'schema':1,'scope':'AGGREGATE_PATH_RELATIVE_PPS','offset_ns':47133,'root':'000000000000000b','root_boot':'0000000000000016','source_epoch':9,'receiver':'000000000000012c','receiver_source_sha256':'1'*64,'receiver_phase_correction_ns':10543,'evidence_sha256':'2'*64,'baseline_evidence_sha256':'3'*64}})
def profile_frame(raw,prefix):
 import re
 body,end=raw.split(b'W12_V12_END ',1);body=body.replace(b'W12_R2_STATUS ',('W12_R2_STATUS pcfg='+prefix+' ').encode(),1)
 end=b'W12_V12_END '+end.split(b' end_crc32=')[0];end=re.sub(rb'crc32=[0-9a-f]{8}',f'crc32={zlib.crc32(body):08x}'.encode(),end)
 return body+end+f' end_crc32={zlib.crc32(end,zlib.crc32(body)):08x}\n'.encode()
for original,manifest in ((frame(),m),(lora_frame(),lm),(timing_frame(),tm)):
 configured=dict(manifest,provisional_path_profile=profile)
 assert len(c.Parser(nonce,configured).feed(profile_frame(original,profile['id'][:8])))==1
 for bad,expected in ((original,configured),(profile_frame(original,'00000000'),configured),(profile_frame(original,profile['id'][:8]),manifest)):
  try:c.Parser(nonce,expected).feed(bad)
  except RuntimeError:pass
  else:raise AssertionError('Undisclosed or mismatched N30 profile accepted')
print('PASS N30 configured profile binding in normal, LoRa and timing CRC frames')

# TX-only filtered count is protected by the existing complete END CRC. The
# expectation flag is per request; it is not a global firmware capability.
def filtered_end(raw,value):
 body,end=raw.split(b'W12_V12_END ',1);end=b'W12_V12_END '+end.split(b' end_crc32=')[0]+b' xf='+value.encode()
 return body+end+f' end_crc32={zlib.crc32(end,zlib.crc32(body)):08x}\n'.encode()
for value in ('0','4294967295'):
 assert c.Parser(nonce,dict(m,n30_tx_capture_crc=True)).feed(filtered_end(frame(),value))[0]['end']['xf']==value
for raw in (frame(),filtered_end(frame(),'-1'),filtered_end(frame(),'4294967296'),filtered_end(frame(),'bad'),filtered_end(frame(),'0').replace(b' xf=0',b' xf=1')):
 try:c.Parser(nonce,dict(m,n30_tx_capture_crc=True)).feed(raw)
 except RuntimeError:pass
 else:raise AssertionError('Missing/invalid/corrupt TX END filtered count accepted')
print('PASS TX-only END filtered uint32, completeENDCRC and per-request mode expectation')

# RC9 is a new independently framed functional service, never RC1 fallback.
assert len(c.Parser(nonce,dict(m,n30_ui_revision=5)).feed(frame(ui=' path=RC9 upstream=AUTO')))==1
try:c.Parser(nonce,dict(m,n30_ui_revision=4)).feed(frame(ui=' path=RC9 upstream=AUTO'))
except RuntimeError:pass
else:raise AssertionError('Old UI revision accepted RC9')
assert c.mutation_command(frames[0],'0000000000000004','rc9-parent auto').endswith(b' rc9-parent auto\n')
assert c.mutation_command(frames[0],'0000000000000004','rc9 diag').endswith(b' rc9 diag\n')
def rc9_frame(change=None):
 original=frame(ui=' path=RC9 upstream=AUTO');prefix=original[:original.index(b'W12_V12_END')].decode().splitlines(True)
 values={
  'N30_RC9_STATUS':dict(sampled_us='1000000',UTC_us='1700000000000000',until_us='12000000',valid='1',following='1',fault='0'),
  'N30_RC9_ORIGIN':dict(server='0000000000000001',server_boot='0000000000000002',root='0000000000000003',root_boot='0000000000000000',source_epoch='1',emitter='0000000000000004',emitter_boot='0000000000000000'),
  'N30_RC9_QUALITY':dict(assurance='0',authority='0',metadata_known='0',upstream_link_rms_ns='4294967295',bound='UNKNOWN'),
  'N30_RC9_COUNTERS':dict(sequence='1',accepted='1',rejected='0'),
  'N30_RC9_LEASE':dict(until_us='360000000',clients='0',serving='0'),
  'N30_RC9_CONTRACT':dict(wire='1',source='RC8',hops='1',precision='UNKNOWN',BLE_training='NONE',relay='NONE',GM='NONE',RX_ONLY='1')}
 if change:
  row,key,value=change;values[row][key]=value
 replacement=[name+' '+' '.join(k+'='+v for k,v in row.items())+'\n' for name,row in values.items()]
 names=set(c.base.ROWS[1:-1]);result=[];inserted=False
 for line in prefix:
  if line.split() and line.split()[0] in names:
   if not inserted:result.extend(replacement);inserted=True
  else:result.append(line)
 body=''.join(result).encode();end=original[original.index(b'W12_V12_END'):].decode().split(' end_crc32=')[0]
 import re
 end=re.sub(r'crc32=[0-9a-f]{8}',f'crc32={zlib.crc32(body):08x}',end).encode()
 return body+end+f' end_crc32={zlib.crc32(end,zlib.crc32(body)):08x}\n'.encode()
rm=dict(m,n30_ui_revision=5,n30_frame_kind='RC9');assert len(c.Parser(nonce,rm).feed(rc9_frame()))==1
for change in [('N30_RC9_CONTRACT','precision','FINE'),('N30_RC9_CONTRACT','relay','RC1'),('N30_RC9_CONTRACT','BLE_training','YES'),('N30_RC9_LEASE','serving','1'),('N30_RC9_QUALITY','bound','100'),('N30_RC9_QUALITY','assurance','3'),('N30_RC9_ORIGIN','root','junk')]:
 try:c.Parser(nonce,rm).feed(rc9_frame(change))
 except RuntimeError:pass
 else:raise AssertionError('Invalid RC9 claim accepted '+str(change))
print('PASS RC9 ten-row nonce/CRC parser, revision and independent functional-only claims')
