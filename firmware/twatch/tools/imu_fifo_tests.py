#!/usr/bin/env python3
"""Actual selected Bosch FIFO parser/HIF regressions, synthetic bytes only.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1];VENDOR=APP/'vendor/bhi260'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
source=VENDOR/'bhy2.c';provenance=VENDOR/'provenance.json';record=json.loads(provenance.read_text());blob=source.read_bytes()
assert hashlib.sha256(blob).hexdigest()==record['local_patches'][0]['local_sha256']
assert blob.count(b'\n')==blob.count(b'\r\n')
cc='/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').exists() else 'clang'
flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Wl,-dead_strip','-I'+str(VENDOR)]
test=APP/'tests/imu_fifo_test.c';hif=VENDOR/'bhy2_hif.c'
def compile(label,extra=()):
 exe=out/label
 with (out/(label+'-compile.log')).open('w') as f:subprocess.run([cc,*flags,*extra,str(test),str(hif),'-o',str(exe)],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
 return exe
exe=compile('patched')
r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=3);(out/'patched.log').write_text(r.stdout+r.stderr);r.check_returncode()
# Reconstruct precisely the recorded selected upstream input, rather than
# relying on a historical compiler bundle or fetching a changing upstream.
guard=b'''                /* PToB local patch, Copyright (c) 2026 Jonathan Yantis.\r
                 * Unknown event sizes cannot advance this parser. Refuse the\r
                 * record rather than spin or guess the next FIFO boundary. */\r
                if (dev->event_size[tmp_sensor_id] == 0)\r
                {\r
                    return BHY2_E_INVALID_PARAM;\r
                }\r
\r
'''
assert blob.count(guard)==1
lookup=b'                rslt = get_callback_info(tmp_sensor_id, &info, dev);\r\n                rslt = check_return_value(rslt);\r\n'
assert blob.count(lookup+guard)==1
premature=out/'premature-guard-bhy2.c';premature.write_bytes(blob.replace(lookup+guard,guard+lookup))
premature_exe=compile('premature-guard',['-DVENDOR_BHY2_SOURCE="'+str(premature)+'"'])
r=subprocess.run([str(premature_exe)],capture_output=True,text=True,timeout=3);(out/'premature-guard.log').write_text(r.stdout+r.stderr)
assert r.returncode!=0 and 'metadata' in r.stderr
original=blob.replace(guard,b'')
old=b'    uint32_t bytes_read = 0;\r\n\r\n    while ((*int_status'
new=b'    uint32_t bytes_read = 0;\r\n    int8_t temp_rslt = BHY2_OK;\r\n\r\n    while ((*int_status'
assert original.count(old)==1;original=original.replace(old,new)
old=b'    return *rslt;\r\n\r\n}\r\n\r\nint8_t bhy2_get_and_process_fifo('
assert original.count(old)==1;original=original.replace(old,old.replace(b'return *rslt',b'return temp_rslt'))
old=b'static int8_t parse_fifo_support(struct bhy2_fifo_buffer *fifo_buf)\r\n{\r\n    uint32_t i;'
assert original.count(old)==1;original=original.replace(old,old.replace(b'uint32_t i',b'uint8_t i'))
assert hashlib.sha256(original).hexdigest()==record['files']['bhy2.c']['sha256']
negative=out/'upstream-bhy2.c';negative.write_bytes(original);exe=compile('upstream',['-DVENDOR_BHY2_SOURCE="'+str(negative)+'"'])
controls={'premature_guard_before_lazy_system_size':'BOOT_META_ASSERTION_REJECTED'}
for case in ('zero','compaction'):
 try:
  r=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=1)
  raise AssertionError('Upstream negative control unexpectedly terminated: '+case+' '+str(r.returncode))
 except subprocess.TimeoutExpired:controls[case]='EXTERNAL_1S_TIMEOUT_CONFIRMED_NO_PROGRESS'
files=[source,hif,VENDOR/'bhy2.h',VENDOR/'bhy2_hif.h',VENDOR/'bhy2_defs.h',provenance,VENDOR/'LICENSE',APP/'NOTICE.md',test,Path(__file__)]
(out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'ACTUAL_VENDOR_PARSER_AND_HIF_SYNTHETIC_FIFO_ASAN_UBSAN','hardware_access':False,'negative_controls':controls,'upstream_hash_reconstructed':True,'source_crlf_preserved':True,'physical_stall_cause':'UNKNOWN_NO_STACK_OR_FIFO_BYTES','sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in files}},indent=2)+'\n');print((out/'patched.log').read_text().strip());print('PASS recorded upstream zero-size and wide-compaction hang negative controls')
