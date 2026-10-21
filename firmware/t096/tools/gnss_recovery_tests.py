#!/usr/bin/env python3
"""Actual GNSS latch to production watchdog recovery, fake public boundaries.
MIT. Copyright (c) 2026 Jonathan Yantis.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,shutil,os
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);args=p.parse_args()
app=Path(__file__).resolve().parents[1];root=app.parents[1];out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
paths=[app/'src/main.c',app/'src/gnss.c',app/'src/watchdog.c',app/'tests/gnss_test.c',app/'tests/watchdog_test.c']
before={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
main=(app/'src/main.c').read_text();begin=main.index('    /* A latched GNSS driver failure');end=main.index('#endif',begin)
hook=main[begin:end]
assert main[begin-len('#ifndef R2_PC_PROFILE\n'):begin]=='#ifndef R2_PC_PROFILE\n'
for n in ['zephyr/kernel.h','zephyr/device.h','zephyr/drivers/gpio.h','zephyr/drivers/uart.h','zephyr/sys/atomic.h','zephyr/sys/reboot.h','zephyr/drivers/watchdog.h','zephyr/drivers/hwinfo.h','cmsis_core.h','build_identity.h']:
 p=out/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('/* Existing fixture supplies public boundary. */\n')
base=[os.environ.get('CC') or ('/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').exists() else shutil.which('clang')),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-I'+str(out),'-I'+str(app/'tests'),'-I'+str(app/'include'),'-I'+str(app/'protocol')]
g=(app/'tests/gnss_test.c').read_text().split('int main(',1)[0].replace('../src/gnss.c',str(app/'src/gnss.c'))
g+='''int main(void){
 assert(!r2_gnss_start());now=10000;r2_gnss_poll(now);pending_worker(NULL,NULL,NULL);
 /* Healthy UART with neither RX nor PPS for hours is not a driver error. */
 now+=8LL*3600*1000000;r2_gnss_poll(now);assert(!r2_gnss_input_error()&&!faults);
 const char *void_rmc="$GPRMC,000000.00,V,,,,,,,010126,,,N*00\\r\\n";
 memcpy(rx[0],void_rmc,strlen(void_rmc));event(UART_RX_RDY,rx[0],0,strlen(void_rmc));
 r2_gnss_poll(++now);assert(!r2_gnss_input_error()&&!faults);
 event(UART_RX_STOPPED,NULL,0,0);assert(r2_gnss_input_error()==-EIO);
 r2_gnss_poll(++now);assert(disable_calls==1&&atomic_get(&used[0])&&levels[1]==1);
 /* Missing callbacks never release DMA or rails. */
 now+=60000000;r2_gnss_poll(now);assert(atomic_get(&used[0])&&levels[1]==1);
 FILE *f=fopen("gnss-error.txt","w");assert(f);fprintf(f,"%d",r2_gnss_input_error());fclose(f);
 event(UART_RX_DISABLED,NULL,0,0);r2_gnss_poll(++now);assert(atomic_get(&used[0])&&levels[1]==1);
 event(UART_RX_BUF_RELEASED,rx[0],0,0);r2_gnss_poll(++now);assert(!atomic_get(&used[0])&&levels[1]==0&&r2_gnss_input_error()==-EIO);
 puts("PASS actual GNSS latch, silence/PPS absence, byte admission, callback ownership");
}
'''
(out/'gnss-chain.c').write_text(g)
w=(app/'tests/watchdog_test.c').read_text().split('int main(',1)[0].replace('../src/watchdog.c',str(app/'src/watchdog.c'))
w+='''static int supplied_error;static bool ui_off;
static int r2_gnss_input_error(void){return supplied_error;}
bool r2_ui_poweroff_requested(void){return ui_off;}
#define R2_IDLE 0
static void proposed_hook(bool normal_started,struct r2_service_status service,int phase,bool poweroff_requested){
 struct {int phase;} maintenance={phase};
'''+hook+'''}
int main(void){
 FILE *f=fopen("gnss-error.txt","r");assert(f&&fscanf(f,"%d",&supplied_error)==1);fclose(f);assert(supplied_error==-EIO);
 for(unsigned excluded=0;excluded<8;excluded++){
  clear_runtime();assert(!r2_watchdog_boot(&id));advance(100);
  struct r2_service_status s={.normal=true,.active=true,.at=100000};
  bool started=true,poweroff=false;int phase=0;ui_off=false;
  if(excluded==1)started=false;if(excluded==2)s.normal=false;if(excluded==3)s.profile_rc=true;
  if(excluded==4)s.profile_pending=true;if(excluded==5)phase=1;if(excluded==6)poweroff=true;if(excluded==7)ui_off=true;
  proposed_hook(started,s,phase,poweroff);
  if(excluded){assert(!restart_error&&step_at(100));}
  else {assert(!step_at(100)&&reset_reason==WD_RECOVERY_FAILED&&previous_bound(&id));assert((int32_t)r2_watchdog_capsule[32]==supplied_error);}
 }
 clear_runtime();assert(!r2_watchdog_boot(&id));advance(100);ui_off=false;supplied_error=0;
 struct r2_service_status s={.normal=true,.active=true,.at=100000};proposed_hook(true,s,0,false);assert(!restart_error&&step_at(100));
 puts("PASS actual watchdog terminal/capsule path, exact proposed hook, seven exclusions and zero latch");
}
'''
(out/'watchdog-chain.c').write_text(w)
commands=[base+['-include',str(app/'tests/gnss_fake.h'),str(out/'gnss-chain.c'),'-o',str(out/'gnss-chain')],[str(out/'gnss-chain')],base+[str(out/'watchdog-chain.c'),str(app/'src/watchdog_policy.c'),'-o',str(out/'watchdog-chain')],[str(out/'watchdog-chain')]]
with (out/'test.log').open('x') as log:
 for c in commands:subprocess.run(c,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
after={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths};assert before==after
(out/'receipt.json').write_text(json.dumps({'passed':True,'sources_unchanged':before==after,'source_hashes':before,'limits':['Public UART and WDT boundaries faked; no physical reset or native build tested.','VOID bytes exercise adapter admission, not NMEA parser. No source qualification decision added.','Hook extracted verbatim into fixture; full main scheduler not executed.','Capsule retains errno, not unique GNSS caller origin.'],'commands':commands},indent=2)+'\n')
print((out/'test.log').read_text())
