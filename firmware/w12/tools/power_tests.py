#!/usr/bin/env python3
"""Native button/shutdown production code with SDK fakes; never opens hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
A=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
flags=[os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(A/'tests/power_fakes'),'-I'+str(A/'main'),'-I'+str(A/'legacy')]
commands=[]
def run(sources,name,defines=()):
 cmd=[*flags,*defines,*map(str,sources),'-o',str(out/name)];commands.append(cmd);subprocess.run(cmd,check=True);subprocess.run([out/name],check=True)
run([A/'tests/power_button_test.cpp'],'button')
s=(A/'main/native_main.cpp').read_text()
start=s.index('    const auto action=buttons.update(');end=s.index('    v12::Report row;',start)
(out/'actual_button_dispatch.inc').write_text(s[start:end])
start=s.index('    if(power_off.ready(');end=s.index('    bool update_ui=',start)
(out/'actual_power_dispatch.inc').write_text(s[start:end])
run([A/'tests/power_dispatch_test.cpp'],'dispatch',['-I'+str(out),'-DV12_R2_ENABLED=1','-DV12_R2_CONFIG=13','-DPTOB_W12_LORA_RX=1'])
for gnss in (0,1):
 for lora in (0,1):run([A/'tests/power_control_test.cpp',A/'main/power_off.cpp'],f'power-g{gnss}-l{lora}',[f'-DW12_GNSS_W12_POWER={gnss}',f'-DW12_COPY_BOARD_PROFILE={12 if gnss else 2}',*(['-DPTOB_W12_LORA_RX=1'] if lora else [])])
paths=[A/'main/w12_button.hpp',A/'main/power_off.hpp',A/'main/power_off.cpp',A/'main/native_main.cpp',A/'main/update_gate.hpp',A/'tests/power_button_test.cpp',A/'tests/power_control_test.cpp',A/'tests/power_dispatch_test.cpp',*sorted((A/'tests/power_fakes').rglob('*.h')),Path(__file__)]
(out/'receipt.json').write_text(json.dumps(dict(status='PASS_NATIVE_BUTTON_AND_POWER_OFF_ASAN_UBSAN',hardware_access=False,commands=commands,sources={str(x.relative_to(A)):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}),indent=2)+'\n')
