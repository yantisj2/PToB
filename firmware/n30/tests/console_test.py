#!/usr/bin/env python3
"""Ten-row production parser checks with synthetic nonce/CRC frames only."""
import sys,zlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import framed_console as c
nonce='0000000000000001';ticket='0000000000000002';boot='0000000000000003'
m={'factory_mac':'02:00:00:00:30:01','source_commit':'a'*40,'build_id':'b'*64,'r2_profile':'r2-g1','r2_enrollment_sha256':'c'*64,'r2_group_id':'0123456789abcdef','r2_key_epoch':1,'r2_client_identity':'0000020000003001'}
def frame(group=None,gps='ABSENT',ui=''):
 rows=[f"W12_R2_STATUS profile=r2-g1 enrollment_sha={m['r2_enrollment_sha256']} self=3 client_capabilities=0 client_control=0 source=0 serving=0 group_id={group or m['r2_group_id']} key_epoch=1 client_identity={m['r2_client_identity']}",f"W12_V12_STATUS factory={m['factory_mac']} source={m['source_commit']} build={m['build_id']} session={boot} GPS_inputs=0"]
 rows += [name+' valid=0' for name in c.base.ROWS[1:-1]]
 rows += [f'W12_V12_COMMAND nonce={nonce}',f'N30_UI profile=RC GPS={gps} page=0 display=64x128{ui}']
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
