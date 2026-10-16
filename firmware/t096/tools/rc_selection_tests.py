#!/usr/bin/env python3
"""MIT. Copyright (c) 2026 Jonathan Yantis. Actual RC selector, offline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

app=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);a=p.parse_args()
out=a.out.resolve();out.mkdir(exist_ok=False)
source=app/'src/rc_dynamic_service.c';s=source.read_text()
def function(name):
    m=re.search(r'^static [^\n]*\b'+name+r'\(',s,re.M);assert m,name
    start=m.start();brace=s.index('{',m.end());end=brace+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
decl=s[s.index('struct candidate {'):s.index('static bool parent_acquiring(')]
decl+='\nstatic bool startup_admitted,startup_attempted;\n'
(out/'selection_under_test.h').write_text(decl+'\n'+'\n'.join(function(n) for n in [
    'parent_acquiring','parent_wait','startup_complete','candidate_defer','candidate_nonroot',
    'candidate_seen','delivery_tick','candidate_choose']))
command=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
         '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),
         str(app/'tests/rc_startup_selection_test.c'),'-o',str(out/'test')]
passed=False
try:
    subprocess.run(command,check=True,timeout=30)
    subprocess.run([str(out/'test')],check=True,timeout=10)
    passed=True
finally:
    (out/'receipt.json').write_text(json.dumps(dict(passed=passed,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),hardware=False),indent=2)+'\n')
