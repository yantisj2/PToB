#!/usr/bin/env python3
"""Focused offline .180 allocation accounting guards; no firmware execution.
Copyright (c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import copy,json
from inspect_build import crash_static_allowance

CONFIG='CONFIG_ISR_STACK_SIZE=3072\nCONFIG_MP_MAX_NUM_CPUS=1\nCONFIG_MPU_STACK_GUARD=y\n'
SYMBOLS={name:[dict(size=size,address=address,section='noinit',type='SHT_NOBITS')]
         for name,size,address in [('r2_crash_record',512,0x2003a488),
                                   ('z_interrupt_stacks',3136,0x20033600)]}
def main():
    checks=[]
    def check(name,gm7=True,profile='GM',version=(0,15,180),config=CONFIG,symbols=None,expected=1408,reject=False):
        try:value=crash_static_allowance(gm7,profile,version,config,SYMBOLS if symbols is None else symbols)
        except ValueError:
            assert reject,name
        else:assert not reject and value==expected,(name,value)
        checks.append(name)
    check('exact .180 GM linked allocation')
    check('exact .181 GM inherited allocation',version=(0,15,181))
    pt=copy.deepcopy(SYMBOLS)
    for name,size in [('pt_input',1128),('pt_output',728)]:pt[name]=[dict(size=size,address=0x20020000,section='bss',type='SHT_NOBITS')]
    check('exact .182 GM unchanged PT allocations',version=(0,15,182),symbols=pt)
    check('exact .183 GM inherited allocations',version=(0,15,183),symbols=pt)
    check('exact .184 GM inherited allocations',version=(0,15,184),symbols=pt)
    check('exact .185 GM inherited allocations',version=(0,15,185),symbols=pt)
    check('exact .186 GM inherited allocations',version=(0,15,186),symbols=pt)
    for name in ('pt_input','pt_output'):
        q=copy.deepcopy(pt);q[name][0]['size']+=8
        check('no hidden182 '+name+' growth',version=(0,15,182),symbols=q,reject=True)
    for profile in ('RC','DC','PC'):check('no '+profile+' allowance',profile=profile,expected=0)
    check('no legacy backend allowance',gm7=False,expected=0)
    for version in ((0,15,179),(0,15,187)):check('no other version '+str(version),version=version,expected=0)
    for line in CONFIG.splitlines():check('requires '+line,config=CONFIG.replace(line,''),reject=True)
    for name in SYMBOLS:
        for field,value in [('size',511),('address',0x20040000),('section','bss'),('type','SHT_PROGBITS')]:
            q=copy.deepcopy(SYMBOLS);q[name][0][field]=value;check(name+' '+field,symbols=q,reject=True)
        q=copy.deepcopy(SYMBOLS);q[name]=[];check(name+' missing',symbols=q,reject=True)
        q=copy.deepcopy(SYMBOLS);q[name]*=2;check(name+' duplicate',symbols=q,reject=True)
    for line in CONFIG.splitlines():
        check('184 requires '+line,version=(0,15,184),config=CONFIG.replace(line,''),symbols=pt,reject=True)
    for name in pt:
        q=copy.deepcopy(pt);q[name][0]['size']+=8
        check('no hidden184 '+name+' growth',version=(0,15,184),symbols=q,reject=True)
        q=copy.deepcopy(pt);q[name]=[]
        check('184 '+name+' missing',version=(0,15,184),symbols=q,reject=True)
        q=copy.deepcopy(pt);q[name]*=2
        check('184 '+name+' duplicate',version=(0,15,184),symbols=q,reject=True)
    for line in CONFIG.splitlines():
        check('185 requires '+line,version=(0,15,185),config=CONFIG.replace(line,''),symbols=pt,reject=True)
    for name in pt:
        q=copy.deepcopy(pt);q[name][0]['size']+=8
        check('no hidden185 '+name+' growth',version=(0,15,185),symbols=q,reject=True)
        q=copy.deepcopy(pt);q[name]=[]
        check('185 '+name+' missing',version=(0,15,185),symbols=q,reject=True)
        q=copy.deepcopy(pt);q[name]*=2
        check('185 '+name+' duplicate',version=(0,15,185),symbols=q,reject=True)
    # The original RC8 ceiling remains4096; separately accounted bytes exactly
    # explain3236B spare. The independent absolute1024B gate stays in inspect().
    assert 3236+1408==8740-4096 and 3235+1408<8740-4096
    print(json.dumps(dict(status='PASS',checks=checks,allowance=1408,rc8_budget=4096)))
if __name__=='__main__':main()
