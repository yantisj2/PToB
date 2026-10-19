#!/usr/bin/env python3
"""Explicit diagnostic build selection and real helper against fake driver calls.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
No device, firmware build, GNSS command or BLE clock training.
"""
import copy,os,re,subprocess,sys,tempfile
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(APP/'tools'))
from build import GNSS_PROFILE,gnss_map_diagnostic,validate_map_compilation
FAKES={'gptimer_priv.h': '#pragma once\n#include "driver/gptimer.h"\n', 'esp_memory_utils.h': '#pragma once\n#include "driver/gptimer.h"\n#define esp_ptr_internal(p) test_internal\n', 'driver/mcpwm_cap.h': '#pragma once\n#include "gptimer.h"\ntypedef struct channel* mcpwm_cap_channel_handle_t;\nesp_err_t mcpwm_capture_channel_trigger_soft_catch(mcpwm_cap_channel_handle_t);\n', 'driver/gptimer.h': '#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#include <stdatomic.h>\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG -1\n#define ESP_ERR_INVALID_STATE -2\n#define ESP_ERR_NOT_SUPPORTED -3\n#define IRAM_ATTR\n#define CONFIG_BT_CTRL_PINNED_TO_CORE 0\n#define GPTIMER_CLK_SRC_XTAL 1\n#define GPTIMER_COUNT_UP 1\n#define GPTIMER_FSM_RUN 2\ntypedef unsigned UBaseType_t;\ntypedef struct timer {int spinlock;unsigned resolution_hz,clk_src,direction;atomic_int fsm;struct {void*dev;unsigned timer_id;} hal;} *gptimer_handle_t;\nextern unsigned test_core,test_irq,test_lock,test_calls,test_capture,test_mcpwm,test_rom;\nextern bool test_internal;\nextern int test_error;\nunsigned test_save(void);void test_restore(unsigned);void test_enter(int*);void test_exit(int*);\nvoid timer_ll_trigger_soft_capture(void*,unsigned);uint64_t timer_ll_get_counter_value(void*,unsigned);\n#define xPortGetCoreID() test_core\n#define portENTER_CRITICAL_SAFE(p) test_enter(p)\n#define portEXIT_CRITICAL_SAFE(p) test_exit(p)\n#define portSET_INTERRUPT_MASK_FROM_ISR() test_save()\n#define portCLEAR_INTERRUPT_MASK_FROM_ISR(s) test_restore(s)\n'}
FAST_PROGRAM='// Executes the exact inert candidate C helper against fake driver primitives.\n#include <assert.h>\n#include <stdio.h>\n#include "driver/mcpwm_cap.h"\n#include "s3_clock_reference.c"\nunsigned test_core=0,test_irq=7,test_lock=0,test_calls=0,test_capture=0,test_mcpwm=0,test_rom=0;\nbool test_internal=true;int test_error=0;\nunsigned test_save(void){++test_calls;unsigned s=test_irq;test_irq=15;return s;}\nvoid test_restore(unsigned s){assert(test_lock==0);++test_calls;test_irq=s;}\nvoid test_enter(int*p){(void)p;assert(test_irq==15);assert(test_lock==0);test_lock=1;++test_calls;}\nvoid test_exit(int*p){(void)p;assert(test_lock==1);test_lock=0;++test_calls;}\nvoid timer_ll_trigger_soft_capture(void*p,unsigned id){(void)p;(void)id;assert(test_lock==1);++test_capture;++test_calls;}\nuint64_t timer_ll_get_counter_value(void*p,unsigned id){(void)p;(void)id;assert(test_lock==1);++test_calls;return 100+10*test_capture;}\nesp_err_t mcpwm_capture_channel_trigger_soft_catch(mcpwm_cap_channel_handle_t ch){assert(ch);assert(!test_lock);assert(test_irq==15);assert(test_capture==1);++test_mcpwm;++test_calls;return test_error;}\nw12_ble_time_t r_rwip_time_get(void){assert(!test_lock);assert(test_irq==15);++test_rom;return (w12_ble_time_t){1,2};}\nstatic void reset(void){test_irq=7;test_lock=test_calls=test_capture=test_mcpwm=test_rom=0;test_error=0;}\nint main(void){\n struct timer timer={.resolution_hz=20000000,.clk_src=1,.direction=1,.fsm=2};\n mcpwm_cap_channel_handle_t ch=(mcpwm_cap_channel_handle_t)&timer;uint64_t before=1,after=2;\n assert(w12_s3_gnss_map_bracket(&timer,ch,&before,&after)==0);\n assert(before==110&&after==120&&test_irq==7&&!test_lock&&test_capture==2&&test_mcpwm==1&&!test_rom);\n reset();test_error=-44;assert(w12_s3_gnss_map_bracket(&timer,ch,&before,&after)==-44);\n assert(before==110&&after==120&&test_irq==7&&!test_lock&&test_capture==2&&test_mcpwm==1);\n reset();assert(w12_s3_gnss_map_bracket(NULL,ch,&before,&after)==-1&&!test_calls);\n assert(w12_s3_gnss_map_bracket(&timer,NULL,&before,&after)==-1&&!test_calls);\n assert(w12_s3_gnss_map_bracket(&timer,ch,NULL,&after)==-1&&!test_calls);\n assert(w12_s3_gnss_map_bracket(&timer,ch,&before,NULL)==-1&&!test_calls);\n#define BAD(statement,restore) do{reset();statement;before=1;after=2;assert(w12_s3_gnss_map_bracket(&timer,ch,&before,&after)==-2);assert(!before&&!after&&!test_calls&&test_irq==7);restore;}while(0)\n BAD(test_core=1,test_core=0);BAD(test_internal=false,test_internal=true);\n BAD(timer.resolution_hz=10000000,timer.resolution_hz=20000000);\n BAD(timer.clk_src=2,timer.clk_src=1);BAD(timer.direction=0,timer.direction=1);BAD(timer.fsm=0,timer.fsm=2);\n reset();w12_ble_time_t t;assert(!w12_s3_ble_time_bracket(&timer,&before,&after,&t));\n assert(test_rom==1&&!test_mcpwm&&test_capture==2&&test_irq==7&&t.half_slots==1&&t.half_us==2);\n puts("{\\"status\\":\\"PASS_INERT_FAST_BRACKET_FAKE_CONTROLS\\",\\"invalid_argument_cases\\":4,\\"invalid_timer_cases\\":6,\\"public_trigger_failure_restores_IRQ\\":true,\\"no_GPTimer_lock_over_MCPWM\\":true,\\"existing_BLE_bracket_pass\\":true,\\"native_build\\":false,\\"hardware\\":false}");\n}\n'
OFF_PROGRAM='// Executes the exact inert candidate C helper against fake driver primitives.\n#include <assert.h>\n#include <stdio.h>\n#include "driver/mcpwm_cap.h"\n#include "s3_clock_reference.c"\nunsigned test_core=0,test_irq=7,test_lock=0,test_calls=0,test_capture=0,test_mcpwm=0,test_rom=0;\nbool test_internal=true;int test_error=0;\nunsigned test_save(void){++test_calls;unsigned s=test_irq;test_irq=15;return s;}\nvoid test_restore(unsigned s){assert(test_lock==0);++test_calls;test_irq=s;}\nvoid test_enter(int*p){(void)p;assert(test_irq==15);assert(test_lock==0);test_lock=1;++test_calls;}\nvoid test_exit(int*p){(void)p;assert(test_lock==1);test_lock=0;++test_calls;}\nvoid timer_ll_trigger_soft_capture(void*p,unsigned id){(void)p;(void)id;assert(test_lock==1);++test_capture;++test_calls;}\nuint64_t timer_ll_get_counter_value(void*p,unsigned id){(void)p;(void)id;assert(test_lock==1);++test_calls;return 100+10*test_capture;}\nesp_err_t mcpwm_capture_channel_trigger_soft_catch(mcpwm_cap_channel_handle_t ch){assert(ch);assert(!test_lock);assert(test_irq==15);assert(test_capture==1);++test_mcpwm;++test_calls;return test_error;}\nw12_ble_time_t r_rwip_time_get(void){assert(!test_lock);assert(test_irq==15);++test_rom;return (w12_ble_time_t){1,2};}\nstatic void reset(void){test_irq=7;test_lock=test_calls=test_capture=test_mcpwm=test_rom=0;test_error=0;}\nint main(void){\n struct timer timer={.resolution_hz=20000000,.clk_src=1,.direction=1,.fsm=2};uint64_t before=0,after=0;w12_ble_time_t t;reset();\n assert(!w12_s3_ble_time_bracket(&timer,&before,&after,&t));\n assert(before==110&&after==120&&test_rom==1&&!test_mcpwm&&test_capture==2&&test_irq==7&&!test_lock);\n puts("PASS default-off BLE bracket retains original query and IRQ behavior");\n}\n'

