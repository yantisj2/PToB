#!/usr/bin/env python3
"""Shared on-demand JSON writer actual-code checks; no hardware."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
A=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();O=a.out.resolve();O.mkdir(parents=True,exist_ok=False)
inputs=[A/'src/bounded_json.c',A/'include/bounded_json.h',A/'tests/bounded_json_test.c',Path(__file__)]
cmd=[os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(A/'include'),str(inputs[0]),str(inputs[2]),'-o',str(O/'json')]
(O/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
for label,command in [('compile',cmd),('run',[str(O/'json')])]:
 r=subprocess.run(command,capture_output=True,text=True,timeout=60);(O/(label+'.log')).write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,flush=True);r.check_returncode()
(O/'receipt.json').write_text(json.dumps({'status':'PASS_ACTUAL_SHARED_WRITER_HOST_ONLY','inputs':{str(p.relative_to(A)):hashlib.sha256(p.read_bytes()).hexdigest()for p in inputs}},indent=2)+'\n')
