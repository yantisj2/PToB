#!/usr/bin/env python3
"""Actual SS3 native survey regressions with synthetic radio callbacks; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,os,json,subprocess,hashlib
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 os.environ.setdefault('IDF_PATH',str(a.sdk.resolve()))
 from rc7_permission_e2e import native_fixture
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);native_fixture(out)
 native=out/'native.cpp';s=native.read_text();assert s.count('#include "rc7_permission_test.cpp"')==1
 native.write_text(s.replace('#include "rc7_permission_test.cpp"','#include "ss3_durability_native_cases.cpp"'))
 (out/'esp_heap_caps.h').write_text('#pragma once\n#include <cstdlib>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n#define MALLOC_CAP_INTERNAL 4\ninline void* heap_caps_calloc(size_t n,size_t s,unsigned){return std::calloc(n,s);}\ninline void heap_caps_free(void* p){std::free(p);}\ninline size_t heap_caps_get_total_size(unsigned){return 8388608;}\ninline size_t heap_caps_get_free_size(unsigned){return 8000000;}\n')
 (out/'esp_memory_utils.h').write_text('#pragma once\ninline bool esp_ptr_external_ram(const void* p){return p!=nullptr;}\n')
 crypto=a.sdk.resolve()/'components/bt/host/nimble/nimble/ext/tinycrypt';src=APP/'main/r2/r2-g1';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang');cxx=os.environ.get('CXX',cc+'++')
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(q)for q in (out,src,APP/'main/r2/rc7/include',crypto/'include',APP/'main',APP/'tests',APP/'tests/r2_fakes',APP/'tests/console_fakes',APP/'legacy/reference')]]
 sources=[src/(n+'.c')for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec','role_announcement','gnss_diagnostics')]+[APP/'main/gm7/gm7_codec.c',APP/'main/r2/rc7/protocol/rc_dynamic.c',APP/'main/r2/rc7/src/rc_boundary.c',APP/'main/g1_hmac.c',crypto/'src/sha256.c',crypto/'src/utils.c']
 record={'status':'INCOMPLETE','hardware':False,'source_sha256':hashlib.sha256((APP/'main/native_g1.cpp').read_bytes()).hexdigest(),'completed':[]}
 def run(cmd,label):
  with (out/(label+'.log')).open('x')as log:subprocess.run(list(map(str,cmd)),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
 try:
  objects=[]
  for q in sources:
   obj=out/(q.stem+'.o');objects.append(obj);run([cc,'-std=c11',*flags,'-c',q,'-o',obj],q.stem+'-compile')
  run([cxx,'-std=c++20',*flags,'-DV12_SS3=1','-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192','-DV12_R2_ENABLED=1','-DV12_R2_CONFIG=13','-DV12_R2_FIELD_CLIENT=1','-DV12_UNIT="SS3"','-DV12_R2_G1_LISTENING_PERCENT=100',native,APP/'main/td_mesh.cpp',APP/'main/gm7_wire.cpp',*objects,'-o',out/'test'],'native-build')
  for case in ('cleanup-create-failure','cleanup-worker-missing','long-empty-surveys','diagnostics-wire','gm7-epoch-discovery'):
   run([out/'test',case],case);record['completed'].append(case)
  record['status']='PASS'
 finally:(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
 print('PASS actual SS3 cleanup race and long-duration survey regressions')
if __name__=='__main__':main()
