#!/usr/bin/env python3
"""Focused R2 actual protocol/core sanitizer tests; no device or Docker access."""
from pathlib import Path
import os,subprocess,tempfile,shutil
APP=Path(__file__).resolve().parents[1]
compiler=os.environ.get('CC') or ((__import__('os').environ.get('CC') or __import__('shutil').which('clang')) if Path((__import__('os').environ.get('CC') or __import__('shutil').which('clang'))).is_file() else shutil.which('clang'))
with tempfile.TemporaryDirectory(prefix='r2-protocol-test-') as directory:
    flags=[compiler,'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-I'+str(APP/'include'),'-I'+str(APP/'protocol')]
    common=[str(APP/'protocol'/name) for name in ['control_codec.c','direct_control_state.c']]
    vector=Path(directory)/'config5_vector'
    subprocess.run([*flags,str(APP/'protocol/authority_codec.c'),str(APP/'tests/config5_vector_test.c'),'-o',str(vector)],check=True,timeout=30)
    subprocess.run([str(vector)],check=True,timeout=30)
    policy=Path(directory)/'election_policy'
    subprocess.run([*flags,str(APP/'protocol/election_policy.c'),str(APP/'tests/election_policy_test.c'),'-o',str(policy)],check=True,timeout=30)
    subprocess.run([str(policy)],check=True,timeout=30)
    for name,extra in [('direct_control_state',common),('standalone',[str(APP/'protocol'/x) for x in ['authority_codec.c','election_policy.c','network.c']]),('authority_network',[str(APP/'protocol'/x) for x in ['authority_codec.c','election_policy.c','network.c']])]:
        out=Path(directory)/name
        subprocess.run([*flags,*extra,str(APP/'tests'/f'{name}_test.c'),'-o',str(out)],check=True,timeout=30)
        for args in ([[]] if name!='authority_network' else [[str(delay)] for delay in [2000,6000,16000,30000]]+[["30000","drift"]]):
            subprocess.run([str(out),*args],check=True,timeout=30)

    out=Path(directory)/'pair_durability'
    subprocess.run([*flags,*[str(APP/'protocol'/x) for x in ['authority_codec.c','election_policy.c','network.c']],str(APP/'tests/pair_durability_test.c'),'-o',str(out)],check=True,timeout=30)
    for seed,loss,delay in [(23,0,2000),(42,5,6000),(71,10,16000),(101,15,30000),(191,20,30000)]:
        subprocess.run([str(out),str(seed),str(loss),str(delay)],check=True,timeout=60)
