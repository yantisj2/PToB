"""Actual main/start/time-start/capture bodies, fake public driver boundaries.
No hardware operations. Copyright (c) 2026 Jonathan Yantis. MIT.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
app=Path(__file__).resolve().parents[1];root=app.parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);args=p.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
source={n:(app/n).read_text()for n in ['src/main.c','src/time_service.c','src/capture.c','src/gm7_service.c','tests/gm7_service_test.c','tests/capture_fake.h','tests/gm7_startup_test.c']}
# Exact main startup branch, no copied policy reimplementation.
main=source['src/main.c'];a=main.index('    if(platform_ready&&!source_started)');b=main.index('    if(now>=ui_due)',a);block=main[a:b]
time=source['src/time_service.c'];a=time.index('void r2_time_start(void)');b=time.index('\nvoid r2_time_service',a);time_start=time[a:b]
for n in ['nrfx_timer.h','nrfx_ppi.h','nrfx_gpiote.h','radio_nrf5_resources.h','zephyr/kernel.h','zephyr/sys/atomic.h','zephyr/sys/onoff.h','zephyr/drivers/clock_control/nrf_clock_control.h','zephyr/drivers/gpio.h','hal/nrf_radio.h','zephyr/bluetooth/bluetooth.h','zephyr/bluetooth/conn.h','zephyr/bluetooth/gatt.h','zephyr/random/random.h']:
 p=out/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('/* Public driver fake boundary. */\n')
fake=source['tests/capture_fake.h'];fake=fake.replace('assert(cc<=1||cc==4)','assert(cc<=1||cc==4||cc==5)').replace('(ch==8&&event==5680&&task==1238)','(ch==8&&((event==5680&&task==1238)||(event==6790&&task==1239)))').replace('#define NRF_RADIO_EVENT_ADDRESS 1','#define NRF_RADIO_EVENT_ADDRESS 1\n#define NRF_RADIO_EVENT_END 2').replace('assert(r==NRF_RADIO&&event==1);return 6789;','assert(r==NRF_RADIO&&(event==1||event==2));return event==1?6789:6790;')
fake=fake.replace('static struct onoff_manager mgr;', 'static struct onoff_manager mgr;static unsigned request_count;').replace('(void)m;(void)c;return 0;}', '(void)m;(void)c;++request_count;return 0;}')
(out/'capture-fake.h').write_text(fake)
rename='\n'.join('#define '+n+' async_'+n for n in ['r2_capture_start','r2_capture_poll','r2_capture_pps','r2_capture_now','r2_capture_now_at','r2_capture_radio','r2_capture_lora_attach','r2_capture_lora','r2_capture_lora_detach','r2_capture_stop','r2_capture_error'])
cap='''#include "capture-fake.h"
'''+rename+f'\n#include "{app}/src/capture.c"\n'+'''
extern void probe_hf_published(bool valid);
void r2_gm7_clock_begin(uint32_t epoch){assert(epoch==1);}
void r2_gm7_clock_publish(uint64_t tick,uint32_t raw,uint64_t epoch,bool valid){(void)tick;(void)raw;assert(epoch==1);probe_hf_published(valid);}
void r2_gm7_clock_invalidate(void){probe_hf_published(false);}
int r2_gm7_capture_enable(uint32_t epoch,unsigned address,unsigned end){assert(epoch==1&&address==6&&end==8);return 0;}
int r2_gm7_capture_disable(void){return 0;}
bool r2_gm7_capture_busy(void){return false;}
int probe_async_begin(int64_t now){struct gpio_dt_spec p={&gpio1,11};fake_now=now;return r2_capture_start(&p,now);}
void probe_async_poll(int64_t now){fake_now=now;raw_now=(uint32_t)((uint64_t)now*16);r2_capture_poll(now);}
bool probe_async_pending(void){return pending&&!atomic_get(&accepting);}
int probe_async_error(void){return r2_capture_error();}
int probe_async_stop(void){return r2_capture_stop();}
void probe_async_failure(unsigned value){failure=value;}
unsigned probe_async_requests(void){return request_count;}
''';(out/'capture.c').write_text(cap)
timeunit='''#include "capture-fake.h"
#include "time_service.h"
static bool started,accepting;static struct k_spinlock lock;
extern int probe_async_begin(int64_t now);
int r2_gnss_start(void){return probe_async_begin(r2_time_now_us());}
void r2_time_input_fault(enum r2_clock_fault fault){assert(!fault);}
'''+time_start+'\n';(out/'time.c').write_text(timeunit)
(out/'gm7_startup_main_under_test.h').write_text(block)
cc=os.environ.get('CC','clang')
common=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_GM7_NATIVE=1','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol')]
commands=[]
for n in ['capture','time']:
 cmd=common+['-c',str(out/(n+'.c')),'-o',str(out/(n+'.o'))];commands.append(cmd)
 with (out/(n+'.compile.log')).open('w')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
tiny=args.deps.resolve()/'modules/crypto/tinycrypt/lib'
sources=[app/'protocol'/n for n in ['gm7_codec.c','gm7_control.c','gm7_owner.c','gm7_schedule.c','gm7_air.c','gm7_bridge.c','gm7_promote.c','group_auth.c','gnss_diagnostics.c']]+[app/'src'/n for n in ['gm7_source.c','precision.c','ui_render.c']]+[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]+[app/'tests/gm7_startup_test.c']
cmd=common+['-DR2_GM5_NATIVE=1','-DR2_NATIVE_BLE=1','-DR2_SWITCHABLE_FIELD=1','-I'+str(app/'tests'),'-I'+str(tiny/'include'),*map(str,sources),str(out/'capture.o'),str(out/'time.o'),'-o',str(out/'startup-test')];commands.append(cmd)
with (out/'startup.compile.log').open('w')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
with (out/'startup.run.log').open('w')as f:
 for mode in range(15):subprocess.run([str(out/'startup-test'),str(mode)],stdout=f,stderr=subprocess.STDOUT,check=True)
receipt={'status':'PASS','hardware':False,'firmware_edited':False,'actual_bodies':['main startup branch','gm7_service start/run','time_service time_start','capture start/poll'],'fake_boundaries':['GPIO/nrfx/PPI/onoff HFXO','GNSS power/UART entry','GM7 clock publication consumer','BLE/kernel/PT public test boundaries'],'sources':{n:hashlib.sha256(s.encode()).hexdigest()for n,s in source.items()},'commands':commands,'main_start_block_sha256':hashlib.sha256(block.encode()).hexdigest(),'target_ESTALE':'SDK ARM/newlib sys/errno.h:168 ESTALE133; host test compares symbolic ESTALE','cases':15,'no_hardware_retry':True}
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({'status':receipt['status'],'out':str(out)}))
