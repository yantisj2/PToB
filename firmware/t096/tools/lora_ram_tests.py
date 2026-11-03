#!/usr/bin/env python3
"""Real RAM-only owner/core/codec at fake boundaries. MIT, Jonathan Yantis, 2026."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
 crypto=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 body=a.out/'owner_body.c';body.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_passive_service.c').read_text(),flags=re.M))
 hwbody=a.out/'hw_admission_body.c';hw=(APP/'src/lora_hw.c').read_text();start=hw.index('static struct r2_lora_hw_rejection rejected;');end=hw.index('\n#endif\nstatic int ',start);hwbody.write_text(hw[start:end])
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(crypto/'include'),'-DR2_PASSIVE_SERVICE_BODY="'+str(body)+'"','-DR2_HW_ADMISSION_BODY="'+str(hwbody)+'"']
 sources=[APP/'tests/lora_ram_test.c',APP/'src/lora_passive_core.c',APP/'protocol/lora_passive.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',*[crypto/'source'/(n+'.c') for n in ('hmac','sha256','utils')]]
 cases=0;log=[]
 for profile,defs in [('slow',['-DR2_LORA_PASSIVE_SLOW=1']),('fast',['-DR2_LORA_PASSIVE_FAST=1']),('diagnostic',['-DR2_LORA_PASSIVE_FAST=1','-DR2_LORA_TX_DIAGNOSTIC=1'])]:
  binary=a.out/profile;r=subprocess.run([*flags,*defs,*map(str,sources),'-o',str(binary)],capture_output=True,text=True);(a.out/(profile+'-compile.log')).write_text(r.stdout+r.stderr);r.check_returncode()
  for mode in [*range(1,16),20,*range(21,95)]:
   if profile!='slow' and mode>=35:continue
   if profile!='slow' and mode==5:continue
   if profile=='diagnostic' and (mode>=28 or mode in (2,3,7,9,10,11,12,13,14,15,20,22,23,24,25,26,27)):continue
   r=subprocess.run([str(binary),str(mode)],capture_output=True,text=True);log.append(r.stdout+r.stderr);(a.out/'tests.log').write_text(''.join(log));r.check_returncode();print(r.stdout.strip());cases+=1
 # The old250ms hint retirement must fail the same legal150ms-late SYNC.
 oldcore=a.out/'old_hint_core.c';oldcore.write_text((APP/'src/lora_passive_core.c').read_text().replace('now>r->hint_us+(r->prediction?LP_PREDICT_HINT_RETIRE_US:LP_HINT_RETIRE_US)','now>r->hint_us+250000'))
 oldbinary=a.out/'old-hint-negative';oldsources=[oldcore if f==APP/'src/lora_passive_core.c' else f for f in sources]
 subprocess.run([*flags,'-DR2_LORA_PASSIVE_SLOW=1','-DR2_QA_OLD_HINT_EXPIRY=1',*map(str,oldsources),'-o',str(oldbinary)],check=True)
 r=subprocess.run([str(oldbinary),'38'],capture_output=True,text=True);(a.out/'old-hint-negative.log').write_text(r.stdout+r.stderr);r.check_returncode();assert 'old250ms hint expiry loses' in r.stdout;print(r.stdout.strip());cases+=1
 # PHY-aware negative: strip only the new rendezvous floor, retaining fences.
 oldowner=a.out/'old_rendezvous_owner.c';oldowner.write_text(body.read_text().replace('if(retry_at_us<rendezvous)retry_at_us=rendezvous;','(void)rendezvous;'))
 oldbinary=a.out/'old-rendezvous-negative';oldflags=[f.replace(str(body),str(oldowner))for f in flags]
 subprocess.run([*oldflags,'-DR2_LORA_PASSIVE_SLOW=1','-DR2_QA_OLD_RENDEZVOUS=1',*map(str,sources),'-o',str(oldbinary)],check=True)
 for mode in (72,80):
  r=subprocess.run([str(oldbinary),str(mode)],capture_output=True,text=True);(a.out/('old-rendezvous-negative-'+str(mode)+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();assert 'physically missed' in r.stdout;print(r.stdout.strip());cases+=1
 # Causal negatives: preserve every other current fence/profile value.
 variants=[]
 oldcodec=a.out/'old400_codec.c';oldcodec.write_text((APP/'protocol/lora_passive.c').read_text().replace('p->delay_10ms>LP_MAX_DELAY_10MS','p->delay_10ms>400'))
 variants.append(('old400-delay',82,flags,[oldcodec if f==APP/'protocol/lora_passive.c' else f for f in sources],'"error":-22'))
 oldpredict=a.out/'old_predicted_tune.c';oldpredict.write_text(body.read_text().replace('if(wait_control)return 0;','(void)wait_control;'))
 variants.append(('old-predicted-retune',83,[f.replace(str(body),str(oldpredict))for f in flags],sources,'visible'))
 oldlate=a.out/'old_direct_late.c';oldlate.write_text(body.read_text().replace('utc_finish(UTC_EXPIRED,UTC_FAILED);if(!t->manual)inc(&retry_count);begin_stop(0);return 0;',
  'if(utc_running_token){lp_tx_cancel(t,true);utc_finish(UTC_EXPIRED,UTC_FAILED);if(!t->manual){retry_at_us=now+LP_RETRY_US;++retry_count;}}else{lp_tx_cancel(t,true);retry_at_us=now+LP_RETRY_US;++retry_count;}'))
 variants.append(('old-direct-late',84,[f.replace(str(body),str(oldlate))for f in flags],sources,'injected&&closure'))
 for name,mode,negative_flags,negative_sources,expected in variants:
  binary=a.out/name;subprocess.run([*negative_flags,'-DR2_LORA_PASSIVE_SLOW=1',*map(str,negative_sources),'-o',str(binary)],check=True)
  r=subprocess.run([str(binary),str(mode)],capture_output=True,text=True);(a.out/(name+'.log')).write_text(r.stdout+r.stderr)
  assert r.returncode!=0 and expected in r.stderr,(name,r.returncode,r.stderr)
  print('PASS causal negative '+name);cases+=1
 # Actual RAM diagnostic and watchdog bodies, without any flash fake or driver.
 for name in ['zephyr/kernel.h','zephyr/sys/atomic.h','zephyr/sys/reboot.h','zephyr/device.h','zephyr/drivers/watchdog.h','zephyr/drivers/hwinfo.h','cmsis_core.h','build_identity.h']:
  f=a.out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Fixture boundary */\n')
 (a.out/'build_identity.h').write_text('#ifndef R2_SOURCE_SHA\n#define R2_SOURCE_SHA "'+'f'*64+'"\n#endif\n')
 runtime_flags=[*flags,'-DR2_LORA=1','-I'+str(a.out),'-include',str(APP/'tests/zephyr_fake.h')]
 for name,extra in [('diagnostic_ram_test',[APP/'src/core.c']),('watchdog_ram_test',[APP/'src/watchdog_policy.c'])]:
  binary=a.out/name;r=subprocess.run([*runtime_flags,str(APP/'tests'/(name+'.c')),*map(str,extra),'-o',str(binary)],capture_output=True,text=True);(a.out/(name+'-compile.log')).write_text(r.stdout+r.stderr);r.check_returncode()
  r=subprocess.run([str(binary)],capture_output=True,text=True);(a.out/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip());cases+=1
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_SYNTHETIC_RAM_ONLY',cases=cases,hardware=False,flash_backend_linked=False,sources={str(f.relative_to(APP)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [*sources,APP/'src/lora_passive_service.c',APP/'protocol/lora_passive_schedule.h',APP/'include/lora_service.h',APP/'tests/lora_ram_fixture.h',APP/'include/lora_watchdog.h',APP/'src/diagnostic.c',APP/'src/watchdog.c',APP/'src/core.c',APP/'src/watchdog_policy.c',APP/'include/diagnostic.h',APP/'tests/diagnostic_ram_test.c',APP/'tests/watchdog_ram_test.c',APP/'tests/watchdog_test.c',Path(__file__)]if f.is_relative_to(APP)}),indent=2)+'\n')
if __name__=='__main__':main()
