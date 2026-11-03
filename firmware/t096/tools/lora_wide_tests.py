#!/usr/bin/env python3
"""Matched SF11/BW500 schedule4: actual driver/core/codec, independent airtime/HMAC.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT. No hardware.
"""
import argparse,hashlib,hmac,json,math,os,re,struct,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def air(sf,bw,n):
 symbols=8+max(math.ceil((8*n-4*sf+28+16)/(4*sf))*6,0)
 return math.ceil((12+4.25+symbols)*(1<<sf)*1000000/bw)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 crypto=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(crypto/'include')]
 crypto_sources=[crypto/'source'/(n+'.c')for n in('hmac','sha256','utils')]
 sources=[APP/'src/lora_passive_core.c',APP/'protocol/lora_passive.c',APP/'protocol/group_auth.c',*crypto_sources]
 body=out/'driver_body.c';body.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_hw.c').read_text(),flags=re.M))
 duration=re.search(r'uint32_t lp_duration\(unsigned k\)\{.*?\n\}',(APP/'src/lora_passive_core.c').read_text(),re.S);size=re.search(r'size_t lp_size\(unsigned k\)\{.*?\n\}',(APP/'protocol/lora_passive.c').read_text(),re.S)
 budget=out/'budget.h';budget.write_text('#include "lora_passive_core.h"\n'+duration[0]+'\n'+size[0]+'\n')
 cases=0;vectors={};commands=[]
 for wide in(False,True):
  name='wide'if wide else'narrow';defs=['-DR2_LORA_PASSIVE_SLOW=1']+(['-DR2_LORA_PASSIVE_WIDE=1']if wide else[])
  def build(target,extra):
   command=[*flags,*defs,*map(str,extra),'-o',str(target)];commands.append(command);r=subprocess.run(command,capture_output=True,text=True);(out/(target.name+'-compile.log')).write_text(r.stdout+r.stderr);r.check_returncode()
  driver=out/(name+'-driver');build(driver,['-DR2_QA_PASSIVE_BUDGET=1','-include',budget,'-DR2_LORA_OWNER_BODY="'+str(body)+'"',APP/'tests/lora_hw_test.c'])
  for mode in [*range(201,283),*range(350,379)]:
   r=subprocess.run([str(driver),str(mode)],capture_output=True,text=True);(out/(name+'-driver-'+str(mode)+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();cases+=1
  codec=out/(name+'-codec');build(codec,[APP/'tests/lora_wide_codec_test.c',*sources]);r=subprocess.run([str(codec)],capture_output=True,text=True);r.check_returncode();vectors[name]=r.stdout; (out/(name+'-wire.txt')).write_text(r.stdout)
  lines=r.stdout.splitlines();schedule=4 if wide else 3
  immutable=b'L\x03\x02'+bytes([schedule])+struct.pack('<6Q3I',1,2,1,2,0,0,3,1,0x51a7c003)+bytes([1,0,0,0])
  mac=lambda domain,b:hmac.new(bytes([1,2,3])+bytes(29),domain+struct.pack('<BQI',1,9,1)+b,hashlib.sha256).digest()[:16]
  assert lines[0]==mac(b'PTL3CTX1',immutable).hex()
  for k,line in enumerate(lines[1:],1):
   wire=bytes.fromhex(line);assert wire[3]==schedule and wire[-16:]==mac([b'PTL3DIS1',b'PTL3MET1',b'PTL3SYN1',b'PTL3FOL1'][k-1],wire[:-16]if k==2 else bytes.fromhex(lines[0])+wire[:-16]);cases+=1
  core=out/(name+'-core');build(core,[APP/'tests/lora_enhanced_test.c',*sources]);r=subprocess.run([str(core),'1'],capture_output=True,text=True);(out/(name+'-48h.log')).write_text(r.stdout+r.stderr);r.check_returncode();cases+=1
 for name,other in(('narrow','wide'),('wide','narrow')):
  r=subprocess.run([str(out/(name+'-codec')),str(out/(other+'-wire.txt'))],capture_output=True,text=True);(out/(name+'-reject-other.log')).write_text(r.stdout+r.stderr);r.check_returncode();cases+=1
 assert vectors['narrow']!=vectors['wide']
 nominal=120*(air(11,500000,24)+10000+air(11,500000,32)+10000)+30*(air(11,500000,28)+10000)+12*(air(11,500000,112)+10000)
 assert air(11,500000,24)==222208 and air(11,500000,32)==246784 and air(11,500000,28)==246784 and air(11,500000,112)==615424 and nominal==73887648 and nominal<108000000
 report=dict(status='PASS_OFFLINE_MATCHED_WIDE_PHY_NOT_RF_QUALIFICATION',cases=cases,hardware=False,wire3_schedule=4,precision_PHY=dict(sf=11,bw_hz=500000,cr='4/6',preamble_symbols=12,explicit_header=True,CRC=True),airtime_us=dict(sync=222208,follow=246784,DISC=246784,META=615424),budget_us=dict(sync=232208,follow=256784,DISC=256784,META=625424),nominal_hour_reserved_us=nominal,cap_us=108000000,driver_owner_budget_equality='all4kinds bothprofiles',BW500_workaround='0x0889 bit2 clear for DTS+wide precision; set for narrow precision; unrelated bits retained',commands=commands,sources={str(f.relative_to(APP)):hashlib.sha256(f.read_bytes()).hexdigest()for f in[APP/'src/lora_hw.c',APP/'protocol/lora_passive_schedule.h',APP/'tests/lora_hw_test.c',APP/'tests/lora_enhanced_test.c',APP/'tests/lora_wide_codec_test.c',Path(__file__),*sources]if f.is_relative_to(APP)})
 (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',cases,'matched PHY/core/HMAC cases; nominal reserved us/hour',nominal)
if __name__=='__main__':main()
