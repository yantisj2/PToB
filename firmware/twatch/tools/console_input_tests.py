#!/usr/bin/env python3
"""Offline actual watch input body and host reconnect-abort regression.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,importlib.util,json,os,subprocess
from pathlib import Path

APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
source=APP/'main/native_main.cpp';host=APP/'tools/framed_console.py'
spec=importlib.util.spec_from_file_location('watch_console',host)
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
s=source.read_text();start=s.index('[[gnu::noinline]] void input()');brace=s.index('{',start);end=brace+1;depth=1
while depth:
 depth+=(s[end]=='{')-(s[end]=='}');end+=1
body=s[start:end]
code='''#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <vector>
std::vector<char> bytes;size_t cursor=0,chunk=64;
namespace n30_console {int read(void* out,size_t n){n=std::min({n,chunk,bytes.size()-cursor});if(n)std::memcpy(out,bytes.data()+cursor,n);cursor+=n;return int(n);}}
void command(const char* line){std::cout<<line<<'\\n';}
'''+body+'''
int main(int argc,char** argv){assert(argc==2);chunk=std::stoul(argv[1]);assert(chunk&&chunk<=64);bytes.assign(std::istreambuf_iterator<char>(std::cin),{});while(cursor<bytes.size())input();}
'''
(out/'actual_input.cpp').write_text(code)
cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
cmd=[cxx,'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'actual_input.cpp'),'-o',str(out/'actual_input')]
with (out/'compile.log').open('w') as log:subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=60)
nonce='0000000000000002'
prior={'begin':{'nonce':'0000000000000001','ticket':'0000000000000003','factory':'02:00:00:00:00:01','session':'0000000000000004'}}
status=c.status_command(nonce);mutation=c.mutation_command(prior,nonce,'bootrom')
bare_status=c.base.status_command(nonce);bare_mutation=mutation[2:]
assert status==b'\x00\n'+bare_status and mutation==b'\x00\n'+bare_mutation
# No transport/session/nonce validation is bypassed by adding the abort prefix.
for bad in ('0000000000000000','not-a-nonce'):
 try:c.status_command(bad)
 except RuntimeError:pass
 else:raise AssertionError('Invalid status nonce accepted')
try:c.mutation_command(prior,prior['begin']['nonce'],'bootrom')
except RuntimeError:pass
else:raise AssertionError('Reused mutation nonce accepted')
# ESP ROM SYNC request: SLIP-delimited direction/opcode/length/checksum header,
# followed by its four-byte magic and 32-byte training pattern (no LF).
rom=b'\xc0\x00\x08\x24\x00'+b'\x00'*4+b'\x07\x07\x12\x20'+b'\x55'*32+b'\xc0'
partial=bare_mutation[:-1]
cases={
 'negative_binary_bare_status':(rom+bare_status,[]),
 'negative_overlong_bare_status':(b'x'*192+bare_status,[]),
 'negative_lf_executes_pending_mutation':(partial+b'\n'+bare_status,[partial,bare_status.rstrip(b'\n')]),
 'binary_status':(rom+status,[bare_status.rstrip(b'\n')]),
 'overlong_status':(b'x'*192+status,[bare_status.rstrip(b'\n')]),
 'partial_mutation_aborted_by_status':(partial+status,[bare_status.rstrip(b'\n')]),
 'partial_mutation_aborted_by_mutation':(partial+mutation,[bare_mutation.rstrip(b'\n')]),
 'binary_mutation':(rom+mutation,[bare_mutation.rstrip(b'\n')]),
 'overlong_mutation':(b'x'*192+mutation,[bare_mutation.rstrip(b'\n')]),
 'clean_status':(status,[bare_status.rstrip(b'\n')]),
 'clean_mutation':(mutation,[bare_mutation.rstrip(b'\n')]),
 'back_to_back':(status+mutation+status,[bare_status.rstrip(b'\n'),bare_mutation.rstrip(b'\n'),bare_status.rstrip(b'\n')]),
}
results=[]
with (out/'runtime.log').open('w') as log:
 for name,(wire,expected) in cases.items():
  for chunk in (1,7,64):
   r=subprocess.run([str(out/'actual_input'),str(chunk)],input=wire,capture_output=True,check=True,timeout=5)
   actual=r.stdout.splitlines();assert actual==expected,(name,chunk,actual,expected,r.stderr)
   results.append({'case':name,'read_chunk':chunk,'dispatches':len(actual)})
   log.write(f'PASS {name} chunk={chunk} dispatches={len(actual)}\n')
files=[Path(__file__),source,host,APP.parent/'w12/tools/framed_console.py',APP.parent/'n30/tools/framed_console.py']
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',scope='ACTUAL_WATCH_INPUT_BODY_WITH_USB_READ_AND_COMMAND_DISPATCH_FAKES_ASAN_UBSAN',hardware_access=False,firmware_modified=False,cases=results,sources={str(q):hashlib.sha256(q.read_bytes()).hexdigest()for q in files}),indent=2)+'\n')
print('PASS actual input: binary/overflow negative controls, NUL+LF safe abort, partial mutation never dispatched, fresh request once; 36 executions')
