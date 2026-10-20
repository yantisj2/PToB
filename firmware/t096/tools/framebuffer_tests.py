#!/usr/bin/env python3
"""Production indexed/RGB565 rendering and bounded synchronous display checks.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

app=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=Path,required=True)
args=p.parse_args()
out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
compiler=os.environ.get('CC') or ('/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').is_file() else shutil.which('clang'))
flags=[compiler,'-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
       '-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-I'+str(app/'include')]
def run(cmd):
    subprocess.run(cmd,check=True,timeout=60)

# Exercise the retained original RGB565 pixel sink without a test branch in
# production. Remove only the indexed sink definitions, keeping drawing/policy
# bodies identical. This compares representation, not hardware qualification.
source=(app/'src/ui_render.c').read_text()
for line in ['#define UI_FRAME_BYTES R2_UI_FRAME_BYTES\n',
             '#define UI_COLOR(name) R2_UI_COLOR_##name\n',
             '#define UI_PIXEL_WRITE(fb,offset,color) ((fb)[offset]=(uint8_t)(color))\n']:
    if source.count(line)!=1:raise ValueError('Indexed pixel-sink boundary changed')
    source=source.replace(line,'')
source=source.replace('"../legacy_ui/draw.inc"','"'+str(app/'legacy_ui/draw.inc')+'"')
reference=out/'rgb565_reference.c';reference.write_text(source)
public=re.findall(r'^(?:void|bool|ui_pulse_t|const char \*)\s*(\w+)\(',
                  source+'\n'+(app/'legacy_ui/draw.inc').read_text(),re.M)
run([*flags,*['-D'+name+'=rgb565_reference_'+name for name in public],
     '-c',str(reference),'-o',str(out/'reference.o')])
run([*flags,str(app/'tests/ui_framebuffer_test.c'),str(app/'src/ui_render.c'),
     str(app/'src/precision.c'),str(out/'reference.o'),'-o',str(out/'framebuffer')])
run([str(out/'framebuffer')])
for name in ['kernel.h','drivers/gpio.h','drivers/spi.h','drivers/sensor.h',
             'drivers/pwm.h','drivers/clock_control/nrf_clock_control.h']:
    header=out/'zephyr'/name;header.parent.mkdir(parents=True,exist_ok=True)
    header.write_text('/* Hardware boundary supplied by display_fake.h. */\n')
run([*flags,'-I'+str(out),'-include',str(app/'tests/display_fake.h'),
     str(app/'tests/display_test.c'),'-o',str(out/'display')])
run([str(out/'display')])
