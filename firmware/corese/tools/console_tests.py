#!/usr/bin/env python3
"""Offline synthetic protocol fixtures. No USB, serial enumeration, RF or reset.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import io,json,zlib
import console as c
N='4'*16;BOOT='5'*16
receipt={'factory_mac':'02:00:00:00:00:01','device_id':'0000020000000001','public_commit':'1'*40,'runtime_identity':{'build_id':'2'*64,'r2_enrollment_sha256':'3'*64}}
def wire(change=None):
 begin=f"W12_V12_BEGIN version=3 nonce={N} ticket={'6'*16} factory={receipt['factory_mac']} session={BOOT} source={'1'*40} build={'2'*64} request_us=1000000 deadline_us=2500000 ticket_deadline_us=3000000"
 rows=[f"W12_R2_STATUS profile=r2-g1 self=3 enrollment_sha={'3'*64} client_identity={receipt['device_id']} source=0 serving=0 client_control=0 client_capabilities=0"]
 rows += [n+' sample=1' for n in ('W12_V12_STATUS','W12_V12_GPS','W12_V12_LORA_STATUS','W12_V12_PUBLIC','W12_V12_CONSOLE','W12_V12_DIRECT','W12_V12_DIAGNOSTIC_TX')]
 rows += [f'W12_V12_COMMAND nonce={N} accepted=1 action=STATUS','N30_UI sample=1']
 loss=0;deadline=2500000
 if change=='serve':rows[0]=rows[0].replace('serving=0','serving=1')
 if change=='row':rows.pop(3)
 if change=='boot':begin=begin.replace(BOOT,'7'*16)
 if change=='refused':rows[-2]=rows[-2].replace('accepted=1','accepted=0')
 if change=='deadline':deadline=1000001
 if change=='loss':loss=1
 raw=('\n'+begin+'\n'+'\n'.join(rows)+'\n').encode();crc=zlib.crc32(raw)
 end=f'W12_V12_END version=3 nonce={N} ticket={"6"*16} rows={len(rows)} crc32={crc:08x} request_us=1000000 completed_us=1010000 deadline_us={deadline} body_driver_drained=1 counter_exhausted=0'
 for key in ('dropped','truncated','isr_refused','write_errors','expired','frame_drops'):end+=f' {key}={loss if key=="write_errors" else 0} req_{key}=0'
 result=raw+end.encode()+f' end_crc32={zlib.crc32(end.encode(),crc):08x}\n'.encode()
 if change=='crc':result=result.replace(b'client_control=0',b'client_control=1')
 if change=='nonce':result=result.replace(N.encode(),b'8'*16)
 return result
class Port:
 def __init__(self,data):self.data=data
 def write(self,data):return len(data)
 def read(self,n):data,self.data=self.data[:n],self.data[n:];return data
class Clock:
 def __init__(self):self.now=0
 def tick(self):self.now+=.1;return self.now
saved=(c.secrets.token_hex,c.time.monotonic)
try:
 for mutation in (None,'serve','row','boot','refused','deadline','loss','crc','nonce'):
  client=c.Client.__new__(c.Client);client.r=receipt;client.session=BOOT;client.frame=None;client.buffer=b'';client.startup_retries=3;client.log=io.BytesIO();client.port=Port(wire(mutation));clock=Clock();c.time.monotonic=clock.tick;c.secrets.token_hex=lambda n:N
  try:client.request()
  except (RuntimeError,TimeoutError,KeyError,ValueError):
   if mutation is None:raise
  else:
   if mutation is not None:raise RuntimeError('Accepted invalid fixture: '+mutation)
 print('PASS console: whole-frame CRC, nonce, boot, rows, original deadline, loss, command refusal and client-only permission; no hardware')
finally:c.secrets.token_hex,c.time.monotonic=saved
