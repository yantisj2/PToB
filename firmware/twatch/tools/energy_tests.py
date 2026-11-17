#!/usr/bin/env python3
"""Offline actual CPU/panel/console regressions; no hardware or native build.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1];W12=APP.parent/'w12'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all']
def execute(label,cmd):
 with (out/(label+'.log')).open('w') as log:subprocess.run(list(map(str,cmd)),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
execute('energy-compile',[cxx,*flags,'-I'+str(APP/'tests/energy_fakes'),'-I'+str(APP/'main'),APP/'main/energy.cpp',APP/'tests/energy_test.cpp','-o',out/'energy'])
execute('energy',[out/'energy'])
for failure in (1,2,4):execute('energy-failure-'+str(failure),[out/'energy',str(failure)])
f=out/'fakes';(f/'driver').mkdir(parents=True);(f/'freertos').mkdir()
(f/'driver/usb_serial_jtag.h').write_text('''#pragma once
#include <cstddef>
#define ESP_OK 0
using esp_err_t=int;
struct usb_serial_jtag_driver_config_t {unsigned rx_buffer_size=0,tx_buffer_size=0;};
inline int usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t*){return 0;}
inline int usb_serial_jtag_read_bytes(void*,size_t,unsigned){return 0;}
int usb_serial_jtag_write_bytes(const void*,size_t,unsigned);
int usb_serial_jtag_wait_tx_done(unsigned);
''')
(f/'freertos/FreeRTOS.h').write_text('#pragma once\n#include "'+str(W12/'tests/console_fakes/freertos/FreeRTOS.h')+'"\n#define pdTRUE 1\n#define portMAX_DELAY 0xffffffffu\n')
(f/'freertos/task.h').write_text('#pragma once\n#include "'+str(W12/'tests/console_fakes/freertos/task.h')+'"\nvoid xTaskNotifyGive(TaskHandle_t);unsigned ulTaskNotifyTake(int,unsigned);\n')
execute('console-compile',[cxx,*flags,'-DV12_R2_ENABLED=1',*['-I'+str(q)for q in (f,APP/'main',APP.parent/'n30/main',W12/'main',W12/'tests/console_fakes')],APP/'tests/console_power_test.cpp','-o',out/'console'])
execute('console',[out/'console'])
execute('control-generate',['python3',APP/'tools/control_vectors.py','generate','--out',out,'--cxx',cxx])
execute('control-compile',[cxx,*flags,'-DV12_R2_ENABLED=1',*['-I'+str(q)for q in (out,f,APP/'main',APP.parent/'n30/main',W12/'main',W12/'tests/console_fakes')],APP/'tests/control_frame_test.cpp','-o',out/'control'])
execute('control',[out/'control',out/'control-max.txt'])
execute('control-validate',['python3',APP/'tools/control_vectors.py','validate','--out',out])

files=[Path(__file__),APP/'main/energy.cpp',APP/'main/energy.hpp',APP/'main/panel_sleep.hpp',APP/'main/bounded_log.cpp',APP/'main/console_power.hpp',APP/'tests/energy_test.cpp',APP/'tests/console_power_test.cpp',APP/'tests/control_frame_test.cpp',APP/'main/control_frame.hpp',APP/'main/native_main.cpp',APP/'tools/control_vectors.py',*sorted((APP/'tests/energy_fakes').rglob('*.h'))]
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',hardware_access=False,scope='ACTUAL_ENERGY_AND_CONSOLE_IMPLEMENTATION_SDK_FAKES_ASAN_UBSAN',sources={str(q):hashlib.sha256(q.read_bytes()).hexdigest()for q in files}),indent=2)+'\n')
print((out/'energy.log').read_text().strip());print((out/'console.log').read_text().strip())
