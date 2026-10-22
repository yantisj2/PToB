#!/usr/bin/env python3
"""Offline N30/T096 wire3 interoperability with real W12 HMAC and synthetic events.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,importlib.util,json,os,subprocess
from decimal import Decimal,localcontext
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1];T096=REPO/'firmware/t096'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--idf',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 sdk=a.idf.resolve();crypto=sdk/'components/bt/host/nimble/nimble/ext/tinycrypt';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'main'),'-I'+str(T096/'include'),'-I'+str(T096/'protocol'),'-I'+str(crypto/'include')]
 sources=[T096/'src/lora_rc8_policy.c',T096/'protocol/lora_rc8.c',REPO/'firmware/w12/main/r2/r2-g1/group_auth.c',REPO/'firmware/w12/main/g1_hmac.c',crypto/'src/sha256.c',crypto/'src/utils.c'];objects=[]
 for i,src in enumerate(sources):
  obj=out/f'{i}.o';subprocess.run([*flags,*(['-Wno-default-const-init-var-unsafe'] if src.name=='g1_hmac.c' else []),'-c',src,'-o',obj],check=True,timeout=30);objects.append(obj)
 def run(name,tests):
  exe=out/name;subprocess.run([*flags,*tests,*objects,'-o',exe],check=True,timeout=40);r=subprocess.run([exe],check=True,text=True,capture_output=True,timeout=30);(out/(name+'.log')).write_text(r.stdout+r.stderr);return exe,r.stdout
 codec,codec_log=run('wire3',[T096/'tests/lora_rc8_test.c'])
 _,fit=run('fit',[T096/'tests/lora_link_fit_test.c'])
 run('n30-bridge',[APP/'tests/lora_core_test.c',APP/'main/lora_core.c'])
 bridge=out/'n30-core.o';subprocess.run([*flags,'-c',APP/'main/lora_core.c','-o',bridge],check=True,timeout=30)
 owner=out/'n30-owner-interop';cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++')
 owner_flags=[cxx,*[x for x in flags[1:] if x!='-std=c11'],'-std=c++20','-DPTOB_N30_TEST_REAL_RC8=1',
  '-I'+str(APP/'tests/lora_fakes'),'-I'+str(REPO/'firmware/w12/main'),'-I'+str(REPO/'firmware/w12/legacy')]
 subprocess.run([*owner_flags,APP/'tests/lora_owner_test.cpp',APP/'main/ui_model.cpp',APP/'main/ui_render.cpp',bridge,*objects,'-o',owner],check=True,timeout=40)
 with (out/'n30-owner-interop.log').open('w') as f:subprocess.run([owner],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)

 spec=importlib.util.spec_from_file_location('t096_rc8_reference',T096/'tools/lora_rc8_tests.py');ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
 vector=json.loads((REPO/'protocol/rc8/vectors-wire3.json').read_text());expected=ref.reference(vector);assert expected==vector['wire_hex']
 for name,value in expected.items():assert subprocess.check_output([codec,name],text=True).strip()==value
 blocks={};checked=0
 for line in fit.splitlines():
  if not line.startswith('FIT '):continue
  scenario,index,tick,ns,n,rms=map(int,line.split()[1:]);points=blocks.setdefault((scenario,index//16),[]);points.append((tick,ns));assert n==len(points)
  if n<4:assert rms==0;continue
  with localcontext() as ctx:
   ctx.prec=60;t0,u0=points[0];xy=[(Decimal((t-t0)//1600),Decimal(u-u0-(t-t0)*125//2)) for t,u in points];mx=sum(x for x,y in xy)/n;my=sum(y for x,y in xy)/n;slope=sum((x-mx)*(y-my) for x,y in xy)/sum((x-mx)**2 for x,y in xy);oracle=(sum((y-my-slope*(x-mx))**2 for x,y in xy)/n).sqrt();assert abs(Decimal(rms)-oracle)<2
  checked+=1
 assert checked==78
 extra=[APP/'main/lora_core.c',APP/'main/lora_core.h',APP/'tests/lora_core_test.c',APP/'main/native_lora.cpp',APP/'tests/lora_owner_test.cpp',APP/'tests/lora_owner_interop.inc',T096/'tests/lora_rc8_test.c',T096/'tests/lora_link_fit_test.c',T096/'protocol/lora_rc8.h',T096/'include/lora_rc8_policy.h',T096/'include/precision_servo.h']
 report={'status':'PASS','hardware_access':False,'precision':'UNQUALIFIED','checks':['actualT096wire3codec/policy with actualW12groupHMAC','independentPython/Cwire3vectors','78independentDecimalOLSresiduals','actualN30SX1262owner+realRC8lateAUTO/6mincapturewrap/CRC/lossrecovery/LORpreference','N30bridgecoldacquisition/metadata/physicalpair/replay/tamper/expiry/OFF/selfbinding'],'sources':{str(x.relative_to(REPO)) if x.is_relative_to(REPO) else str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in sources+extra},'codec_output':codec_log.strip()}
 (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS actualN30/T096RC8interop, W12HMAC,78Decimalfit estimates, bridge original expiry/fences')
if __name__=='__main__':main()
