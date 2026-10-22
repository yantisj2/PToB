#!/usr/bin/env python3
"""Render actual N30 formats at maximum numeric widths and bound whole frame."""
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import ast,importlib.util,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('w12_status_budget',APP.parent/'w12/tools/status_budget_test.py');w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
w.TEXT.update(GPS='ABSENT',rotation='CCW',display='64x128',hardware='UNVALIDATED',path='HOLD',upstream='PENDING');w.BOOL.update('help saver blank m'.split())
def render(f,wide):
 # ESP32 unsigned long is32-bit. Normalize its specifiers for the host helper,
 # which understands u/llu but otherwise silently omits %lu varargs.
 return w.render(re.sub(r'%([-+0-9.#]*)l([udx])',r'%\1\2',f),wide)
def formats(text,token):
 # Literal-only peripheral rows end at ')', while formatted rows end at ','.
 out=[]
 for m in re.finditer(re.escape(token)+r'\(',text):
  tail=text[m.end():];quote=escape=False;stop=None
  for i,c in enumerate(tail):
   if quote:
    if escape:escape=False
    elif c=='\\':escape=True
    elif c=='"':quote=False
   elif c=='"':quote=True
   elif c in ',)':stop=i;break
  assert stop is not None
  out.append(''.join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"',tail[:stop])))
 return out
def pp(text,relay):
 defs='#define V12_R2_ENABLED 1\n#define V12_R2_CONFIG 13\n#define PRIu64 "llu"\n#define PRId64 "lld"\n#define PRIx64 "llx"\n#define PRIu32 "u"\n#define PRId32 "d"\n#define PRIx32 "x"\n'
 if relay:defs+='#define V12_R2_RC7_TX_ESTIMATE_LAB 1\n'
 return subprocess.check_output([os.environ.get('CXX','clang++'),'-E','-P','-x','c++','-'],input=defs+text,text=True)
src=(APP/'main/native_main.cpp').read_text();log=(APP/'main/bounded_log.cpp').read_text();ui=(APP/'main/display.cpp').read_text()
begin=log[log.index('void frame_begin('):log.index('bool frame_submit(')]
trailer=log[log.index('const int n=std::snprintf(trailer,'):log.index('if(n<=0||n>=int(sizeof(trailer)))')].replace('std::snprintf(trailer,sizeof(trailer),','trailer_format(')
cases=[]
for relay in (False,True):
 fs=formats(pp(src[src.index('void status('):src.index('void ui(')],relay),'response.format')+formats(ui[ui.index('void n30_display_status('):],'f.format');assert len(fs)==10
 for wide in (False,True):
  body=len(render(w.formats(pp(begin,relay),'f.format')[0],wide))+sum(len(render(f,wide)) for f in fs)
  end=len(render(w.formats(pp(trailer,relay),'trailer_format')[0],True))+len(' end_crc32=ffffffff\n')
  assert body<8704-800 and end<800 and body+end<=8704,(relay,wide,body,end)
  cases.append(dict(estimated_relay=relay,max_numeric_widths=wide,body_bytes=body,end_max_bytes=end,whole_bytes=body+end))
print(json.dumps(dict(status='PASS',rows=10,whole_capacity=8704,original_response_us=1500000,cases=cases)))

w.TEXT.update(mode='KEEP_OFF',context='f'*32,timestamp='HARDWARE_DIO1_MAPPED',physical_bias='UNKNOWN',durable_fault_journal='UNAVAILABLE',TX='FORBIDDEN',source_freshness='UNKNOWN',uncertainty='UNKNOWN',bound='UNKNOWN')
w.BOOL.update('metadata_known BLE_update time_valid parent_quality_current requested active fault off_ack SPI_retired capture_released reset_asserted RX_ONLY'.split())
diag=(APP/'main/lora_diagnostics.cpp').read_text();fs=formats(pp(src[src.index('[[gnu::noinline]] void lora_status_frame('):src.index('void status(')],False),'response.format')+formats(pp(re.sub(r'^#include.*$', '', diag, flags=re.M),False),'f.format')+formats(ui[ui.index('void n30_display_status('):],'f.format');assert len(fs)==10
for wide in (False,True):
 body=len(render(w.formats(pp(begin,False),'f.format')[0],wide))+sum(len(render(f,wide)) for f in fs)
 end=len(render(w.formats(pp(trailer,False),'trailer_format')[0],True))+len(' end_crc32=ffffffff\n')
 assert body<8704-800 and end<800 and body+end<=8704,(wide,body,end)
print(json.dumps(dict(status='PASS',frame='LORA',rows=10,max_body=body,max_whole=body+end,whole_capacity=8704)))

fine=(APP/'main/fine_capture.cpp').read_text();fine_formats=formats(pp(re.sub(r'^#include.*$', '', fine, flags=re.M),False),'f.format');assert len(fine_formats)==4
common=formats(pp(src[src.index('[[gnu::noinline]] void lora_status_frame('):src.index('void status(')],False),'response.format')+formats(ui[ui.index('void n30_display_status('):],'f.format');assert len(common)==4
for wide in (False,True):
 body=len(render(w.formats(pp(begin,False),'f.format')[0],wide))+sum(len(render(f,wide)) for f in common+[fine_formats[0],*[fine_formats[2]]*4,fine_formats[3]])
 end=len(render(w.formats(pp(trailer,False),'trailer_format')[0],True))+len(' end_crc32=ffffffff\n')
 assert body<8704-800 and body+end<=8704,(wide,body,end)
print(json.dumps(dict(status='PASS',frame='FINE',rows=10,max_body=body,max_whole=body+end,whole_capacity=8704)))
