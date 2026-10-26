#!/usr/bin/env python3
"""Regress primary-scanner AUX cancellation with actual GM7/G1 capture bodies.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
Use --build with a normal native build (its pinned generator inputs), --out fresh.
"""
import argparse, hashlib, importlib.util, json, os, shutil, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
a=p.parse_args();app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
pins=json.loads((app/'cmake/gm7-controller.json').read_text())['inputs'];inputs=[]
for name in ('ull.c','lll_scan.c','lll_scan_aux.c','ull_adv_sync.c'):
 matches=[f for f in a.build.rglob(name) if hashlib.sha256(f.read_bytes()).hexdigest() in pins[name]]
 assert matches,name
 inputs.append(matches[0])
spec=importlib.util.spec_from_file_location('hooks',app/'tools/gm7_controller_hooks.py');hooks=importlib.util.module_from_spec(spec);spec.loader.exec_module(hooks)
hooks.generate(*inputs,out/'generated')
def body(s):
 start=s.index('static void isr_done_cleanup(void *param)\n{');end=s.index('\nstatic inline int isr_rx_pdu',start)
 return s[start:end]
for name in ('hal/nrf_radio.h','hal/nrf_timer.h','nrfx_ppi.h','zephyr/irq.h'):
 f=out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Fake public driver boundary. */\n')
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_GM7_NATIVE=1','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'tests'),'-include',str(app/'tests/gm7_capture_cleanup_fake.h'),*[str(app/f)for f in ('tests/gm7_capture_cleanup_test.c','src/gm7_capture.c','src/radio_capture.c')],'-o',str(out/'test')]
for original in (True,False):
 (out/'scan_cleanup.inc').write_text(body(inputs[1].read_text() if original else (out/'generated/lll_scan.c').read_text()))
 subprocess.run(cmd,check=True)
 subprocess.run([str(out/'test'),str(int(original))],check=True)
print('PASS original failure reproduced and corrected scanner cleanup tested')
