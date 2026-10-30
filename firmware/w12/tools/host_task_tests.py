#!/usr/bin/env python3
"""Offline W12 host allocation and existing transport-owner regressions.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import os
import argparse,subprocess,json,hashlib
from pathlib import Path
A=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);out=p.parse_args().out.resolve();out.mkdir(parents=True,exist_ok=False)
 for name in ('host_task','transport_owner'):
  cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-parameter','-fsanitize=address,undefined','-I'+str(A/'tests/console_fakes'),str(A/'tests'/f'{name}_test.cpp'),'-o',str(out/name)]
  with (out/f'{name}-build.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
  with (out/f'{name}.log').open('w') as f:subprocess.run([str(out/name)],stdout=f,stderr=subprocess.STDOUT,check=True)
 paths=[A/'main/w12_host_task.hpp',A/'main/native_transport.cpp',A/'tests/host_task_test.cpp',A/'tests/transport_owner_test.cpp']
 (out/'receipt.json').write_text(json.dumps(dict(status='PASS',hardware_access=False,sources={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}),indent=2)+'\n');print('PASS checked static allocation and actual transport-owner regressions under ASan/UBSan')
if __name__=='__main__':main()
