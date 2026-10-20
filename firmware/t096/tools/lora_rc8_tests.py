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

APP=Path(__file__).resolve().parents[1]
REPO=APP.parents[1]

def airtime(payload):
    # Explicit header, CRC, SF10/BW500/CR4/6, DE=0, preamble12.
    symbols=8+max(math.ceil((8*payload-4*10+28+16)/(4*10))*6,0)
    return int((12+4.25+symbols)*(2**10)*1_000_000/500_000)

def reference(vector):
    group=vector['group'];scope=vector['scope'];c=vector['correction']
    context=struct.pack('<BQI',1,int(group['id_hex'],16),group['epoch'])
    key=bytes.fromhex(group['synthetic_psk_hex'])
    meta=bytearray(52);meta[:4]=b'R8\x02\x01'
    for offset,name,fmt in [(4,'emitter','Q'),(12,'emitter_boot','Q'),(20,'stream','Q'),(28,'root','Q'),(36,'root_boot','Q'),(44,'source_epoch','I')]:
        struct.pack_into('<'+fmt,meta,offset,scope[name])
    meta[48]=scope['scale'];meta[49]=scope['kind']
    tag=hmac.new(key,b'PTOBR8M2'+context+meta,hashlib.sha256).digest()[:16]
    timing=bytearray(64);timing[:4]=b'R8\x02\x02';timing[4:20]=tag
    struct.pack_into('<I',timing,20,2)
    for offset,name,fmt in [(24,'sequence','I'),(28,'map_generation','I'),(32,'event_ns','Q'),(40,'created_ns','Q'),(48,'expires_ns','Q'),(56,'map_age_us','I'),(60,'upstream_bound_ns','I')]:
        struct.pack_into('<'+fmt,timing,offset,c[name])
    timing_tag=hmac.new(key,b'PTOBR8T2'+context+timing,hashlib.sha256).digest()[:16]
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
    sizes={key:int(size) for key,size in re.findall(r'(receiver|export|packet|transmission|stop|gesture)=(\d+)',result.stdout)}
    contract=json.loads((REPO/'protocol/rc8/contract.json').read_text())
    budget=contract['memory']['planned_items_bytes']
    assert max(sizes['receiver'],sizes['transmission']+sizes['export'])<=budget['exclusive_boot_role_union']
    assert sum(budget.values())==contract['memory']['planned_total_bytes']<=4096
    vector=json.loads((REPO/'protocol/rc8/vectors.json').read_text())
    expected=reference(vector)
    assert expected==vector['wire_hex']
    for name,value in expected.items():
        actual=subprocess.check_output([str(out/'rc8'),name],text=True,timeout=30).strip()
        assert actual==value,'C '+name+' codec differs from independent Python HMAC/layout vector'
    assert airtime(64)==209408 and airtime(68)==221696 and airtime(80)==258560 and airtime(176)==492032
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
            'rx_metadata_key_setup':['rc8_receiver_process','metadata','rc8_metadata_decode','r2_group_verify','r2_group_mac','tc_hmac_set_key','tc_sha256_final','compress'],
            'rx_timing_key_setup':['rc8_receiver_process','rc8_decode','r2_group_verify','r2_group_mac','tc_hmac_set_key','tc_sha256_final','compress'],
            'tx_context_key_setup':['rc8_tx_prepare','rc8_context_id','rc8_metadata_encode','r2_group_mac','tc_hmac_set_key','tc_sha256_final','compress'],
            'tx_context_final':['rc8_tx_prepare','rc8_context_id','rc8_metadata_encode','r2_group_mac','tc_hmac_final','tc_sha256_final','compress']}
        native={'compiler_frames_bytes':frames,'nested_known_frames_bytes':{name:sum(frames.get(fn,0) for fn in chain) for name,chain in paths.items()},
                'paths':paths,'limits':'Known C frames only; inlined helpers contribute within caller; excludes worker frame, IRQ/kernel/libc/libgcc/SPI/capture costs and physical high-water. Not stack qualification.'}
    report={'status':'PASS_RC8_WIRE2_OFFLINE','hardware':'NOT_ACCESSED','precision':'UNQUALIFIED',
            'tests':result.stdout.strip(),'metadata_bytes':68,'timing_bytes':80,'metadata_airtime_us':airtime(68),'timing_airtime_us':airtime(80),
            'host_abi_sizes':sizes,'occupancy_timing4s_metadata60s_percent':airtime(80)/40000+airtime(68)/600000,
            'planned_static_bytes':sum(budget.values()),
            'startup_one_metadata_two_timing_us':airtime(68)+2*airtime(80),'native':native,
            'vectors_sha256':{k:hashlib.sha256(bytes.fromhex(v)).hexdigest() for k,v in expected.items()},
            'sources':{str(src.relative_to(REPO)):hashlib.sha256(src.read_bytes()).hexdigest() for src in sources},
            'compiler':subprocess.check_output([compiler,'--version'],text=True).splitlines()[0]}
    (out/'report.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    print('Independent Python/C metadata+timing vectors PASS; airtime221696+258560us')

if __name__=='__main__':main()
