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
indices=re.search(r'^static uint8_t node, parent_index;$',s,re.M)
assert indices,'Actual bounded owner index declaration'
decl=indices[0]+'\n'+s[s.index('struct candidate {'):s.index('static bool parent_acquiring(')]
decl+='\nstatic bool startup_admitted,startup_attempted;\n'
(out/'selection_under_test.h').write_text(decl+'\n'+'\n'.join(function(n) for n in [
    'parent_acquiring','parent_wait','startup_complete','candidate_defer','candidate_nonroot',
    'candidate_seen','delivery_tick','candidate_choose']))
flags=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
       '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include')]
sources=[source,Path(__file__).resolve(),app/'include/rc_stream_quality.h',
         app/'include/link_quality.h',app/'include/rc_admission_policy.h',app/'include/rc_search.h',
         app/'tests/rc_startup_selection_test.c',app/'tests/rc_stream_quality_test.c',
         app/'src/watchdog_policy.c',app/'include/watchdog_policy.h']
commands=[]
passed=False
try:
    for name in ['rc_startup_selection','rc_stream_quality']:
        command=[*flags,str(app/'tests'/(name+'_test.c')),str(app/'src/watchdog_policy.c'),'-o',str(out/name)]
        commands.append(command)
        subprocess.run(command,check=True,timeout=30)
        subprocess.run([str(out/name)],check=True,timeout=10)
    passed=True
finally:
    (out/'receipt.json').write_text(json.dumps(dict(passed=passed,hardware=False,
        sources={str(f.relative_to(app)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sources},
        commands=commands),indent=2)+'\n')
