#!/usr/bin/env python3
"""Execute production dual-source event-sequence models and retained diagnostics offline.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1];W12=APP.parent/'w12'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 cc=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++'if Path('/opt/homebrew/opt/llvm/bin/clang++').exists()else'clang++')
 flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DPTOB_N30=1']
 flags+=['-I'+str(x)for x in (out,APP/'main',W12/'main',W12/'main/r2/r2-g1',W12/'main/r2/rc7/include',W12/'legacy',W12/'legacy/reference')]
 records=[]
 def run(name,cmd):
  with(out/(name+'.log')).open('w')as log:subprocess.run(list(map(str,cmd)),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
  records.append(name)
 common=[APP/'main/ui_model.cpp',APP/'main/ui_render.cpp']
 run('dual-state-model-build',[cc,*flags,APP/'tests/dual_timing_state_machine_test.cpp',*common,'-o',out/'dual-model']);run('dual-state-model',[out/'dual-model'])
 text=(APP/'main/display.cpp').read_text();(out/'actual_dual_frame.inc').write_text(text[text.index('void n30_display_observe_source('):text.index('void n30_display_publish(')]+text[text.index('void n30_display_dual_frame('):text.index('void n30_display_status(')])
 run('dual-diag-build',[cc,*flags,APP/'tests/dual_diagnostics_test.cpp',*common,'-o',out/'dual-diagnostics']);run('dual-diag',[out/'dual-diagnostics',out/'payload.txt'])
 run('dual-CRC-decode',[sys.executable,'-B',APP/'tests/dual_console_test.py','--payload',out/'payload.txt'])
 run('source-bound-gate',[sys.executable,'-B',APP/'tests/receiver_state_gate_test.py'])
 files=[APP/'main'/x for x in ('dual_time.hpp','display.cpp','status_adapter.hpp','sync_watchdog.hpp','dual_command.hpp')]
 (out/'receipt.json').write_text(json.dumps(dict(status='PASS',checks=records,hardware_access=False,population='SYNTHETIC_EVENT_SEQUENCES_AND_ACTUAL_DIAGNOSTIC_FORMATTER',precision='UNQUALIFIED',sources={str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in files}),indent=2)+'\n')
 print('PASS dual-source state sequences, main/publication timing interleavings, retained breadcrumbs and actual CRC diagnostic payload')
if __name__=='__main__':main()
