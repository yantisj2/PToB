#!/usr/bin/env python3
"""Differential actual BLE fitter cache proof, including fit-call population.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
No hardware; reduced fit-call count is not a native CPU/latency measurement.
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 source=APP/'include/precision_servo.h';body=source.read_text();needle='if ((history_changed || !c->fit_valid) && !r2_servo_fit(c))';assert needle in body
 cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang');flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-but-set-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include')]
 objects=[]
 for name in ('cached','uncached'):
  header=out/(name+'.h');variant=body if name=='cached'else body.replace(needle,'if (!r2_servo_fit(c))')
  variant=variant.replace('  c->fit_valid = false;\n  unsigned n = c->count;',f'  ++{name}_fits;\n  c->fit_valid = false;\n  unsigned n = c->count;')
  header.write_text(f'extern unsigned {name}_fits;\n'+variant)
  wrapper=out/(name+'.c');wrapper.write_text(f'#include "{header}"\nunsigned {name}_fits;\nbool {name}_observe(r2_servo_clock_t*c,const r2_servo_sample_t*s,bool a,bool b){{return r2_servo_observe_permitted(c,s,a,b);}}\n')
  obj=out/(name+'.o');subprocess.run([*flags,'-c',str(wrapper),'-o',str(obj)],check=True);objects.append(obj)
 binary=out/'equivalence';subprocess.run([*flags,str(APP/'tests/servo_equivalence_test.c'),*map(str,objects),'-o',str(binary)],check=True)
 result=subprocess.run([str(binary)],capture_output=True,text=True);(out/'test.log').write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
 (out/'receipt.json').write_text(json.dumps(dict(status='PASS_DIFFERENTIAL_PRODUCTION_BODY',hardware=False,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),test=result.stdout.strip()),indent=2)+'\n')
if __name__=='__main__':main()
