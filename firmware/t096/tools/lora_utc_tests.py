#!/usr/bin/env python3
"""Focused finite UTC-job owner/core/actual RX-retune regressions. MIT."""
import argparse, hashlib, json, os, re, subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
 crypto=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 body=a.out/'owner.c';body.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_passive_service.c').read_text(),flags=re.M))
 hwbody=a.out/'hw_guard.c';hw=(APP/'src/lora_hw.c').read_text();start=hw.index('static struct r2_lora_hw_rejection rejected;');end=hw.index('\n#endif\nstatic int ',start);hwbody.write_text(hw[start:end])
 src=[APP/'tests/lora_utc_test.c',APP/'src/lora_passive_core.c',APP/'protocol/lora_passive.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',APP/'src/precision.c',*[crypto/'source'/(n+'.c')for n in ('hmac','sha256','utils')]]
 cmd=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(crypto/'include'),'-DR2_LORA_PASSIVE_SLOW=1','-DR2_PASSIVE_SERVICE_BODY="'+str(body)+'"','-DR2_HW_ADMISSION_BODY="'+str(hwbody)+'"',*map(str,src),'-o',str(a.out/'utc')]
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(a.out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode();results=[]
 for mode in [*range(1,10),*range(10,45),46]:
  r=subprocess.run([str(a.out/'utc'),str(mode)],capture_output=True,text=True,timeout=20);(a.out/f'case{mode}.log').write_text(r.stdout+r.stderr);r.check_returncode();results.append([json.loads(line) for line in r.stdout.splitlines()]);print(f'PASS UTC case {mode}',flush=True)
 # Public parser boundary, including builds without LoRa.
 for enabled in (False,True):
  binary=a.out/('parser-on'if enabled else'parser-off')
  flags=cmd[:cmd.index('-DR2_LORA_PASSIVE_SLOW=1')]
  r=subprocess.run([*flags,*(['-DR2_LORA=1']if enabled else[]),str(APP/'src/core.c'),str(APP/'tests/lora_core_test.c'),'-o',str(binary)],capture_output=True,text=True,timeout=30)
  (a.out/(binary.name+'-compile.log')).write_text(r.stdout+r.stderr);r.check_returncode()
  r=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10);(a.out/(binary.name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode()
 fast=[x.replace('-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_PASSIVE_FAST=1') for x in cmd];fast[-1]=str(a.out/'fast')
 r=subprocess.run(fast,capture_output=True,text=True,timeout=60);(a.out/'fast-compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
 r=subprocess.run([str(a.out/'fast'),'45'],capture_output=True,text=True,timeout=10);(a.out/'fast.log').write_text(r.stdout+r.stderr);r.check_returncode()
 rcbody=a.out/'rc8.c';rcbody.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_service.c').read_text(),flags=re.M))
 wrapper=a.out/'unsupported.c';wrapper.write_text('#define main legacy_unused_main\n#include "'+str(APP/'tests/lora_service_test.c')+'"\n#undef main\nint main(void){init(false);assert(r2_lora_utc_request(1,0,true)==-ENOTSUP);char out[1024];assert(r2_lora_utc_json(out,sizeof out,0)>0&&strstr(out,"\\\"supported\\\":false")&&strstr(out,"0000000000000016"));puts(out);return 0;}\n')
 flags=cmd[:cmd.index('-DR2_LORA_PASSIVE_SLOW=1')]
 common=[APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',*[crypto/'source'/(n+'.c')for n in ('hmac','sha256','utils')]]
 r=subprocess.run([*flags,'-DR2_LORA_SERVICE_BODY="'+str(rcbody)+'"',str(wrapper),*map(str,common),'-o',str(a.out/'rc8')],capture_output=True,text=True,timeout=60);(a.out/'rc8-compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
 r=subprocess.run([str(a.out/'rc8')],capture_output=True,text=True,timeout=10);(a.out/'rc8.log').write_text(r.stdout+r.stderr);r.check_returncode()
 files=[*src,APP/'src/lora_passive_service.c',APP/'protocol/lora_passive_schedule.h',APP/'include/lora_service.h',APP/'include/lora_watchdog.h',APP/'src/lora_hw.c',APP/'include/lora_passive_core.h',APP/'src/core.c',APP/'tests/lora_core_test.c',APP/'src/lora_service.c',APP/'tests/lora_service_test.c',APP/'tests/lora_ram_fixture.h',Path(__file__)]
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_SYNTHETIC_UTC_JOB_OWNER',hardware=False,cases=len(results),parser_variants=2,unsupported_variants=['fast','RC8'],results=results,sources={str(f.relative_to(APP)):hashlib.sha256(f.read_bytes()).hexdigest()for f in files if f.is_relative_to(APP)}),indent=2)+'\n')
if __name__=='__main__':main()
