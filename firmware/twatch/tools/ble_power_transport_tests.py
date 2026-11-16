#!/usr/bin/env python3
"""Actual watch transport lifecycle with SDK fakes; no native builder/hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1];W12=APP.parent/'w12'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
f=out/'fakes';shutil.copytree(W12/'tests/console_fakes',f)
hs=f/'host/ble_hs.h';hs.write_text(hs.read_text().replace('inline int ble_hs_util_ensure_addr(unsigned){return 0;}','extern int fixture_ensure_rc; inline int ble_hs_util_ensure_addr(unsigned){return fixture_ensure_rc;}'))
(f/'freertos/task.h').write_text('''#pragma once
using TaskHandle_t=void*;
using StackType_t=unsigned;
void vTaskDelay(unsigned);void vTaskDelete(void*);void xTaskNotifyGive(TaskHandle_t);unsigned ulTaskNotifyTake(int,unsigned);
inline unsigned uxTaskGetStackHighWaterMark(void*){return 3000;}
int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*,unsigned);
''')
with (f/'freertos/FreeRTOS.h').open('a') as h:h.write('\n#define pdTRUE 1\n#define portMAX_DELAY 0xffffffff\n')
(f/'esp_bt.h').write_text('#pragma once\n#define ESP_BT_CONTROLLER_STATUS_IDLE 0\nint esp_bt_controller_get_status();\n')
(f/'nimble/nimble_port.h').write_text('''#pragma once
#include <cstdint>
#include "driver/usb_serial_jtag.h"
struct ble_npl_event {void(*fn)(ble_npl_event*)=nullptr;bool live=false,queued=false;};
struct ble_npl_callout {bool live=false,stopped=false,armed=false;};
inline void* nimble_port_get_dflt_eventq(){return nullptr;}
inline void ble_npl_event_init(ble_npl_event* e,void(*fn)(ble_npl_event*),void*){e->fn=fn;e->live=true;e->queued=false;}
void ble_npl_eventq_put(void*,ble_npl_event*);void ble_npl_eventq_remove(void*,ble_npl_event*);
inline void ble_npl_event_deinit(ble_npl_event* e){e->live=false;e->queued=false;}
inline void ble_npl_callout_init(ble_npl_callout* c,void*,void(*)(ble_npl_event*),void*){c->live=true;c->stopped=c->armed=false;}
inline void ble_npl_callout_reset(ble_npl_callout* c,uint32_t){c->armed=true;}
inline void ble_npl_callout_stop(ble_npl_callout* c){c->stopped=true;c->armed=false;}
inline void ble_npl_callout_deinit(ble_npl_callout* c){c->live=false;}
bool ble_npl_eventq_is_empty(void*);
inline uint32_t ble_npl_time_ms_to_ticks32(uint32_t n){return n;}
int nimble_port_init();int nimble_port_stop();int nimble_port_deinit();void nimble_port_run();
''')
cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
includes=[f,APP/'main',APP.parent/'n30/main',W12/'main',W12/'legacy',W12/'legacy/reference',W12/'main/r2/r2-g1',W12/'main/r2/rc7/include']
cmd=[cxx,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DPTOB_TWATCH=1','-DPTOB_N30=1','-DV12_R2_ENABLED=1','-DV12_R2_CONFIG=13','-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192','-DNIMBLE_CORE=0','-DconfigMAX_PRIORITIES=25',*['-I'+str(q)for q in includes],APP/'tests/ble_power_transport_test.cpp','-o',out/'transport']
for label,command in [('compile',cmd),('runtime',[out/'transport'])]:
 with (out/(label+'.log')).open('w') as h:subprocess.run(list(map(str,command)),check=True,stdout=h,stderr=subprocess.STDOUT,timeout=60)
sources=[APP/'main/ble_power.hpp',APP/'main/ble_power_transport.inc',W12/'main/native_transport.cpp',W12/'main/native_transport.hpp',APP/'tests/ble_power_transport_test.cpp',Path(__file__)]
(out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'ACTUAL_NATIVE_TRANSPORT_SDK_FAKES_ASAN_UBSAN','hardware_access':False,'sources':{str(q):hashlib.sha256(q.read_bytes()).hexdigest()for q in sources}},indent=2)+'\n');print((out/'runtime.log').read_text(),end='')
