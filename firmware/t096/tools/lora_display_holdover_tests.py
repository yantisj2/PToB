#!/usr/bin/env python3
"""MIT. Jonathan Yantis, 2026. UI-only slow holdover; synthetic boundaries, no hardware."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
 a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
 cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang');crypto=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
 hwbody=a.out/'hw_admission_body.c';hw=(APP/'src/lora_hw.c').read_text();start=hw.index('static struct r2_lora_hw_rejection rejected;');end=hw.index('\n#endif\nstatic int ',start);hwbody.write_text(hw[start:end])
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_BACKUP_TRANSMITTER_FACTORY=3','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(crypto/'include')]
 body=a.out/'owner_body.c';body.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_passive_service.c').read_text(),flags=re.M))
 sources=[APP/'tests/lora_display_holdover_test.c',APP/'src/lora_passive_core.c',APP/'protocol/lora_passive.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',*[crypto/'source'/(n+'.c')for n in ['hmac','sha256','utils']]]
 binary=a.out/'owner';subprocess.run([*flags,'-DR2_PASSIVE_SERVICE_BODY="'+str(body)+'"','-DR2_HW_ADMISSION_BODY="'+str(hwbody)+'"',*map(str,sources),'-o',str(binary)],check=True)
 log=[]
 for i in range(1,31):
  r=subprocess.run([str(binary),str(i)],capture_output=True,text=True);log.append(r.stdout+r.stderr);(a.out/'owner.log').write_text(''.join(log));r.check_returncode();print(r.stdout.strip())
 ui=a.out/'ui';subprocess.run([*flags,'-DR2_LORA=1',str(APP/'tests/lora_display_holdover_ui_test.c'),str(APP/'src/precision.c'),'-o',str(ui)],check=True)
 r=subprocess.run([str(ui),str(a.out)],capture_output=True,text=True);(a.out/'ui.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
 files=[APP/'protocol/lora_passive_schedule.h',APP/'protocol/lora_passive.c',APP/'include/lora_service.h',APP/'include/lora_watchdog.h',APP/'include/lora_passive_core.h',APP/'src/lora_passive_core.c',APP/'src/lora_passive_service.c',APP/'src/ui_render.c',APP/'legacy_ui/draw.inc',APP/'tests/lora_ram_fixture.h',APP/'tests/lora_display_holdover_test.c',APP/'tests/lora_display_holdover_ui_test.c',Path(__file__)]
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_SYNTHETIC_BOUNDARIES_ONLY',owner_cases=30,ui=True,hardware=False,absolute_UTC_accuracy='UNQUALIFIED',holdover_uncertainty='UNKNOWN',sources={str(f.relative_to(APP)):hashlib.sha256(f.read_bytes()).hexdigest()for f in files}),indent=2)+'\n')
if __name__=='__main__':main()