def refuses(fn):
 try:fn()
 except ValueError:return
 raise AssertionError('Invalid diagnostic configuration accepted')
d={'board':'meshnology_w12_esp32s3','gnss_profile':GNSS_PROFILE}
assert gnss_map_diagnostic(d,True) and not gnss_map_diagnostic(d,False)
for bad in (0,1,None,'true'):refuses(lambda:gnss_map_diagnostic(d,bad))
for bad in ({'board':'meshnology_w12_esp32s3','label':'C-3'},{**d,'gnss_profile':'auto'},{**d,'board':'other'}):refuses(lambda:gnss_map_diagnostic(bad,True))
flags=['-DPTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC=1','-DV12_R2_RX_SHADOW=1','-DW12_COPY_BOARD_PROFILE=12','-DW12_GNSS_VERIFIED_INPUTS=1','-DW12_GNSS_W12_POWER=1','-DW12_GNSS_RX_PIN=39','-DW12_GNSS_PPS_PIN=41','-DW12_GNSS_BAUD=9600']
rows=[{'file':'/app/'+n,'command':'cc '+' '.join(flags)} for n in ('native_main.cpp','gnss_pps.cpp','s3_clock_reference.c')]
r={'gnss_fast_map_diagnostic':True};config='CONFIG_BT_CTRL_PINNED_TO_CORE=0\nCONFIG_ESP_MAIN_TASK_AFFINITY_CPU0=y\nCONFIG_ESP_MAIN_TASK_AFFINITY=0x0\n'
assert validate_map_compilation(r,d,rows,config)
for i in range(3):
 for token in flags:
  bad=copy.deepcopy(rows);bad[i]['command']=bad[i]['command'].replace(token,'')
  refuses(lambda:validate_map_compilation(r,d,bad,config))
