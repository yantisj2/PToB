#!/usr/bin/env python3
"""Offline N30/T096 passive wire3/schedule8 interoperability with real W12 HMAC and synthetic events.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,importlib.util,json,os,subprocess
from decimal import Decimal,localcontext
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1];T096=REPO/'firmware/t096'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--idf',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 sdk=a.idf.resolve();crypto=sdk/'components/bt/host/nimble/nimble/ext/tinycrypt';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_FIXED500_EXPERIMENT=1','-DR2_LORA_FIXED500_EVENT_DELAY_NS=99328000','-I'+str(APP/'main'),'-I'+str(T096/'include'),'-I'+str(T096/'protocol'),'-I'+str(crypto/'include')]
 sources=[T096/'src/lora_passive_core.c',T096/'protocol/lora_passive.c',REPO/'firmware/w12/main/r2/r2-g1/group_auth.c',REPO/'firmware/w12/main/g1_hmac.c',crypto/'src/sha256.c',crypto/'src/utils.c',APP/'main/lora_core.c',APP/'tests/lora_packets.c'];objects=[]
 for i,src in enumerate(sources):
  obj=out/f'{i}.o';subprocess.run([*flags,*(['-Wno-default-const-init-var-unsafe'] if src.name=='g1_hmac.c' else []),'-c',src,'-o',obj],check=True,timeout=30);objects.append(obj)
 exe=out/'n30-core';subprocess.run([*flags,APP/'tests/lora_core_test.c',*objects,'-o',exe],check=True,timeout=40);subprocess.run([exe],check=True,timeout=30)
 cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++');owner=out/'n30-owner-interop'
 owner_flags=[cxx,*[x for x in flags[1:] if x!='-std=c11'],'-std=c++20','-DPTOB_N30_TEST_REAL_RC8=1','-I'+str(APP/'tests/lora_fakes'),'-I'+str(REPO/'firmware/w12/main'),'-I'+str(REPO/'firmware/w12/legacy')]
 subprocess.run([*owner_flags,APP/'tests/lora_owner_test.cpp',APP/'main/ui_model.cpp',APP/'main/ui_render.cpp',*objects,'-o',owner],check=True,timeout=40)
 with (out/'n30-owner-interop.log').open('w') as f:subprocess.run([owner],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
 report={'status':'PASS','hardware_access':False,'precision':'UNQUALIFIED','calibration':'ENGINEERING_SYMBOL_MODEL_ENDPOINT_UNKNOWN','checks':['actual shared passive wire3/schedule8 codec/core and W12 HMAC','old3/5/7 namespace rejection and fresh META cursor floor without physical-pair invention','physical SYNC/FOLLOW, original expiry, replay/tamper and OFF fences','actual SX1262 owner HeaderValid latch preserved across RxDone; CRC and noTX; sixminute counter wraps','continuous passive RX and trained delivered map'],'sources':{str(x.relative_to(REPO)) if x.is_relative_to(REPO) else str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in sources+[APP/'main/lora_mapping.hpp',APP/'main/native_lora.cpp',APP/'tests/lora_owner_interop.inc',APP/'tests/lora_core_test.c']}}
 (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS N30 passive schedule8 core, HMAC and real-owner synthetic capture interop')
if __name__=='__main__':main()
