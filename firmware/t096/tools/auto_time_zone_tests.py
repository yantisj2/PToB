#!/usr/bin/env python3
"""Production automatic-TZ tests, pinned database integrity and calendar oracle.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
from bisect import bisect_right
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

APP=Path(__file__).resolve().parents[1]
REPO=APP.parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
manifest=json.loads((APP/'data/manifest.json').read_text())
header=(APP/'data/time_zone_data.h').read_bytes()
assert hashlib.sha256(header).hexdigest()==manifest['header_sha256']
data=bytes(int(v,16)for v in re.findall(rb'0x([0-9a-f]{2})',header))
assert len(data)==manifest['image_bytes']<=16384
assert hashlib.sha256(data).hexdigest()==manifest['image_sha256']
for name,digest in manifest['inputs_sha256'].items():
 assert hashlib.sha256((REPO/'tools/time_zones'/name).read_bytes()).hexdigest()==digest,name
assert hashlib.sha256((REPO/'tools/time_zones/generate.py').read_bytes()).hexdigest()==manifest['generator_sha256']
start,end=manifest['sources']['valid_from'],manifest['sources']['valid_until']
oracle=out/'oracle.txt'
coverage=json.loads((APP/'data/coverage.json').read_text())
assert not coverage['counts'].get('wrong',0)
geography=out/'geography.txt'
geography.write_text(''.join(f"{r['latitude_e7']} {r['longitude_e7']} {r['actual_id']}\n"for r in coverage['locations']))
with oracle.open('w')as f:
 for row in manifest['zones']:
  changes=row['transitions'];instants=[r[0]for r in changes]
  samples=set(range(start,end,86400))|{start,end-1}
  for t,_ in changes:samples.update((t-1,t,t+1))
  for t in sorted(samples):
   i=bisect_right(instants,t);offset=row['initial_offset_min']if not i else changes[i-1][1]
   f.write(f"{row['id']} {t*1000000} {offset}\n")
cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
command=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
 '-fno-sanitize-recover=all','-DR2_TZ_AUTO=1','-DR2_TZ_WIRE=1','-I'+str(APP/'include'),
 '-I'+str(APP/'protocol'),*[str(APP/'src'/n)for n in ('time_zone_database.c','time_zone_selector.c','nmea.c','clock.c')],
 str(APP/'tests/auto_time_zone_test.c'),'-o',str(out/'test')]
with (out/'compile.log').open('w')as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=45)
with (out/'run.log').open('w')as log:subprocess.run([str(out/'test'),str(oracle),str(geography)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=45)
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',command=command,image_sha256=manifest['image_sha256'],hardware=False),indent=2)+'\n')
print((out/'run.log').read_text().strip())
