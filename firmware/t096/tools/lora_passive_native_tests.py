#!/usr/bin/env python3
"""Cross-compile passive components with an inspected native build's ARM flags.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Compilation/resource evidence only: no linking, installation or hardware access.
"""
import argparse,hashlib,json,shlex,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--arm-reference',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    reference=a.arm_reference.resolve();receipt=json.loads((reference/'receipt.json').read_text())
    inspection=json.loads((reference/'inspection.json').read_text())
    if inspection['status']!='PASS' or receipt['profile'] not in ('GM','RC'):raise ValueError('Need an inspected normal T096 reference')
    entries=json.loads((reference/'target/compile_commands.json').read_text())
    matches=[e for e in entries if e['file'].endswith('/src/lora_hw.c')]
    if len(matches)!=1:raise ValueError('Need exactly one actual LoRa owner compilation')
    entry=matches[0];command=entry.get('arguments') or shlex.split(entry['command'])
    flags=[];skip=False
    for arg in command:
        if skip:skip=False;continue
        if arg=='-o':skip=True;continue
        if arg=='-c' or arg==entry['file']:continue
        flags.append(arg)
    flags[1:1]=['-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-DR2_LORA_PASSIVE_FAST=1']
    a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sources=[APP/'protocol/lora_passive.c',APP/'src/lora_passive_core.c',APP/'src/lora_hw.c',APP/'src/lora_passive_service.c']
    frames={}
    for source in sources:
        obj=a.out/(source.stem+'.o')
        run=subprocess.run([*flags,'-fstack-usage','-c',str(source),'-o',str(obj)],cwd=entry['directory'],text=True,capture_output=True)
        (a.out/(source.stem+'.log')).write_text(run.stdout+run.stderr);run.check_returncode()
        for line in obj.with_suffix('.su').read_text().splitlines():
            location,size,kind=line.rsplit('\t',2)
            if 'dynamic' in kind and 'bounded' not in kind:raise ValueError('Unbounded stack: '+line)
            frames[location.rsplit(':',1)[-1]]=int(size)
    probe=a.out/'sizes.c';probe.write_text('#include "lora_passive_core.h"\nchar checkpoint_bytes[sizeof(struct lp_checkpoint)];\nchar tx_bytes[sizeof(struct lp_tx)];\nchar rx_bytes[sizeof(struct lp_rx)];\n')
    obj=a.out/'sizes.o';subprocess.run([*flags,'-c',str(probe),'-o',str(obj)],cwd=entry['directory'],check=True)
    nm=str(Path(flags[0]).with_name('arm-zephyr-eabi-nm'))
    output=subprocess.check_output([nm,'-S','--size-sort',str(obj)],text=True)
    sizes={line.split()[-1]:int(line.split()[1],16) for line in output.splitlines() if line.split()[-1].endswith('_bytes')}
    result=dict(status='PASS_ARM_COMPILATION_ONLY',hardware='NOT_TESTED',native_linked_resource_qualification=False,
                reference_source=receipt['source_sha256'],reference_profile=receipt['profile'],sizes=sizes,
                frame_bytes=frames,max_individual_frame_bytes=max(frames.values()),
                sources={str(s.relative_to(APP)):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources})
    (a.out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('status','sizes','max_individual_frame_bytes')}))
if __name__=='__main__':main()
