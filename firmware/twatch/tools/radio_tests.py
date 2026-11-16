#!/usr/bin/env python3
"""Watch mobile timing/LoRa regressions using real authenticated codec and core.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--idf',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
crypto=a.idf.resolve()/'components/bt/host/nimble/nimble/ext/tinycrypt'
cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_FIXED500_EXPERIMENT=1','-DR2_LORA_FIXED500_EVENT_DELAY_NS=99328000',*['-I'+str(REPO/q) for q in ['firmware/twatch/main','firmware/n30/main','firmware/t096/include','firmware/t096/protocol']],'-I'+str(crypto/'include')]
sources=[REPO/q for q in ['firmware/t096/src/lora_passive_core.c','firmware/t096/protocol/lora_passive.c','firmware/w12/main/r2/r2-g1/group_auth.c','firmware/w12/main/g1_hmac.c','firmware/twatch/main/lora_core.c','firmware/n30/main/lora_thermal.c','firmware/n30/tests/lora_packets.c']]+[crypto/'src/sha256.c',crypto/'src/utils.c'];objects=[]
for i,src in enumerate(sources):
 obj=out/f'{i}.o';subprocess.run([*flags,*(['-Wno-default-const-init-var-unsafe'] if src.name=='g1_hmac.c' else []),'-c',src,'-o',obj],check=True,timeout=30);objects.append(obj)
cases=['firmware/twatch/tests/lora_mobile_test.c','firmware/twatch/tests/lora_dynamic_test.c','firmware/n30/tests/lora_watch_test.c','firmware/n30/tests/lora_core_test.c','firmware/n30/tests/lora_hold_test.c']
# The ordinary N30 core keeps its pinned receiver/API. Its strict fixture is
# still tested against that implementation, including its original cursor
# and legacy self-hint expectations; dynamic watch policy has its own cases.
n30_core=REPO/'firmware/n30/main/lora_core.c';n30_obj=out/'n30-core.o'
subprocess.run([*flags,'-c',n30_core,'-o',n30_obj],check=True,timeout=30)
for case in cases:
 linked=[n30_obj if src==APP/'main/lora_core.c' else obj for src,obj in zip(sources,objects)] if case in ('firmware/n30/tests/lora_core_test.c','firmware/n30/tests/lora_hold_test.c') else objects
 exe=out/Path(case).stem;subprocess.run([*flags,REPO/case,*linked,'-o',exe],check=True,timeout=40);subprocess.run([exe],check=True,timeout=30)
# Current N30 fixtures use the same per-emitter consumed-cursor policy.
# Run the complete strict and HOLD cases against the watch implementation too.
for name in ('lora_core_test.c','lora_hold_test.c'):
 exe=out/('watch-'+name.removesuffix('.c'))
 subprocess.run([*flags,REPO/'firmware/n30/tests'/name,*objects,'-o',exe],check=True,timeout=40)
 subprocess.run([exe],check=True,timeout=30)
sources += [REPO/q for q in cases]+[n30_core,REPO/'firmware/n30/main/lora_watch.inc',APP/'main/lora_watch.inc',APP/'main/lora_shadow_input.h',Path(__file__)]
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',scope='REAL_CODEC_CORE_SYNTHETIC_RF_NOT_HARDWARE',sources={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in sources}),indent=2)+'\n')
