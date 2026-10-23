#!/usr/bin/env python3
"""W12 real RC8 codec/HMAC, fallback policy and owner boundary checks; no USB."""
import argparse,hashlib,json,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--idf',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
c=a.idf.resolve()/'components/bt/host/nimble/nimble/ext/tinycrypt'
flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'main'),'-I'+str(APP/'main/rc8'),'-I'+str(c/'include')]
cc='/opt/homebrew/opt/llvm/bin/clang';cxx=cc+'++';objects=[]
for i,src in enumerate([APP/'main/rc8/lora_rc8.c',APP/'main/rc8/lora_rc8_policy.c',APP/'main/r2/r2-g1/group_auth.c',APP/'main/g1_hmac.c',c/'src/sha256.c',c/'src/utils.c']):
 obj=out/f'{i}.o';subprocess.run([cc,'-std=c11',*flags,'-c',src,'-o',obj],check=True);objects.append(obj)
subprocess.run([cc,'-std=c11',*flags,APP/'tests/lora_core_test.c',APP/'main/lora_core.c',*objects,'-o',out/'core'],check=True)
subprocess.run([out/'core'],check=True)
subprocess.run([cxx,'-std=c++20',*flags,APP/'tests/lora_policy_test.cpp','-o',out/'policy'],check=True);subprocess.run([out/'policy'],check=True)
subprocess.run([cxx,'-std=c++20',*flags,'-Wno-misleading-indentation','-I'+str(APP/'tests/lora_fakes'),APP/'tests/lora_owner_test.cpp','-o',out/'owner'],check=True)
for case in ['normal','dma','cancel','tx','capture']:subprocess.run([out/'owner',case],check=True)
paths=[APP/'main/w12_lora_radio.cpp',APP/'main/lora_policy.hpp',APP/'main/lora_core.c',APP/'tests/lora_owner_test.cpp',APP/'tests/lora_core_test.c',APP/'tests/lora_policy_test.cpp',Path(__file__)]
(out/'receipt.json').write_text(json.dumps({'status':'PASS','hardware_access':False,'scope':'REAL_RC8_AUTH_REPLAY_EXPIRY_AUTO180_OWNER_DMA_CANCEL_TX_BAN_ASAN_UBSAN','sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}},indent=2)+'\n')