for token in flags:
 bad=copy.deepcopy(rows);bad[0]['command']+=' '+token.split('=',1)[0]+'=999';refuses(lambda:validate_map_compilation(r,d,bad,config))
refuses(lambda:validate_map_compilation(r,d,rows+rows[:1],config))
refuses(lambda:validate_map_compilation(r,d,rows[:-1],config))
for value in (None,0,1):refuses(lambda:validate_map_compilation({'gnss_fast_map_diagnostic':value},d,rows,config))
for line in config.splitlines():refuses(lambda:validate_map_compilation(r,d,rows,config.replace(line+'\n','')))
refuses(lambda:validate_map_compilation(r,d,rows,config+config))
plain=[{'file':x['file'],'command':x['command'].replace('FAST_MAP_DIAGNOSTIC=1','FAST_MAP_DIAGNOSTIC=0')} for x in rows]
assert not validate_map_compilation({'gnss_fast_map_diagnostic':False},{'board':'meshnology_w12_esp32s3'},plain,config)
CC=os.environ.get('CC','cc');CXX=os.environ.get('CXX','c++')
with tempfile.TemporaryDirectory() as tmp:
 temp=Path(tmp)
 for name,text in FAKES.items():
  f=temp/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(text)
 common=[CC,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(temp),'-I'+str(APP/'main')]
 defs=['-DV12_R2_RX_SHADOW=1','-DW12_GNSS_VERIFIED_INPUTS=1','-DW12_COPY_BOARD_PROFILE=12']
 for enabled,program in ((0,OFF_PROGRAM),(1,FAST_PROGRAM)):
  f=temp/('control'+str(enabled)+'.c');f.write_text(program);binary=temp/('control'+str(enabled))
  subprocess.run([*common,*defs,'-DPTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC='+str(enabled),str(f),'-o',str(binary)],check=True,timeout=20)
  subprocess.run([str(binary)],check=True,timeout=10)
 source=str(APP/'main/s3_clock_reference.c')
 for extra in (['-DPTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC=1','-DW12_GNSS_VERIFIED_INPUTS=1','-DW12_COPY_BOARD_PROFILE=12'],['-DPTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC=1','-DV12_R2_RX_SHADOW=1','-DW12_GNSS_VERIFIED_INPUTS=0','-DW12_COPY_BOARD_PROFILE=12'],['-DPTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC=1','-DV12_R2_RX_SHADOW=1','-DW12_GNSS_VERIFIED_INPUTS=1','-DW12_COPY_BOARD_PROFILE=2']):
  q=subprocess.run([*common,*extra,'-fsyntax-only',source],capture_output=True,text=True,timeout=20)
  assert q.returncode!=0 and '#error' in q.stderr
 default=subprocess.check_output([*common,*defs,'-E','-P',source],text=True,timeout=20)
 explicit_off=subprocess.check_output([*common,*defs,'-DPTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC=0','-E','-P',source],text=True,timeout=20)
 assert default==explicit_off and 'w12_s3_gnss_map_bracket' not in default
 stripped=re.sub(r'^\s*#include .*$', '',(APP/'legacy/gnss_pps.cpp').read_text(),flags=re.M)
 def pp(enabled):return subprocess.check_output([CXX,'-E','-P','-x','c++','-'],input='#define PTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC '+str(enabled)+'\n'+stripped,text=True,timeout=20)
 slow=pp(0);fast=pp(1)
 sequence='before=now();result=mcpwm_capture_channel_trigger_soft_catch(map_channel);after=now();'
 assert slow.count(sequence)==2 and 'w12_s3_gnss_map_bracket' not in slow
 assert fast.count(sequence)==1 and fast.count('result=w12_s3_gnss_map_bracket(clock,map_channel,&before,&after);')==1
 assert 'FAST_DRIVER_LATCH_DIAGNOSTIC' in fast and 'FAST_DRIVER_LATCH_DIAGNOSTIC' not in slow
