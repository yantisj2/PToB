#!/usr/bin/env python3
"""Production passive C engine, real HMAC and independent Python wire oracle.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
No hardware or network access. All identities and credentials are synthetic.
"""
import argparse, hashlib, hmac, json, os, re, struct, subprocess
from bisect import bisect_right
from pathlib import Path
APP=Path(__file__).resolve().parents[1]

def occupied(intervals,window):
    spans=sorted(intervals);starts=[a for a,b in spans];ends=[b for a,b in spans];prefix=[0]
    for i,(start,end) in enumerate(spans):
        assert end>=start and (i==0 or start>=spans[i-1][1])
        prefix.append(prefix[-1]+end-start)
    def integral(t):
        i=bisect_right(ends,t)
        return prefix[i]+(max(0,t-starts[i]) if i<len(spans) else 0)
    candidates={0}
    for start,end in spans:candidates.update((start,end,start-window,end-window))
    return max(integral(t+window)-integral(t) for t in candidates)

def reference():
    key=bytes([1,2,3])+bytes(29)
    group=struct.pack('<BQI',1,9,1)
    def mac(domain,body):
        return hmac.new(key,domain+group+body,hashlib.sha256).digest()[:16]
    immutable=b'L\x03\x02\x01'+struct.pack('<6Q3I',1,2,1,2,0,0,3,1,0x51a7c003)
    context=mac(b'PTL3CTX1',immutable+bytes([1,0,0,0]))
    result={'context':context.hex()}
    for kind,size,domain in [(1,28,b'PTL3DIS1'),(2,112,b'PTL3MET1'),(3,24,b'PTL3SYN1'),(4,32,b'PTL3FOL1')]:
        body=bytearray(size-16);body[:4]=bytes([ord('L'),3,kind|(0xc0 if kind<3 else 0),1])
        if kind==2:
            body[4:64]=immutable[4:64]
            struct.pack_into('<IIQHhH',body,64,0,1,10**12,2,-300,85);body[86]=1
        elif kind==1:struct.pack_into('<IH',body,4,0,85)
        elif kind==4:struct.pack_into('<IQ',body,4,0,10**12)
        result['wire'+str(kind)]=(body+mac(domain,body if kind==2 else context+body)).hex()
    order=list(range(64));counter=0
    for i in range(63,0,-1):
        while True:
            draw=int.from_bytes(hashlib.sha256(b'PTOB-HOP-MODEL\0'+struct.pack('<II',0x51a7c003,counter)).digest()[:4],'little');counter+=1
            if draw<(2**32//(i+1))*(i+1):break
        j=draw%(i+1);order[i],order[j]=order[j],order[i]
    result['hop']=bytes(order).hex()
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=False)
    cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
    crypto=a.deps/'modules/crypto/tinycrypt/lib'
    flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(crypto/'include')]
    objects=[]
    for name in ['hmac.c','sha256.c','utils.c']:
        obj=a.out/(name+'.o');subprocess.run([*flags,'-Wno-unused-variable','-Wno-default-const-init-var-unsafe','-c',str(crypto/'source'/name),'-o',str(obj)],check=True);objects.append(str(obj))
    sources=[APP/'tests/lora_passive_test.c',APP/'src/lora_passive_core.c',APP/'protocol/lora_passive.c',APP/'protocol/group_auth.c']
    binary=a.out/'passive'
    subprocess.run([*flags,*map(str,sources),*objects,'-o',str(binary)],check=True)
    run=subprocess.run([str(binary)],text=True,capture_output=True)
    (a.out/'test.log').write_text(run.stdout+run.stderr)
    run.check_returncode()
    actual=dict(line.split(' ',1) for line in run.stdout.splitlines() if line.startswith(('context ','hop ','wire')))
    assert actual==reference(),(actual,reference())
    campaigns={}
    for line in run.stdout.splitlines():
        if line.startswith('RF '):
            phase,start,duration,ch=map(int,line.split()[1:])
            all_rf,channels=campaigns.setdefault(phase,([],{i:[] for i in range(64)}))
            all_rf.append((start,start+duration))
            if ch>=0:channels[ch].append((start,start+duration))
    hour=max(occupied(spans,3600_000_000) for spans,channels in campaigns.values())
    assert hour<=1080_000_000
    dwell=max(occupied(spans,20_000_000) for events,channels in campaigns.values() for spans in channels.values())
    assert dwell<=400_000
    body=a.out/'owner_body.c'
    body.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_hw.c').read_text(),flags=re.M))
    driver=a.out/'passive_driver'
    budget=a.out/'passive_budget.h'
    duration=re.search(r'uint32_t lp_duration\(unsigned k\)\{.*?\n\}',(APP/'src/lora_passive_core.c').read_text(),re.S)
    size=re.search(r'size_t lp_size\(unsigned k\)\{.*?\n\}',(APP/'protocol/lora_passive.c').read_text(),re.S)
    assert duration and size
    budget.write_text('#include "lora_passive_core.h"\n'+duration.group(0)+'\n'+size.group(0)+'\n')
    subprocess.run([*flags,'-DR2_LORA_PASSIVE_FAST=1','-DR2_QA_PASSIVE_BUDGET=1','-include',str(budget),'-DR2_LORA_OWNER_BODY="'+str(body.resolve())+'"',str(APP/'tests/lora_hw_test.c'),'-o',str(driver)],check=True)
    for mode in range(201,283):subprocess.run([str(driver),str(mode)],check=True)
    for mode in range(350,379):subprocess.run([str(driver),str(mode)],check=True)
    receipt=dict(status='PASS_PRODUCTION_C_FAKE_BOUNDARIES',hardware='NOT_TESTED',deployment_authorized=False,wire_oracle=True,driver_cases=111,offered_hours_per_campaign=2,phases_us=sorted(campaigns),admitted_frames_per_campaign={str(k):len(v[0]) for k,v in campaigns.items()},worst_rolling_hour_us=hour,worst_channel_20s_us=dwell,sources={str(s.relative_to(APP)):hashlib.sha256(s.read_bytes()).hexdigest() for s in [*sources,APP/'src/lora_hw.c',APP/'include/lora_hw.h',APP/'include/lora_watchdog.h',APP/'include/lora_passive_core.h',APP/'protocol/lora_passive.h',APP/'tests/lora_hw_test.c',Path(__file__)]})
    receipt.update(driver_cases=111,staged_driver_cases=29,stop_race_cases=18)
    (a.out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('\n'.join(line for line in run.stdout.splitlines() if not line.startswith(('RF ','wire','context ','hop '))))
    print(f'PASS independent wire/hop/occupancy oracles;111 driver cases;hour={hour}us;channel20s={dwell}us')
if __name__=='__main__':main()
