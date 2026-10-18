#!/usr/bin/env python3
"""Actual RC7 admission/path refusal body with public protocol; offline only.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

app=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--deps',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args();out=a.out.resolve();out.mkdir(exist_ok=False)
def function(path,name):
    text=path.read_text()
    m=re.search(r'^static [^\n]*\b'+name+r'\(',text,re.M);assert m,name
    start=m.start();end=text.index('{',m.end())+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
service=app/'src/rc_dynamic_service.c';transport=app/'src/rc_dynamic_transport.inc'
(out/'rc_relay_admission_under_test.h').write_text(
    function(service,'parent_path_usable')+'\n'+function(transport,'lt_apply_read'))
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
sources=[app/'tests/rc_relay_admission_test.c',app/'src/rc_boundary.c',
         app/'protocol/rc_dynamic.c',app/'protocol/group_auth.c',
         *[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]]
cmd=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
     '-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined',
     '-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),
     '-I'+str(app/'protocol'),'-I'+str(tiny/'include'),*map(str,sources),'-o',str(out/'test')]
passed=False
try:
    with (out/'test.log').open('x') as log:
        subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
        subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
    passed=True
finally:
    (out/'receipt.json').write_text(json.dumps(dict(passed=passed,hardware=False,command=cmd,
        sources={str(f):hashlib.sha256(f.read_bytes()).hexdigest()
                 for f in [service,transport,Path(__file__).resolve(),*sources]}),indent=2)+'\n')
print('PASS actual relay admission/path refusal under ASan/UBSan')
