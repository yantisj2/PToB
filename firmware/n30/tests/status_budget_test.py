#!/usr/bin/env python3
"""Render actual N30 formats at maximum numeric widths and bound whole frame."""
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import ast,importlib.util,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('w12_status_budget',APP.parent/'w12/tools/status_budget_test.py');w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
w.TEXT.update(GPS='ABSENT',rotation='CCW',display='64x128',hardware='UNVALIDATED',path='HOLD',upstream='PENDING');w.BOOL.update('help saver blank'.split())
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
  body=len(w.render(w.formats(pp(begin,relay),'f.format')[0],wide))+sum(len(w.render(f,wide)) for f in fs)
  end=len(w.render(w.formats(pp(trailer,relay),'trailer_format')[0],True))+len(' end_crc32=ffffffff\n')
  assert body<8704-800 and end<800 and body+end<=8704,(relay,wide,body,end)
  cases.append(dict(estimated_relay=relay,max_numeric_widths=wide,body_bytes=body,end_max_bytes=end,whole_bytes=body+end))
print(json.dumps(dict(status='PASS',rows=10,whole_capacity=8704,original_response_us=1500000,cases=cases)))
