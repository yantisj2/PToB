#!/usr/bin/env python3
"""MIT. Copyright (c) 2026 Jonathan Yantis. Actual RC path role boundary, offline."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
app=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
out=a.out.resolve();out.mkdir(exist_ok=False)
source=app/'src/rc_dynamic_service.c';s=source.read_text()
def function(n):
 m=re.search(r'^static [^\n]*\b'+n+r'\(',s,re.M);start=m.start();brace=s.index('{',m.end());end=brace+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
indices=re.search(r'^static uint8_t node, parent_index;$',s,re.M)
bound=re.search(r'^_Static_assert\(RC5_NODES <= UINT8_MAX, [^\n]+$',s,re.M)
assert indices and bound,'Actual bounded owner index declaration and capacity guard'
(out/'rc_path_role_under_test.h').write_text(indices[0]+'\n'+bound[0]+'\n'+'\n'.join(function(n) for n in ['parent_path_usable','make_publication_path','layout_path']))
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
sources=[app/'tests/rc_path_role_test.c',app/'src/rc_slots.c',app/'src/rc_boundary.c',app/'protocol/rc_dynamic.c',app/'protocol/group_auth.c',*[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]]
cmd=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),'-I'+str(tiny/'include'),*map(str,sources),'-o',str(out/'test')]
passed=False
try:
 with (out/'test.log').open('x') as log:
  subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=30);subprocess.run([str(out/'test')],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=10)
 passed=True
finally:(out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'production_functions':['parent_path_usable','make_publication_path','layout_path'],'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'command':cmd},indent=2)+'\n')
print((out/'test.log').read_text())
