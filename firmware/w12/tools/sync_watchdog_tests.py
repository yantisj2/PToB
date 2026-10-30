#!/usr/bin/env python3
"""Compile actual restart supervisor at synthetic RTOS boundaries; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
f=a.out/'fakes';(f/'freertos').mkdir(parents=True)
(f/'esp_attr.h').write_text('#pragma once\n#define RTC_NOINIT_ATTR\n')
(f/'esp_system.h').write_text('#pragma once\nenum {ESP_RST_POWERON=1,ESP_RST_SW=3};\nint esp_reset_reason();void esp_restart();\n')
(f/'esp_timer.h').write_text('#pragma once\n#include <cstdint>\nint64_t esp_timer_get_time();\n')
(f/'freertos/FreeRTOS.h').write_text((APP.parent/'n30/tests/lora_fakes/freertos/FreeRTOS.h').read_text()+'\nusing StackType_t=uint8_t;struct StaticTask_t{unsigned unused;};using TaskHandle_t=void*;\n')
(f/'freertos/task.h').write_text('#pragma once\n#include "FreeRTOS.h"\nvoid vTaskDelay(unsigned);TaskHandle_t xTaskCreateStaticPinnedToCore(void(*)(void*),const char*,unsigned,void*,unsigned,StackType_t*,StaticTask_t*,int);\n')
flags=[os.environ.get('CXX','clang++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DESP_PLATFORM=1','-I'+str(f),'-I'+str(APP/'main'),'-I'+str(APP/'main/r2/r2-g1'),'-I'+str(APP/'main/r2/rc7/include')]
for name,define,source in [('legacy-shared',[],APP.parent/'n30/tests/sync_watchdog_runtime_test.cpp'),('w12-sync',['-DPTOB_W12_SYNC_WATCHDOG=1'],APP/'tests/sync_watchdog_runtime_test.cpp'),('n30-sync',['-DPTOB_N30=1'],APP.parent/'n30/tests/sync_watchdog_runtime_test.cpp')]:
 exe=a.out/name
 subprocess.run([*flags,*define,source,'-o',exe],check=True,timeout=30)
 subprocess.run([exe],check=True,timeout=10)
# A future short watchdog must be rejected by the shared policy itself.
short=a.out/'too-short.cpp';short.write_text('#include "fault_watchdog_model.hpp"\n v12::ContinuousFaultWatchdogModel<4999999> forbidden;\n')
negative=subprocess.run([*flags,short,'-o',a.out/'too-short'],capture_output=True,text=True,timeout=30)
assert negative.returncode!=0 and 'at least five seconds' in negative.stderr
exe=a.out/'adapter'
subprocess.run([*flags,'-DPTOB_W12_LORA_RX=1',APP/'tests/sync_watchdog_adapter_test.cpp','-o',exe],check=True,timeout=30)
subprocess.run([exe],check=True,timeout=10)
sys.path.insert(0,str(APP/'tools'))
from build import validate_watchdog_config
policy={'watchdog_policy':'ALL_MIN5S_STALL5S_FAULT120S_NOTIME300S_V2'}
valid='CONFIG_ESP_INT_WDT_TIMEOUT_MS=5000\nCONFIG_ESP_TASK_WDT_TIMEOUT_S=5\nCONFIG_BOOTLOADER_WDT_TIME_MS=9000\n'
validate_watchdog_config(policy,valid)
for name,value in [('ESP_INT_WDT_TIMEOUT_MS','4999'),('ESP_TASK_WDT_TIMEOUT_S','4'),('BOOTLOADER_WDT_TIME_MS','4999')]:
 import re
 bad=re.sub(r'CONFIG_'+name+r'=[0-9]+','CONFIG_'+name+'='+value,valid)
 try:validate_watchdog_config(policy,bad)
 except ValueError:pass
 else:raise AssertionError('Fast SDK watchdog accepted: '+name)
import hashlib,json
paths=[APP/'sdkconfig.defaults',APP/'tools/build.py',APP/'main/fault_watchdog.cpp',APP/'main/fault_watchdog.hpp',APP/'main/fault_watchdog_model.hpp',APP/'main/sync_watchdog.hpp',APP/'tests/sync_watchdog_runtime_test.cpp',APP/'tests/sync_watchdog_adapter_test.cpp',Path(__file__)]
(a.out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'ACTUAL_300S_SUPERVISOR_RESET_RTC_AND_CURRENT_TIME_ADAPTER_ASAN_UBSAN','hardware_access':False,'sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}},indent=2)+'\n')
