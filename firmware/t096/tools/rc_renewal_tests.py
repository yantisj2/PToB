#!/usr/bin/env python3
"""Actual RC7 binding/codec renewal regressions under ASan/UBSan; offline only.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--legacy-bind',action='store_true',help='Expected-failure control omitting pre-reset admission validation');p.add_argument('--legacy-gm-renewal',action='store_true',help='Expected-failure control restoring300s GM renewal');a=p.parse_args()
if a.legacy_bind and a.legacy_gm_renewal:p.error('Select one negative control')
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(exist_ok=False)
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
sources=[app/'tests/rc_renewal_test.c',app/'src/rc_boundary.c',app/'protocol/rc_dynamic.c',app/'protocol/group_auth.c',*[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]]
policy=app/'include/rc_admission_policy.h';extra_includes=[]
if a.legacy_gm_renewal:
 old=policy.read_text();needle='{return UINT64_C(3840000000);}'
 if old.count(needle)!=1:raise ValueError('Expected exact GM renewal policy')
 folder=out/'legacy-include';folder.mkdir();policy=folder/policy.name
 policy.write_text(old.replace(needle,'{return UINT64_C(4800000000);}'))
 extra_includes=['-I'+str(folder)]
if a.legacy_bind:
 old=(app/'protocol/rc_dynamic.c').read_text().replace('!s || !rc5_admission_valid(a) || !self','!s || !a || !self')
 (out/'legacy_rc_dynamic.c').write_text(old);sources[2]=out/'legacy_rc_dynamic.c'
cmd=[os.environ.get('CC') or shutil.which('clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all',*extra_includes,'-I'+str(app/'include'),'-I'+str(app/'protocol'),'-I'+str(tiny/'include'),*map(str,sources),'-o',str(out/'test')]
passed=False
try:
 with (out/'test.log').open('x') as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'legacy_bind':a.legacy_bind,'legacy_gm_renewal':a.legacy_gm_renewal,'command':cmd,'sources':{str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in [*sources,policy]}},indent=2)+'\n')
print('PASS RC7 actual renewal tests')