print('PASS explicit profile/metadata/three compiled units/core gates, feature-off source behavior, guarded fast helper and IRQ/error/refusal controls')

# Bound every modified map-treatment log format at ESP32 32-bit-long widths.
from status_budget_test import formats,render
log=(APP/'main/bounded_log.cpp').read_text();prefix=log[log.index('const int prefix=std::snprintf'):log.index('if(prefix<=0')];prefix=prefix.replace('std::snprintf(line.bytes,sizeof(line.bytes),','prefix_format(')
defs='#define PRIu64 "llu"\n#define PRId64 "lld"\n#define PRIx64 "llx"\n#define PRIu32 "u"\n#define PRId32 "d"\n#define PRIx32 "x"\n'
pp=subprocess.check_output([CXX,'-E','-P','-x','c++','-'],input=defs+prefix,text=True,timeout=20);pfs=formats(pp,'prefix_format');assert len(pfs)==1
capacity=int(re.search(r'char bytes\[(\d+)\]',log)[1]);assert capacity==1536
fs=[f for f in formats((APP/'legacy/gnss_pps.cpp').read_text(),'WC12_OBSERVER_PRINTF') if ' treatment=%s ' in f];assert len(fs)==4;sizes=[]
for f in fs:
 rendered=render(f.replace('%lu','%u').replace('%ld','%d'),True);n=len(render(pfs[0],True))+len(rendered);assert n<capacity and rendered.endswith(b'\n');sizes.append(n)
assert re.search(r'ConsoleFrameBytes=8704\b',(APP/'main/console_protocol.hpp').read_text())
print('PASS actual four map-treatment log formats with 64-byte text bounds, maximum',max(sizes),'of',capacity,'bytes including prefix; whole console frame8704 unchanged')
