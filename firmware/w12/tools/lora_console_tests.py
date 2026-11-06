#!/usr/bin/env python3
"""Actual W12 passive LoRa diagnostic builder/writer/parser; fixed bounds, no USB."""
import os
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import argparse,hashlib,json,subprocess,re,zlib
from pathlib import Path
from lora_console import Parser
from framed_console import require_coverage
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--tz',action='store_true');a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
s=(APP/'main/native_main.cpp').read_text();start=s.index('[[gnu::noinline]] void lora_status_frame');end=s.index('\n}\n#endif',start)+2;(out/'actual_lora_frame.inc').write_text(s[start:end])
flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DV12_R2_ENABLED=1','-DPTOB_W12_BLE_RECOVERY=1','-DPTOB_RC9=1']
if a.tz:flags+=['-DR2_TZ_WIRE=1']
for q in (out,APP/'tests/console_fakes',APP/'main',APP/'main/r2/r2-g1',APP/'main/r2/rc7/include'):flags+=['-I'+str(q)]
subprocess.run([os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').is_file() else 'clang++'),*flags,APP/'tests/lora_console_worker_test.cpp','-o',out/'writer'],check=True);subprocess.run([out/'writer',out/'wire.txt'],check=True)
m=dict(factory_mac='02:00:00:00:00:05',source_commit='0'*40,build_id='0'*64,console_frame_bytes=8704,fault_watchdog='SUSTAINED_FAULT_120S_V1',r2_profile='r2-g1',r2_enrollment_sha256='0'*64,r2_group_id='0000000000000011',r2_key_epoch=9,r2_client_identity='0000020000000005')
wire=(out/'wire.txt').read_bytes();frames=Parser('0000000000000abc',m,'0000000000001234').feed(wire);assert len(frames)==1;require_coverage(frames[0]);assert len(frames[0]['rows'])==10
mesh=frames[0]['rows']['W12_R2_STATUS'];assert mesh['dual_schema']=='2' and mesh['parent_view']=='RING8' and len(mesh['parent_events'])==8 and len(wire)<=8704
def re_crc(raw):
 body,end=raw.split(b'W12_V12_END ',1);end=b'W12_V12_END '+end.rstrip(b'\n').rsplit(b' end_crc32=',1)[0]
 end=re.sub(rb' crc32=[0-9a-f]{8}',b' crc32='+f'{zlib.crc32(body):08x}'.encode(),end)
 return body+end+b' end_crc32='+f'{zlib.crc32(end,zlib.crc32(body)):08x}'.encode()+b'\n'
for old,new in ((b'wd_no_time_limit_us=720000000',b'wd_no_time_limit_us=300000000'),(b'absence_expected=0',b'absence_expected=2'),(b'authority=UNCHANGED',b'authority=GRANTED'),(b'parent_view=RING8',b'parent_view=COMPLETE_HISTORY'),(b'parent_returned=8',b'parent_returned=9'),(b'permission_gap_limit_us=1500000',b'permission_gap_limit_us=0'),(b'remaining_us=30000000',b'remaining_us=30000001'),(('ble_refusal='+mesh['ble_refusal']).encode(),b'ble_refusal=4096')):
 assert old in wire
 try:Parser('0000000000000abc',m,'0000000000001234').feed(re_crc(wire.replace(old,new)))
 except RuntimeError:pass
 else:raise AssertionError('Invalid source/parent diagnostic accepted: '+str(old))
# Any changed byte in a body row must fail the complete-frame CRC.
changed=wire.replace(b'phase_ns=0',b'phase_ns=1');assert changed!=wire
try:Parser('0000000000000abc',m,'0000000000001234').feed(changed)
except RuntimeError:pass
else:raise AssertionError('Changed diagnostic body accepted')
paths=[APP/'main/native_main.cpp',APP/'main/bounded_log.cpp',APP/'main/bounded_log.hpp',APP/'tests/lora_console_worker_test.cpp',APP/'tools/lora_console.py',APP/'main/lora_diagnostics.cpp',Path(__file__)]
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',hardware_access=False,tz=a.tz,wire_bytes=len(wire),scope='ACTUAL_RC9_NATIVE_BUILDER_WRITER_PARSER_FIXED10_NORMAL9_CRC_ORIGINAL_EXPIRY_ASAN_UBSAN',sources={str(q):hashlib.sha256(q.read_bytes()).hexdigest() for q in paths}),indent=2)+'\n')
print('PASS actual passive LoRa diagnostic writer/parser')
