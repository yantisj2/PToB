#!/usr/bin/env python3
"""Actual SS3 diagnostic worker and signed codec, offline synthetic inputs only.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,os,subprocess,hashlib
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 native=(APP/'main/native_g1.cpp').read_text();start=native.index('void td_role_poll(uint64_t now) {');brace=native.index('{',start);end=brace+1;depth=1
 while depth:depth+=(native[end]=='{')-(native[end]=='}');end+=1
 (out/'td_role_poll_under_test.hpp').write_text(native[start:end])
 crypto=a.sdk.resolve()/'components/bt/host/nimble/nimble/ext/tinycrypt';src=APP/'main/r2/r2-g1'
 cc=os.environ.get('CC','clang');cxx=os.environ.get('CXX','clang++')
 flags=['-DR2_TZ_WIRE=1','-DPTOB_SS3_SERVO=1','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'main'),'-I'+str(src),'-I'+str(crypto/'include'),'-I'+str(out)]
 sources=[src/'group_auth.c',src/'role_announcement.c',APP/'main/g1_hmac.c',crypto/'src/sha256.c',crypto/'src/utils.c'];objects=[]
 with (out/'test.log').open('x') as log:
  for source in sources:
   obj=out/(source.stem+'.o');objects.append(obj);subprocess.run([cc,'-std=c11',*flags,'-c',str(source),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([cxx,'-std=c++20',*flags,'-Wno-misleading-indentation',str(APP/'tests/td_role_native_test.cpp'),*map(str,objects),'-o',str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','hardware':False,'native_sha256':hashlib.sha256(native.encode()).hexdigest(),'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in sources}},indent=2)+'\n')
 print('PASS actual SS3 event worker and authenticated codec guards')
if __name__=='__main__':main()
