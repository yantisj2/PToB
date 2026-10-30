#!/usr/bin/env python3
"""Offline RC9 codec/client checks with real group HMAC and independent vectors."""
import os
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import argparse,hashlib,hmac,json,struct,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--idf',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
crypto=a.idf.resolve()/'components/bt/host/nimble/nimble/ext/tinycrypt'
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'main'),'-I'+str(crypto/'include')]
cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').is_file() else 'clang');objects=[]
for i,src in enumerate([APP/'main/r2/r2-g1/group_auth.c',APP/'main/g1_hmac.c',crypto/'src/sha256.c',crypto/'src/utils.c']):
 obj=out/f'{i}.o';subprocess.run([cc,'-std=c11',*flags,'-c',src,'-o',obj],check=True);objects.append(obj)
subprocess.run([os.environ.get('CXX',cc+'++'),'-std=c++17',*flags,APP/'main/rc9/rc9.cpp',APP/'tests/rc9_client_test.cpp',*objects,'-o',out/'client'],check=True)
log=subprocess.check_output([out/'client'],text=True);(out/'client.log').write_text(log)
key=bytes([1,2,3])+bytes(29);context=bytes([1])+struct.pack('<QI',7,9)
for name,domain,size in [('capability',b'RC9CAP01',144),('response',b'RC9CTRL1',212),('time',b'RC9TIME1',190)]:
 wire=bytes.fromhex(next(line.split()[1] for line in log.splitlines() if line.startswith(name+' ')))
 assert len(wire)==size and wire[0]==size-1 and wire[1:8]==b'\xff\xff\xffR9\x01'+bytes([{'capability':1,'response':3,'time':4}[name]])
 assert hmac.new(key,domain+context+wire[:-16],hashlib.sha256).digest()[:16]==wire[-16:]
 if name=='time':
  expected=bytearray(174);expected[:8]=bytes([189,255,255,255,82,57,1,4])
  for offset,width,value in [(8,4,9),(12,8,11),(20,8,22),(28,4,1),(32,1,1),(36,8,5100000),(44,8,4900000),(52,8,16900000),(60,8,1700000000000000000),(68,8,55),(84,8,66),(108,4,1),(112,4,2),(116,1,1),(132,8,1699999999000000000),(140,8,1700000001000000000),(148,4,2**32-1),(153,1,2),(156,4,2**32-1),(160,4,10)]:expected[offset:offset+width]=value.to_bytes(width,'little')
  assert wire[:-16]==expected
paths=[APP/'main/rc9/rc9.hpp',APP/'main/rc9/rc9.cpp',APP/'main/rc9/client.hpp',APP/'tests/rc9_client_test.cpp',Path(__file__)]
(out/'receipt.json').write_text(json.dumps({'status':'PASS','hardware_access':False,'precision':'UNKNOWN','checks':['actual group HMAC and independent Python layout/HMAC','all byte tampering/truncation/reserved fields/domain separation','challenge/exact identity/boot/nonce/lease','original hold/no poll or renewal extension','withdrawal/replay/new physical pair recovery','pending renewal leaves current map unchanged'],'sources':{str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}},indent=2)+'\n')
print(log.splitlines()[-1])
