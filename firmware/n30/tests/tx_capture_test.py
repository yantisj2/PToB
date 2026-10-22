#!/usr/bin/env python3
"""Strict TX LOGv2 CRC byte grammar and corruption controls; no hardware."""
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import argparse,sys,zlib,re
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import framed_console as c
p=argparse.ArgumentParser();p.add_argument('--wire',type=Path);a=p.parse_args()
payload=b'W12_G1_TX_EST x=1 bound=UNKNOWN\n'
def wire(cid=b'000000000000000a',seq=b'2',enqueue=b'1000',data=payload,length=None):
 header=b'W12_V12_LOG version=2 mode=TX capture_id='+cid+b' log_seq='+seq+b' enqueue_us='+enqueue+b' record_bytes='+str(len(data) if length is None else length).encode()
 crc=zlib.crc32(b'\n'+header+b' record='+data)
 return header+b' record_crc32='+f'{crc:08x}'.encode()+b' record='+data
raw=wire();r=c.parse_tx_log(raw,'000000000000000a');assert r['record_bytes']==len(payload)
if a.wire:
 actual=a.wire.read_bytes();assert actual.startswith(b'\n');assert c.parse_tx_log(actual[1:],'000000000000000a')['record']==r['record']
 full=Path(str(a.wire)+'-max-frame.txt').read_bytes();body,end=full.split(b'W12_V12_END ',1);end=b'W12_V12_END '+end;protected,endcrc=end.rsplit(b' end_crc32=',1)
 assert len(end)<800 and b' xf=4294967295' in end and zlib.crc32(protected,zlib.crc32(body))==int(endcrc.strip(),16)
 timing={k:int(re.search(rb' '+k.encode()+rb'=([0-9]+)',end).group(1)) for k in ('request_us','completed_us','deadline_us')}
 assert 0<timing['request_us']<=timing['completed_us']<timing['deadline_us']
 assert zlib.crc32(body)==int(re.search(rb' crc32=([0-9a-f]{8})',end).group(1),16)
checks=0
for bad in (raw.replace(b'capture_id=000000000000000a',b'capture_id=000000000000000b'),raw.replace(b'log_seq=2',b'log_seq=3'),raw.replace(b'enqueue_us=1000',b'enqueue_us=1001'),raw.replace(b'record_bytes=32',b'record_bytes=33'),raw.replace(b'x=1',b'x=2'),raw[:-1],raw[:-2]+b'\n',wire(data=payload+b'forged\n'),wire(data=payload[:-1]),wire(length=31),wire(cid=b'0000000000000000'),wire(seq=str(2**64).encode()),wire(data=b'W12_G1_TX_EST\n'),wire(data=b'W12_R2_RX_SHADOW x=1 bound=UNKNOWN\n'),wire(data=b'W12_G1_TX_EST bad\tfield bound=UNKNOWN\n'),wire(data=b'W12_G1_TX_EST bad\rfield bound=UNKNOWN\n')):
 try:c.parse_tx_log(bad,'000000000000000a')
 except RuntimeError:checks+=1
 else:raise AssertionError('Invalid TX record accepted: '+repr(bad))
# Identity/type fields remain joined by the caller only after this CRC gate.
try:c.parse_tx_log(raw,'000000000000000b')
except RuntimeError:checks+=1
else:raise AssertionError('Foreign capture accepted')
print('PASS TX LOGv2 actualproducer/seed0/header/payload/length/newline/scope/partial controls',checks)
