#!/usr/bin/env python3
"""Actual native dual-source UI selection and freshness; offline only."""
import os
import argparse,hashlib,json,subprocess
from pathlib import Path
A=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
s=(A/'main/native_main.cpp').read_text();start=s.index('  w12_lora_view lv;w12_lora_snapshot(lv);uint64_t lora_utc=0;');end=s.index('\n#endif\n#ifdef PTOB_RC9\n  const auto rc9_clock',start)
(out/'actual_dual_display.inc').write_text(s[start:end])
cmd=[os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').is_file() else 'clang++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DPTOB_W12_DUAL_UI=1','-DPTOB_W12_LORA_RX=1','-DPTOB_W12_LORA_TX_FACTORY=1','-I'+str(out),'-I'+str(A/'main'),'-I'+str(A/'main/r2/r2-g1'),'-I'+str(A/'main/r2/rc7/include'),str(A/'tests/dual_display_test.cpp'),'-o',str(out/'selection')]
subprocess.run(cmd,check=True);subprocess.run([out/'selection'],check=True)
rc9_cmd=cmd[:-1]+[str(out/'rc9-selection'),'-DPTOB_RC9=1']
subprocess.run(rc9_cmd,check=True);subprocess.run([out/'rc9-selection'],check=True)
paths=[A/'main/native_main.cpp',A/'main/sync_watchdog.hpp',A/'main/rc9_source_policy.hpp',A/'legacy/display.hpp',A/'legacy/display.cpp',A/'tests/dual_display_test.cpp',Path(__file__)]
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',scope='ACTUAL_LORA_RMS_ALL_PAGES_WARMUP_STALE_EXPIRY_FAULT_BLE_PRIORITY_ASAN_UBSAN',hardware_access=False,sources={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}),indent=2)+'\n')
print('PASS actual dual-source RMS selection, expiry and BLE priority')
