#!/usr/bin/env python3
"""Exercise the actual pinned receiver backport with public native RC9 fakes.
No hardware. Compile the normal G1 host tests first to obtain codec objects.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,os,shutil,subprocess,sys
from pathlib import Path
from live_build import APP,REPO,RECOVERY_POLICY,RECOVERY_REF,LEADER_POLICY,LEADER_REF,freeze_shared,sha
from prepare_runtime import prepare
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--objects',type=Path,required=True);a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);root=out/'source';sources=freeze_shared(root)
prepare(out/'runtime',root) # Also require every target-local overlay anchor.
# Test harnesses are current public sources; production receiver files retain
# exactly the pinned baseline plus the two-file recovery patch.
for name in ('tests','tools'):
 shutil.copytree(REPO/'firmware/w12'/name,root/'firmware/w12'/name)
command=[sys.executable,str(root/'firmware/w12/tools/rc9_native_tests.py'),'--out',str(out/'native'),'--objects',str(a.objects.resolve())]
subprocess.run(command,check=True,env=os.environ.copy())
# Compile the actual pinned production native receiver with the current
# source-loss/new-leader tests; no calibrated W12 or N30 clock is imported.
leader=out/'leader';leader.mkdir()
(leader/'ptob_device.h').write_text('#define PTOB_FACTORY_MAC {2,0,0,0,0,5}\n#define PTOB_DEVICE_LABEL "C-1"\n#define PTOB_LEGACY_WIRE_ID 3\n')
(leader/'r2_bench_enrollment.h').write_text('static const r2_group_enrollment provision={{{'+','.join(map(str,range(32)))+'},UINT64_C(0x123456789abcdef0),1},UINT64_C(0x020000000005),"C-1",40000};\n')
s=(REPO/'firmware/w12/tests/native_g1_test.cpp').read_text().replace('#include "../main/native_g1.cpp"','#include "'+str(root/'firmware/w12/main/native_g1.cpp')+'"')
(leader/'native.cpp').write_text(s)
flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DV12_R2_ENABLED=1','-DV12_R2_FIELD_CLIENT=1','-DV12_R2_CONFIG=13','-DV12_R2_G1_LISTENING_PERCENT=100','-DV12_UNIT="C1"']
for path in [leader,root/'firmware/w12/main',root/'firmware/w12/legacy/reference',REPO/'firmware/w12/tests/r2_fakes',REPO/'firmware/w12/tests/console_fakes',root/'firmware/w12/main/r2/r2-g1',root/'firmware/w12/main/r2/rc7/include',Path(os.environ['IDF_PATH'])/'components/bt/host/nimble/nimble/ext/tinycrypt/include']:flags+=['-I'+str(path)]
objects=[a.objects.resolve()/(n+'.o')for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec','rc_dynamic','rc_boundary','rc_slots','g1_hmac','sha256','utils')]
with (leader/'compile.log').open('w')as f:subprocess.run([os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++'),*flags,leader/'native.cpp',*objects,'-o',leader/'native'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
leader_cases=['leader-loss','leader-loss-boundary','leader-loss-pin','leader-loss-switch','no-source-held','foreign-root-held','warm-control-PA']
for case in leader_cases:
 with (leader/(case+'.log')).open('w')as f:subprocess.run([leader/'native',case],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=15)
r=json.loads((out/'native/receipt.json').read_text())
assert r['status']=='PASS'
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',scope='PINNED_SHARED_RECEIVER_BACKPORT_AND_CORESE_OVERLAY_ANCHORS',policy=RECOVERY_POLICY,recovery_commit=RECOVERY_REF,leader_policy=LEADER_POLICY,leader_commit=LEADER_REF,leader_patch_sha256=sha(APP/'tools/leader_recovery.patch'),leader_cases=leader_cases,patch_sha256=sha(APP/'tools/auto_rc9_recovery.patch'),production_sources=sources,cases=r['cases'],hardware_access=False),indent=2)+'\n')
print('PASS CoreSE pinned receiver recovery; actual RF handover remains a separate hardware check')
