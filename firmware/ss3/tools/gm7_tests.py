#!/usr/bin/env python3
"""SS3 GM7 monitor/epoch regressions with real codec/HMAC; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,os,subprocess,hashlib
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 crypto=a.sdk.resolve()/'components/bt/host/nimble/nimble/ext/tinycrypt';src=APP/'main/r2/r2-g1';cc=os.environ.get('CC','clang');cxx=os.environ.get('CXX','clang++')
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'main'),'-I'+str(src),'-I'+str(crypto/'include')]
 sources=[src/'group_auth.c',src/'role_announcement.c',src/'gnss_diagnostics.c',APP/'main/gm7/gm7_codec.c',APP/'main/g1_hmac.c',crypto/'src/sha256.c',crypto/'src/utils.c'];objects=[]
 with (out/'test.log').open('x')as log:
  for source in sources:
   obj=out/(source.stem+'.o');objects.append(obj);subprocess.run([cc,'-std=c11',*flags,'-c',str(source),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([cxx,'-std=c++20',*flags,str(APP/'tests/gm7_wire_test.cpp'),str(APP/'main/gm7_wire.cpp'),str(APP/'main/td_mesh.cpp'),*map(str,objects),'-o',str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','hardware_access':False,'sources':{str(q):hashlib.sha256(q.read_bytes()).hexdigest()for q in sources}},indent=2)+'\n');print('PASS SS3 GM7 real wire/authentication/epoch date and elapsed guards')
if __name__=='__main__':main()
