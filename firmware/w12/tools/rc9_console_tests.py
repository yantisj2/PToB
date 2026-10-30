#!/usr/bin/env python3
"""Actual W12 RC9 diagnostic builder/writer/parser; fixed bounds, no USB."""
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import argparse,hashlib,json,subprocess
from pathlib import Path
from rc9_console import Parser
from framed_console import require_coverage
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--tz',action='store_true');a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
s=(APP/'main/native_main.cpp').read_text();start=s.index('[[gnu::noinline]] void rc9_status_frame');end=s.index('\n}\n#endif',start)+2;(out/'actual_rc9_frame.inc').write_text(s[start:end])
flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DV12_R2_ENABLED=1','-DPTOB_RC9=1']
if a.tz:flags+=['-DR2_TZ_WIRE=1']
for q in (out,APP/'tests/console_fakes',APP/'main',APP/'main/r2/r2-g1',APP/'main/r2/rc7/include'):flags+=['-I'+str(q)]
subprocess.run(['/opt/homebrew/opt/llvm/bin/clang++',*flags,APP/'tests/rc9_console_worker_test.cpp','-o',out/'writer'],check=True);subprocess.run([out/'writer',out/'wire.txt'],check=True)
m=dict(factory_mac='02:00:00:00:00:05',source_commit='0'*40,build_id='0'*64,console_frame_bytes=8704,fault_watchdog='SUSTAINED_FAULT_120S_V1',r2_profile='r2-g1',r2_enrollment_sha256='0'*64,r2_group_id='0000000000000011',r2_key_epoch=9,r2_client_identity='0000020000000005')
wire=(out/'wire.txt').read_bytes();frames=Parser('0000000000000abc',m,'0000000000001234').feed(wire);assert len(frames)==1;require_coverage(frames[0]);assert len(frames[0]['rows'])==10
# Any changed byte in a body row must fail the complete-frame CRC.
changed=wire.replace(b'lora_rms_ns=123',b'lora_rms_ns=124');assert changed!=wire
try:Parser('0000000000000abc',m,'0000000000001234').feed(changed)
except RuntimeError:pass
else:raise AssertionError('Changed diagnostic body accepted')
paths=[APP/'main/native_main.cpp',APP/'main/bounded_log.cpp',APP/'main/bounded_log.hpp',APP/'tests/rc9_console_worker_test.cpp',APP/'tools/rc9_console.py',Path(__file__)]
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',hardware_access=False,tz=a.tz,wire_bytes=len(wire),scope='ACTUAL_RC9_NATIVE_BUILDER_WRITER_PARSER_FIXED10_NORMAL9_CRC_ORIGINAL_EXPIRY_ASAN_UBSAN',sources={str(q):hashlib.sha256(q.read_bytes()).hexdigest() for q in paths}),indent=2)+'\n')
print('PASS actual RC9 diagnostic writer/parser')
