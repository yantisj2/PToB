#!/usr/bin/env python3
"""Check actual PPS log widths and compiled profile selection; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import os,re,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(APP/'tools'))
from status_budget_test import formats,render
source=(APP/'main/native_main.cpp').read_text()
# Preprocess the real source without SDK headers; this observes the real profile
# conditional and helper calls without inventing runtime hardware behavior.
stripped=re.sub(r'^\s*#include .*$', '',source,flags=re.M)
defs='#define V12_R2_ENABLED 1\n#define V12_R2_CONFIG 13\n#define PRIu64 "llu"\n#define PRId64 "lld"\n#define PRIx64 "llx"\n#define PRIu32 "u"\n#define PRId32 "d"\n#define PRIx32 "x"\n'
CXX=os.environ.get('CXX',os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').is_file() else 'clang')+'++')
def preprocess(extra):
 return subprocess.check_output([CXX,'-E','-P','-x','c++','-'],input=defs+extra+stripped,text=True)
normal=preprocess('#define PTOB_GNSS_INDICATORS 1\n')
assert 'observe_production_pps_diagnostic();' in normal and 'score_production_pps_diagnostic(edge);' in normal
assert 'observe_gps_actuator' not in normal and 'W12_GPS_PPS_SHADOW' not in normal
assert 'W12_R2_PPS_SHADOW' in normal and 'W12_G1_PPS_SHADOW' in normal
assert 'production_pps_diagnostic' not in preprocess('#define PTOB_GNSS_INDICATORS 0\n')
assert 'production_pps_diagnostic' not in preprocess('#define PTOB_GNSS_INDICATORS 1\n#define V12_GPS_SHADOW_OBSERVER 1\n')
observer=normal[normal.index('void observe_production_pps_diagnostic()'):normal.index('void score_production_pps_diagnostic(')]
assert 'aid_time' not in observer and 'set_' not in observer and 'request' not in observer
score=normal[normal.index('void score_production_pps_diagnostic('):normal.index('void pps(')]
fs=formats(score,'std::printf');assert len(fs)==1 and fs[0].startswith('W12_G1_PPS_SHADOW ') and fs[0].endswith('\n')
log=(APP/'main/bounded_log.cpp').read_text();prefix=log[log.index('const int prefix=std::snprintf'):log.index('if(prefix<=0')]
prefix=prefix.replace('std::snprintf(line.bytes,sizeof(line.bytes),','prefix_format(')
pp=subprocess.check_output([CXX,'-E','-P','-x','c++','-'],input=defs+prefix,text=True)
pfs=formats(pp,'prefix_format');assert len(pfs)==1
record_capacity=int(re.search(r'char bytes\[(\d+)\]',log)[1])
for wide in (False,True):
 record=render(fs[0],wide);p=render(pfs[0],wide)
 assert b'\n' not in record[:-1] and len(record)+len(p)<record_capacity
 assert b'receiver_phase_ns=' in record and b'output_correction_ns=' in record and b'rate_known=' in record
print('PASS actual GNSS-on/off/special-profile selection, unchanged strict R2 metric/no aid branch; bounded PPS log maximum',len(render(fs[0],True))+len(render(pfs[0],True)),'of',record_capacity,'bytes')
