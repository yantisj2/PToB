#!/usr/bin/env python3
"""Exercise the actual pinned receiver backport with public native RC9 fakes.
No hardware. Compile the normal G1 host tests first to obtain codec objects.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,os,shutil,subprocess,sys
from pathlib import Path
from live_build import APP,REPO,RECOVERY_POLICY,RECOVERY_REF,freeze_shared,sha
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
r=json.loads((out/'native/receipt.json').read_text())
assert r['status']=='PASS'
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',scope='PINNED_SHARED_RECEIVER_BACKPORT_AND_CORESE_OVERLAY_ANCHORS',policy=RECOVERY_POLICY,recovery_commit=RECOVERY_REF,patch_sha256=sha(APP/'tools/auto_rc9_recovery.patch'),production_sources=sources,cases=r['cases'],hardware_access=False),indent=2)+'\n')
print('PASS CoreSE pinned receiver recovery; actual RF handover remains a separate hardware check')
