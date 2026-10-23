#!/usr/bin/env python3
"""Offline fine-coordinate lifetime guard against actual G1/source codecs.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--idf',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 w12=APP.parent/'w12';src=w12/'main/r2/r2-g1';crypto=a.idf/'components/bt/host/nimble/nimble/ext/tinycrypt'
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all']
 for x in (APP/'main',w12/'main',src,w12/'main/r2/rc7/include',w12/'legacy',w12/'legacy/reference',crypto/'include'):flags+=['-I'+str(x)]
 cxx=os.environ.get('CXX','clang++');cc=os.environ.get('CC','clang');objects=[]
 files=[src/(n+'.c') for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec')]+[w12/'main/r2/rc7/protocol/rc_dynamic.c',w12/'main/r2/rc7/src/rc_boundary.c',w12/'main/r2/rc7/src/rc_slots.c',w12/'main/g1_hmac.c']+[crypto/'src'/n for n in ('sha256.c','utils.c')]
 for file in files:
  obj=out/(file.stem+'.o')
  with (out/(file.stem+'-compile.log')).open('w') as log:subprocess.run([cc,'-std=c11',*flags,'-c',file,'-o',obj],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  objects.append(obj)
 test=APP/'tests/fine_permission_test.cpp';exe=out/'permission'
 with (out/'compile.log').open('w') as log:subprocess.run([cxx,'-std=c++20','-DPTOB_N30=1',*flags,test,*objects,'-o',exe],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 with (out/'run.log').open('w') as log:subprocess.run([exe],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
 files.extend([Path(__file__),test,APP/'main/fine_receiver.hpp',w12/'main/g1_corrected_clock.hpp'])
 (out/'receipt.json').write_text(json.dumps(dict(status='PASS',scope='ACTUAL_G1_FINE_TRIANGLE_ENCLOSURE_SOURCE_EXPIRY_ASAN_UBSAN',hardware_access=False,sources={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}),indent=2)+'\n')
 print((out/'run.log').read_text(),end='')
if __name__=='__main__':main()
