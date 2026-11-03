#!/usr/bin/env python3
"""Finite BLE original grants through actual exporter/owner/driver guard. MIT."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
 crypto=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 body=a.out/'owner.c';body.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_passive_service.c').read_text(),flags=re.M))
 hwbody=a.out/'hw_guard.c';hw=(APP/'src/lora_hw.c').read_text();start=hw.index('static struct r2_lora_hw_rejection rejected;');end=hw.index('\n#endif\nstatic int ',start);hwbody.write_text(hw[start:end])
 src=[APP/'tests/lora_ble_admission_test.c',APP/'src/lora_passive_core.c',APP/'protocol/lora_passive.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',APP/'src/lora_source.c',APP/'src/precision.c',*[crypto/'source'/(n+'.c')for n in ('hmac','sha256','utils')]]
 cmd=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(crypto/'include'),'-DR2_LORA_PASSIVE_SLOW=1','-DR2_PASSIVE_SERVICE_BODY="'+str(body)+'"','-DR2_HW_ADMISSION_BODY="'+str(hwbody)+'"',*map(str,src),'-o',str(a.out/'repro')]
 r=subprocess.run(cmd,capture_output=True,text=True);(a.out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode();results=[]
 for mode in (1,2,3,4,5,9):
  r=subprocess.run([str(a.out/'repro'),str(mode)],capture_output=True,text=True);(a.out/('case'+str(mode)+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();results.append(json.loads(r.stdout));print(r.stdout.strip())
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_SYNTHETIC_FINITE_BLE_OWNER',hardware=False,original_intervals_extended=False,assumptions='Actual source follower/owner/core/window guard; synthetic625ms BLE map delivery and physical completions; original1s restrictionfor120s then1.497s recovery; original1.2s/600ms delivery eventually launches through normal retry jitter;400ms pre-RF encoding/SPI in mode3',results=results,sources={str(f.relative_to(APP)):hashlib.sha256(f.read_bytes()).hexdigest()for f in [*src,APP/'src/lora_passive_service.c',APP/'protocol/lora_passive_schedule.h',APP/'include/lora_service.h',APP/'include/lora_watchdog.h',APP/'src/lora_hw.c',APP/'tests/lora_ram_fixture.h',Path(__file__)]if f.is_relative_to(APP)}),indent=2)+'\n')
if __name__=='__main__':main()
