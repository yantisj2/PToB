#!/usr/bin/env python3
"""Compile actual restart supervisor at synthetic RTOS boundaries; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
f=a.out/'fakes';(f/'freertos').mkdir(parents=True)
(f/'esp_attr.h').write_text('#pragma once\n#define RTC_NOINIT_ATTR\n')
(f/'esp_system.h').write_text('#pragma once\nenum {ESP_RST_POWERON=1,ESP_RST_SW=3};\nint esp_reset_reason();void esp_restart();\n')
(f/'esp_timer.h').write_text('#pragma once\n#include <cstdint>\nint64_t esp_timer_get_time();\n')
(f/'freertos/FreeRTOS.h').write_text((APP/'tests/lora_fakes/freertos/FreeRTOS.h').read_text()+'\nusing StackType_t=uint8_t;struct StaticTask_t{unsigned unused;};using TaskHandle_t=void*;\n')
(f/'freertos/task.h').write_text('#pragma once\n#include "FreeRTOS.h"\nvoid vTaskDelay(unsigned);TaskHandle_t xTaskCreateStaticPinnedToCore(void(*)(void*),const char*,unsigned,void*,unsigned,StackType_t*,StaticTask_t*,int);\n')
flags=[os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DESP_PLATFORM=1','-I'+str(f),'-I'+str(APP.parent/'w12/main')]
for n30 in (False,True):
 exe=a.out/('n30-supervisor' if n30 else 'w12-supervisor')
 subprocess.run([*flags,*(['-DPTOB_N30=1'] if n30 else []),APP/'tests/sync_watchdog_runtime_test.cpp','-o',exe],check=True,timeout=30)
 subprocess.run([exe],check=True,timeout=10)
