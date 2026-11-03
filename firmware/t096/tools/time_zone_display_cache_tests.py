#!/usr/bin/env python3
"""Offline actual authenticated RX/UI learned-zone cache. MIT, Jonathan Yantis2026."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def block(source,name):
 start=source.index(name);brace=source.index('{',start);end=brace+1;depth=1
 while depth:
  if source[end]=='{':depth+=1
  elif source[end]=='}':depth-=1
  end+=1
 return source[start:end]
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 for name in ('kernel.h','irq.h','device.h','usb/usb_device.h','sys/atomic.h','random/random.h'):
  header=out/'zephyr'/name;header.parent.mkdir(parents=True,exist_ok=True);header.write_text('/* Offline boundary supplied by zephyr_fake.h. */\n')
 crypto=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
 hw=(APP/'src/lora_hw.c').read_text();start=hw.index('static struct r2_lora_hw_rejection rejected;');end=hw.index('\n#endif\nstatic int ',start)
 hwbody=out/'hw_admission_body.c';hwbody.write_text(hw[start:end])
 body=out/'owner_body.c';body.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_passive_service.c').read_text(),flags=re.M))
 ui=(APP/'src/ui_service.c').read_text();publish=out/'ui_publish_body.h';publish.write_text(block(ui,'void r2_ui_publish(')+'\n'+block(ui,'void r2_ui_stop(')+'\n')
 gm=out/'gm_tz_body.h';gm.write_text(block((APP/'src/gm7_service.c').read_text(),'static void observe_coarse_tz(')+'\n')
 cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_TZ_WIRE=1','-DR2_TZ_AUTO=1','-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(crypto/'include'),'-DR2_PASSIVE_SERVICE_BODY="'+str(body)+'"','-DR2_HW_ADMISSION_BODY="'+str(hwbody)+'"','-DR2_UI_PUBLISH_BODY="'+str(publish)+'"','-DR2_GM_TZ_BODY="'+str(gm)+'"']
 files=[APP/'tests/ui_time_zone_cache_test.c',*[APP/'src'/name for name in ['lora_passive_core.c','lora_rc8_policy.c','precision.c','time_zone_database.c']],*[APP/'protocol'/name for name in ['lora_passive.c','lora_rc8.c','group_auth.c','gm7_codec.c','gm7_control.c','gm7_owner.c','gm7_schedule.c','gm7_bridge.c','gm7_promote.c']],*[crypto/'source'/(n+'.c')for n in ['hmac','sha256','utils']]]
 cmd=[*flags,*map(str,files),'-o',str(out/'test')]
 with (out/'compile.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 with (out/'run.log').open('w') as log:subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 # Production snapshot/backend switch/main publisher, including own GM+LoRa TX.
 gm_ui=out/'gm_ui_body.h';gm_source=(APP/'src/gm7_service.c').read_text()
 gm_ui.write_text(block(gm_source,'void r2_service_snapshot(')+'\n'+block(gm_source,'void r2_service_ui(')+'\n')
 switch_ui=out/'switch_ui_body.h';switch_ui.write_text(block((APP/'src/service_switch.c').read_text(),'void r2_service_ui(')+'\n')
 main_ui=out/'main_ui_body.h';main_source=(APP/'src/main.c').read_text()
 main_ui.write_text(block(main_source,'static void lora_ui_view(')+'\n'+block(main_source,'static uint64_t __attribute__((noinline)) gm7_ui_publish(')+'\n')
 pipecmd=[*flags,'-I'+str(out),'-DR2_GM7_NATIVE=1','-DR2_NATIVE_BLE=1','-DR2_GM_UI_BODY="'+str(gm_ui)+'"','-DR2_SWITCH_UI_BODY="'+str(switch_ui)+'"','-DR2_MAIN_UI_BODY="'+str(main_ui)+'"',str(APP/'tests/ui_time_zone_pipeline_test.c'),str(APP/'src/precision.c'),str(APP/'src/time_zone_database.c'),'-o',str(out/'pipeline')]
 (out/'pipeline-command.json').write_text(json.dumps(pipecmd,indent=2)+'\n')
 with (out/'pipeline-compile.log').open('w') as log:subprocess.run(pipecmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 with (out/'pipeline-run.log').open('w') as log:subprocess.run([str(out/'pipeline')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 # Actual maximum-width status/trace formatter, with TZ diagnostics enabled.
 (out/'build_identity.h').write_text('#define R2_VERSION "0.1.0"\n#define R2_SOURCE_SHA "'+'f'*64+'"\n#define R2_CHILD_COMMIT "'+'f'*40+'"\n#define R2_SOURCE_DIRTY 0\n')
 (out/'r2_enrollment.h').write_text('static const struct r2_enrollment r2_enrollment={0};\n')
 reply=out/'reply_fixture.c';reply.write_text('#include "time_zone_config.h"\nvoid r2_time_position_snapshot(struct r2_position *out){*out=(struct r2_position){0};}\nvoid r2_time_tz_status(struct r2_tz_local_status *out,uint64_t now){(void)now;*out=(struct r2_tz_local_status){.position_age_us=UINT64_MAX,.confirmed_age_us=UINT64_MAX,.selected=65535,.mode=2,.reason=R2_TZ_UNSUPPORTED,.hold=true};}\n#include "'+str(APP/'tests/reply_test.c')+'"\n')
 replyflags=[f for f in flags if f not in ('-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1')]
 replycmd=[*replyflags,'-DR2_NATIVE_BLE=1','-I'+str(out),'-include',str(APP/'tests/zephyr_fake.h'),str(reply),str(APP/'src/core.c'),str(APP/'src/time_zone_selector.c'),str(APP/'src/time_zone_database.c'),'-o',str(out/'reply')]
 with (out/'reply-compile.log').open('w') as log:subprocess.run(replycmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 with (out/'reply-run.log').open('w') as log:subprocess.run([str(out/'reply')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 # Explicit manual-offset remains a fixed offset, including after caching.
 manual=out/'manual_fixture.c';manual.write_text('#include <assert.h>\n#include <string.h>\n#include "'+str(APP/'src/ui_render.c')+'"\nint main(void){struct r2_ui_model m={.sampled_ms=1000,.tz={2,-300},.tz_until_us=2000000};int64_t local;uint8_t f;const int64_t utc=1793659574000000LL;assert(!strcmp(r2_ui_clock_zone(&m,1000,utc,&local),"CDT")&&local==utc-18000000000LL);m.display_tz=m.tz;m.display_tz_source=R2_UI_TZ_LOCAL;m.tz=r2_tz_unknown();assert(!strcmp(r2_ui_clock_zone(&m,1000,utc,&local),"CDT"));(void)r2_ui_display_zone(&m,1000,utc,&f);assert((f&R2_UI_TZ_CACHED)&&!(f&R2_UI_TZ_CALENDAR));return 0;}\n')
 manualcmd=[*flags,'-DR2_TZ_MODE=0',str(manual),str(APP/'src/precision.c'),str(APP/'src/time_zone_database.c'),'-o',str(out/'manual')]
 with (out/'manual-compile.log').open('w') as log:subprocess.run(manualcmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 with (out/'manual-run.log').open('w') as log:subprocess.run([str(out/'manual')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 # Existing fresh/expired/unknown renderer behavior, with only the new blank
 # UTC presentation rule changed; both normal and LoRa leaf configurations.
 compat=[]
 for name,defines in [('normal',[]),('lora',['-DR2_LORA=1'])]:
  c=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_TZ_WIRE=1',*defines,'-I'+str(APP/'include'),str(APP/'tests/ui_time_zone_test.c'),str(APP/'src/precision.c'),'-o',str(out/('compat-'+name))]
  with (out/('compat-'+name+'-compile.log')).open('w') as log:subprocess.run(c,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  with (out/('compat-'+name+'-run.log')).open('w') as log:subprocess.run([str(out/('compat-'+name)),str(out)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  compat.append(c)
 files+=[APP/'protocol/lora_passive_schedule.h',APP/'protocol/lora_passive.c',APP/'include/ui.h',APP/'include/lora_service.h',APP/'include/lora_watchdog.h',APP/'src/ui_service.c',APP/'src/ui_render.c',APP/'src/lora_passive_service.c',APP/'src/gm7_service.c',APP/'src/main.c',APP/'tests/lora_ram_fixture.h',APP/'tests/ui_time_zone_pipeline_test.c',APP/'tests/ui_time_zone_test.c',APP/'src/service_switch.c',Path(__file__)]
 (out/'receipt.json').write_text(json.dumps(dict(status='PASS_SYNTHETIC_BOUNDARIES_ONLY',command=cmd,reply_command=replycmd,manual_command=manualcmd,pipeline_command=pipecmd,compat_commands=compat,hardware=False,authority='UNCHANGED',sources={str(f.relative_to(APP)):hashlib.sha256(f.read_bytes()).hexdigest()for f in files if f.is_relative_to(APP)}),indent=2)+'\n')
 print((out/'run.log').read_text())
if __name__=='__main__':main()
