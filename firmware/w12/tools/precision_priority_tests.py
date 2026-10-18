#!/usr/bin/env python3
"""Production native precision/control ordering checks with synthetic SDK calls.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Never opens hardware. Run after g1_host_tests to supply its codec objects.
"""
import argparse,json,os,subprocess
from pathlib import Path
from rc7_permission_e2e import native_fixture,replace_once,sha
APP=Path(__file__).resolve().parents[1]
CASES=('gm-dwell','gm-lock-fallback','gm-lock-search','gm-locked-retain','quality-dip','warm-window','cold-window','source-expiry','model-expiry',
       'capture-gap','wrong-parent','clock-failure','control-drain','batch-budget','retry-bound','retry-unreserved','retry-relay','legacy-control','relay-control','control-gatt',
       'control-stop-bound','control-delayed','control-old','control-expired','precision-delayed')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--objects',type=Path,required=True);p.add_argument('--native-source',type=Path)
    p.add_argument('--cases',nargs='+',choices=CASES,default=CASES);a=p.parse_args()
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);native_fixture(out)
    if a.native_source:
      original=a.native_source.read_text().replace('#include "native_device_binding.hpp"','#include "rc7_permission_device_fixture.hpp"')
      (out/'native_g1_fixture.cpp').write_text(original)
    s=(out/'native.cpp').read_text().split('#define V12_RC7_PERMISSION_NATIVE 1')[0]
    s=replace_once(s,'#include "native_g1_fixture.cpp"','void priority_marker_returned();void priority_join_written();\n#include "native_g1_fixture.cpp"')
    s=replace_once(s,'extern "C" bool clock_reference_read(uint64_t* t){','bool priority_clock_ok=true;\nextern "C" bool clock_reference_read(uint64_t* t){if(!priority_clock_ok)return false;')
    begin=s.index('int ble_gattc_read_by_uuid(');end=s.index('int ble_gattc_read(',begin)
    marker=replace_once(s[begin:end],'cb(h,&e,&a,arg);return 0;','cb(h,&e,&a,arg);priority_marker_returned();return 0;')
    s=s[:begin]+marker+s[end:]
    s=replace_once(s,'output.emplace_back(b,b+n);return 0;','output.emplace_back(b,b+n);priority_join_written();return 0;')
    s+='\n#include "precision_priority_fixture.inc"\n';(out/'priority.cpp').write_text(s)
    sdk=Path(os.environ['IDF_PATH']);crypto=sdk/'components/bt/host/nimble/nimble/ext/tinycrypt/include'
    flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers',
      '-fsanitize=address,undefined','-DV12_R2_ENABLED=1','-DV12_R2_FIELD_CLIENT=1','-DV12_R2_CONFIG=13',
      '-DV12_UNIT="C1"','-DV12_R2_G1_LISTENING_PERCENT=100','-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192']
    for path in [out,APP/'tests',APP/'tests/r2_fakes',APP/'tests/console_fakes',APP/'main',APP/'legacy/reference',APP/'main/r2/r2-g1',APP/'main/r2/rc7/include',crypto]:flags+=['-I'+str(path)]
    objects=[a.objects.resolve()/(n+'.o') for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec','rc_dynamic','rc_boundary','rc_slots','g1_hmac','sha256','utils')]
    binary=out/'priority';cmd=[os.environ.get('CXX','clang++'),*flags,str(out/'priority.cpp'),*map(str,objects),'-o',str(binary)]
    with (out/'compile.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
    result={'status':'RUNNING','cases':{},'native_sha256':sha(out/'native_g1_fixture.cpp'),'fixture_sha256':sha(APP/'tests/precision_priority_fixture.inc'),'scope':'SYNTHETIC_PUBLIC_SDK_BOUNDARY;NO_RF_OR_PRECISION_QUALIFICATION'}
    for name in a.cases:
      with (out/(name+'.log')).open('w') as f:r=subprocess.run([str(binary),name],stdout=f,stderr=subprocess.STDOUT,timeout=10)
      result['cases'][name]=r.returncode
    result['status']='PASSED' if not any(result['cases'].values()) else 'FAILED'
    (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
    if result['status']!='PASSED':raise SystemExit(1)
if __name__=='__main__':main()
