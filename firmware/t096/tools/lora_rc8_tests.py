#!/usr/bin/env python3
"""RC8 production codec/policy checks with fake boundaries; never opens hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import hashlib
import hmac
import json
import math
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
from decimal import Decimal,localcontext

APP=Path(__file__).resolve().parents[1]
REPO=APP.parents[1]

def airtime(payload):
    # Explicit header, CRC, SF10/BW500/CR4/6, DE=0, preamble12.
    symbols=8+max(math.ceil((8*payload-4*10+28+16)/(4*10))*6,0)
    return int((12+4.25+symbols)*(2**10)*1_000_000/500_000)

def reference(vector):
    group=vector['group'];scope=vector['scope'];c=vector['correction']
    auth=struct.pack('<BQI',1,int(group['id_hex'],16),group['epoch'])
    key=bytes.fromhex(group['synthetic_psk_hex'])
    meta=bytearray(112);meta[:4]=b'R8\x03\x01'
    for offset,name,fmt in [(4,'emitter','Q'),(12,'emitter_boot','Q'),(20,'stream','Q'),(28,'root','Q'),(36,'root_boot','Q'),(44,'parent','Q'),(52,'parent_boot','Q'),(60,'source_epoch','I')]:
        struct.pack_into('<'+fmt,meta,offset,scope.get(name,0))
    meta[64]=scope['scale'];meta[65]=scope['kind']
    context=hmac.new(key,b'PTOBR8C3'+auth+meta[:66],hashlib.sha256).digest()[:16]
    d=vector['diagnostic'];q=vector['quality']
    struct.pack_into('<IIIHHHBBBBIIIiIBB',meta,66,d['sequence'],d['snapshot_age_ms'],d['timing_sequence'],d['events'],q['known'],q['values'],q['role'],q['hop'],q['parent_class'],q['parent_accuracy'],q['parent_bound_ns'],q['correction_age_ms'],q['map_life_ms'],q['rate_ppb'],q['residual_ns'],q['losses'],q['recoveries'])
    meta[111]=d['time_valid']
    tag=hmac.new(key,b'PTOBR8D3'+auth+meta,hashlib.sha256).digest()[:16]
    timing=bytearray(64);timing[:4]=b'R8\x03'+bytes([2|(scope['scale']<<2)|(scope['kind']<<3)]);timing[4:20]=context
    struct.pack_into('<QIIIQI',timing,20,scope['root'],scope['source_epoch'],2,c['sequence'],c['event_ns'],c['map_generation'])
    struct.pack_into('<III',timing,52,(c['event_ns']-c['created_ns'])//1000,(c['expires_ns']-c['event_ns'])//1000,c['map_age_us'])
    timing_tag=hmac.new(key,b'PTOBR8T3'+auth+timing,hashlib.sha256).digest()[:16]
    return {'metadata':(meta+tag).hex(),'timing':(timing+timing_tag).hex()}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--arm-reference',type=Path,help='Existing native build supplying pinned CortexM4 flags; outputs remain untouched')
    args=p.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    compiler=os.environ.get('CC') or ('/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').is_file() else shutil.which('clang'))
    flags=[compiler,'-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
           '-fsanitize=address,undefined','-fno-sanitize-recover=all','-g',
           '-I'+str(APP/'include'),'-I'+str(APP/'protocol'),
           '-I'+str(args.deps/'modules/crypto/tinycrypt/lib/include')]
    sources=[APP/'tests/lora_rc8_test.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c']
    crypto=[args.deps/'modules/crypto/tinycrypt/lib/source'/name for name in ['hmac.c','sha256.c','utils.c']]
    # Pinned TinyCrypt hmac.c contains its existing dummy_key declaration.
    # Keep that upstream-only warning suppression off the production sources.
    objects=[]
    for source in crypto:
        obj=out/(source.stem+'.o');objects.append(obj)
        subprocess.run([*flags,'-Wno-default-const-init-var-unsafe','-c',str(source),'-o',str(obj)],check=True,timeout=30)
    subprocess.run([*flags,*map(str,sources+objects),'-o',str(out/'rc8')],check=True,timeout=60)
    result=subprocess.run([str(out/'rc8')],check=True,timeout=30,text=True,capture_output=True)
    print(result.stdout,end='')
    fit_sources=[APP/'tests/lora_link_fit_test.c',*sources[1:],*objects]
    subprocess.run([*flags,*map(str,fit_sources),'-o',str(out/'link-fit')],check=True,timeout=60)
    fit=subprocess.check_output([str(out/'link-fit')],text=True,timeout=30)
    (out/'link-fit.log').write_text(fit)
    blocks={};checked=0
    for line in fit.splitlines():
        if not line.startswith('FIT '):continue
        scenario,index,tick,ns,n,rms=map(int,line.split()[1:]);key=(scenario,index//16)
        points=blocks.setdefault(key,[]);points.append((tick,ns));assert n==len(points)
        if n<4:assert rms==0;continue
        # Independent high-precision OLS: construct every centered residual,
        # rather than copying the firmware sufficient-stat SSE expression.
        with localcontext() as ctx:
            ctx.prec=60;t0,u0=points[0]
            xy=[(Decimal((t-t0)//1600),Decimal(u-u0-(t-t0)*125//2))for t,u in points]
            mx=sum(x for x,y in xy)/n;my=sum(y for x,y in xy)/n
            slope=sum((x-mx)*(y-my)for x,y in xy)/sum((x-mx)**2 for x,y in xy)
            expected=(sum((y-my-slope*(x-mx))**2 for x,y in xy)/n).sqrt()
            assert abs(Decimal(rms)-expected)<2,(scenario,index,rms,str(expected))
        checked+=1
    assert checked==78 and len(blocks)==6
    print('Independent Decimal OLS residual oracle PASS78 estimates,6 blocks; synthetic events only')
    sizes={key:int(size) for key,size in re.findall(r'(receiver|export|packet|transmission|stop|gesture)=(\d+)',result.stdout)}
    contract=json.loads((REPO/'protocol/rc8/contract-wire3.json').read_text())
    budget=contract['memory']['object_max_bytes']
    assert max(sizes['receiver'],sizes['transmission']+sizes['export'])<=budget['exclusive_boot_role_union']
    assert contract['memory']['ceiling_added_static_bytes']==4096
    # The historical planning reserves overlap integrated storage; only a
    # complete native ELF comparison can establish the actual static ceiling.
    vector=json.loads((REPO/'protocol/rc8/vectors-wire3.json').read_text())
    expected=reference(vector)
    assert expected==vector['wire_hex']
    for name,value in expected.items():
        actual=subprocess.check_output([str(out/'rc8'),name],text=True,timeout=30).strip()
        assert actual==value,'C '+name+' codec differs from independent Python HMAC/layout vector'
    assert airtime(64)==209408 and airtime(128)==369152 and airtime(80)==258560 and airtime(176)==492032
    native={}
    if args.arm_reference:
        import shlex
        commands=json.loads((args.arm_reference/'target/compile_commands.json').read_text())
        unit=next(x for x in commands if x['file'].endswith('/protocol/group_auth.c'))
        frames={}
        for source in sources[1:]+crypto:
            command=shlex.split(unit['command']);name=source.stem;obj=out/(name+'.native.o')
            for flag,value in [('-o',str(obj)),('-MF',str(obj)+'.d'),('-MT',str(obj)),('-c',str(source.resolve()))]:
                if flag in command:command[command.index(flag)+1]=value
            subprocess.run(command,check=True,timeout=60,cwd=unit['directory'])
            for line in (out/(name+'.native.su')).read_text().splitlines():
                location,size,kind=line.split('\t');frames[location.rsplit(':',1)[-1]]=int(size)
        paths={
            'rx_metadata_key_setup':['rc8_receiver_process','metadata','rc8_companion_decode','r2_group_verify','r2_group_mac','tc_hmac_set_key','tc_sha256_final','compress'],
            'rx_metadata_context_key_setup':['rc8_receiver_process','rc8_companion_decode','companion_context','r2_group_mac','tc_hmac_set_key','tc_sha256_final','compress'],
            'rx_timing_key_setup':['rc8_receiver_process','rc8_decode','r2_group_verify','r2_group_mac','tc_hmac_set_key','tc_sha256_final','compress'],
            'tx_context_key_setup':['rc8_tx_prepare','rc8_context_id','companion_context','r2_group_mac','tc_hmac_set_key','tc_sha256_final','compress'],
            'tx_context_final':['rc8_tx_prepare','rc8_context_id','companion_context','r2_group_mac','tc_hmac_final','tc_sha256_final','compress']}
        native={'compiler_frames_bytes':frames,'nested_known_frames_bytes':{name:sum(frames.get(fn,0) for fn in chain) for name,chain in paths.items()},
                'paths':paths,'limits':'Known C frames only; inlined helpers contribute within caller; excludes worker frame, IRQ/kernel/libc/libgcc/SPI/capture costs and physical high-water. Not stack qualification.'}
    report={'status':'PASS_RC8_WIRE3_OFFLINE','hardware':'NOT_ACCESSED','precision':'UNQUALIFIED','link_fit_decimal_oracle_estimates':checked,
            'tests':result.stdout.strip(),'metadata_bytes':128,'timing_bytes':80,'metadata_airtime_us':airtime(128),'timing_airtime_us':airtime(80),
            'host_abi_sizes':sizes,'occupancy_timing4s_companion120s_percent':airtime(80)/40000+airtime(128)/1200000,
            'object_reserves_bytes':budget,
            'startup_two_timing_us':2*airtime(80),'native':native,
            'vectors_sha256':{k:hashlib.sha256(bytes.fromhex(v)).hexdigest() for k,v in expected.items()},
            'sources':{str(src.relative_to(REPO)):hashlib.sha256(src.read_bytes()).hexdigest() for src in sources},
            'compiler':subprocess.check_output([compiler,'--version'],text=True).splitlines()[0]}
    (out/'report.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    print('Independent Python/C metadata+timing vectors PASS; airtime369152+258560us')

if __name__=='__main__':main()
