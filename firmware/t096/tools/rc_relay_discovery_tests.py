#!/usr/bin/env python3
"""MIT. Copyright (c) 2026 Jonathan Yantis. Production relay GATT interoperability, offline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

app = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
out = a.out.resolve()
out.mkdir(exist_ok=False)
source = app/'src/rc_dynamic_transport.inc'
s = source.read_text()
def function(name):
    match = re.search(r'^static [^\n]*\b'+name+r'\(', s, re.M)
    assert match, name
    start = match.start()
    brace = s.index('{', match.end())
    end, depth = brace+1, 1
    while depth:
        depth += (s[end]=='{')-(s[end]=='}')
        end += 1
    return s[start:end]
names = ['lt_read_done','lt_relay_located','lt_relay_locate','lt_relay_read','lt_relay_fetch']
(out/'rc_relay_discovery_under_test.h').write_text('\n'.join(function(n) for n in names)+'\n')
test = app/'tests/rc_relay_discovery_test.c'
command = [os.environ.get('CC') or shutil.which('clang'), '-std=c11','-Wall','-Wextra','-Werror',
           '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),str(test),'-o',str(out/'test')]
passed = False
try:
    subprocess.run(command, check=True, timeout=30)
    subprocess.run([str(out/'test')], check=True, timeout=10)
    tz_command=command.copy();tz_command.insert(1,'-DR2_TZ_WIRE=1');tz_command[-1]=str(out/'tz-test')
    subprocess.run(tz_command,check=True,timeout=30)
    subprocess.run([str(out/'tz-test')],check=True,timeout=10)
    passed = True
finally:
    (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'production_functions':names,
        'sources':{str(f.relative_to(app)):hashlib.sha256(f.read_bytes()).hexdigest()
                   for f in [source,test,Path(__file__).resolve()]},'command':command},indent=2)+'\n')
