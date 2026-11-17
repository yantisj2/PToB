#!/usr/bin/env python3
"""Generate maximum-width control vectors from the production format strings.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,ast,json,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
CONV=re.compile(r'%(?P<flags>[-+ #0]*)(?P<width>[0-9]*)(?:\.(?P<precision>[0-9]+))?(?P<length>hh|ll|h|l|z|j|t)?(?P<type>[diuoxXsc%])')
def strings(text):return ''.join(ast.literal_eval(q) for q in re.findall(r'"(?:\\.|[^"\\])*"',text))
def generate(out,cxx):
 source=(APP/'main/native_main.cpp').read_text();source=source[source.index('[[gnu::noinline]] void status('):source.index('\nvoid respond_status')]
 pp=subprocess.run([cxx,'-E','-P','-x','c++','-std=c++20','-DV12_R2_ENABLED=1','-DV12_R2_CONFIG=13','-'],input='#include <cinttypes>\n'+source,text=True,capture_output=True,check=True,timeout=60).stdout
 formats=[strings(m.group(1)) for m in re.finditer(r'response\.format\(\s*((?:"(?:\\.|[^"\\])*"\s*)+)',pp)]
 formats=[f for f in formats if not f.startswith('W12_R2_STATUS ')]
 assert len(formats)==8,[f.split()[0] for f in formats]
 bound=0;rows=[]
 for fmt in formats:
  args=[];cursor=0;size=0
  for m in CONV.finditer(fmt):
   size+=len(fmt[cursor:m.start()]);cursor=m.end();kind=m['type'];length=m['length'] or '';key=re.search(r'([A-Za-z_][A-Za-z_0-9]*)=$',fmt[:m.start()]);key=key[1] if key else ''
   if kind=='%':size+=1;continue
   if kind=='s':
    value={'factory':'02:00:00:00:00:01','session':'0000000000000003','source':'source','build':'build','action':'CONTROL_STATUS','reason':'READ_ONLY'}.get(key,'OFF')
    op='cs.action' if key=='action' else 'cs.reason' if key=='reason' else json.dumps(value)
    args.append('(maximum?wide:'+op+')');size+=max(64,int(m['width'] or 0));continue
   bits=64 if length in ('ll','l','z','j','t') else 32;signed=kind in 'di';ctype=('long long' if length=='ll' else 'long' if length=='l' else 'int')
   if not signed:ctype='unsigned '+ctype
   maximum='INT64_MIN' if signed and bits==64 else 'INT32_MIN' if signed else 'UINT64_MAX' if bits==64 else 'UINT32_MAX'
   op={'nonce':'nonce','session':'3','accepted':'1','monotonic_us':'now','admitted_us':'now','update_stage':'cs.stage','update_deadline_us':'cs.deadline','update_completed_us':'cs.completed','application_drained':'cs.drained','off_ack':'1','off_proof':'15','generation':'1','acknowledged_generation':'1'}.get(key,'0')
   args.append('static_cast<'+ctype+'>(maximum?'+maximum+':'+op+')')
   digits=(16 if bits==64 else 8) if kind in 'xX' else (22 if bits==64 else 11) if kind=='o' else (20 if bits==64 else 10)+(1 if signed else 0)
   size+=max(digits,int(m['width'] or 0))
  size+=len(fmt[cursor:]);bound+=size
  rows.append(' f.format('+json.dumps(fmt)+(','+','.join(args) if args else '')+');')
 header='''// Generated from the current production status() formatter; test-only.
#pragma once
#include <cstdint>
struct CommonControl {unsigned stage=0;uint64_t deadline=0,completed=0;bool drained=false;const char* action="CONTROL_STATUS";const char* reason="READ_ONLY";};
template<class Frame> void common_control(Frame& f,uint64_t nonce,uint64_t now,bool maximum,const CommonControl& cs={}){
 const char* wide="XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
'''+ '\n'.join(rows)+'\n}\n'
 (out/'control_common_max.inc').write_text(header)
 (out/'control-budget.json').write_text(json.dumps({'common_rows':len(formats),'common_max_bytes':bound,'header_max_bytes':450,'END_reserved_bytes':800,'source':'firmware/twatch/main/native_main.cpp','construction':'Production printf templates; every numeric field widened to full printf type range; every %s capped at conservative64 bytes'},indent=2)+'\n')
def validate(out):
 import sys
 sys.path.insert(0,str(APP/'tools'));import framed_console as c
 budget=json.loads((out/'control-budget.json').read_text());vector=(out/'control-max.txt').read_text();mesh_ui_bytes=len(vector.encode());body=budget['common_max_bytes']+mesh_ui_bytes+budget['header_max_bytes']
 assert body+800<=8704,(body,budget,mesh_ui_bytes)
 budget.update(mesh_ui_max_bytes=mesh_ui_bytes,total_body_upper_bound=body,whole_frame_upper_bound=body+800,headroom_bytes=8704-body-800,status='PASS')
 (out/'control-budget.json').write_text(json.dumps(budget,indent=2)+'\n')
 manifest=dict(factory_mac='02:00:00:00:00:01',source_commit='source',build_id='build',r2_profile='r2-g1',r2_enrollment_sha256='e'*64,version='0.1.8',console_frame_bytes=8704,fault_watchdog='SUSTAINED_FAULT_120S_V1',control_contract='BOUNDED_CONTROL_V1',ble_power_lifecycle='FAULT_CLEANUP_OFF_V2')
 wire=(out/'control-max.txt.wire').read_bytes();p=c.Parser('0000000000000002',manifest);frames=[]
 for i in range(0,len(wire),17):frames+=p.feed(wire[i:i+17])
 assert len(frames)==1 and frames[0]['rows']['TWATCH_UI']['control_version']=='1'
 assert not any(frames[0]['request_delta'].values())
 # CRC corruption is never a fresh control proof.
 broken=wire.replace(b'control_version=1',b'control_version=0',1)
 try:c.Parser('0000000000000002',manifest).feed(broken)
 except RuntimeError:pass
 else:raise AssertionError('Corrupted control CRC accepted')
 previous=None;drain=None
 for nonce,suffix in [(5,'.bootstrap'),(6,'.prepare'),(7,'.ready'),(8,'.bootrom')]:
  raw=Path(str(out/'control-max.txt')+suffix).read_bytes();parser=c.Parser(f'{nonce:016x}',manifest,'0000000000000003');decoded=[]
  for i in range(0,len(raw),31):decoded+=parser.feed(raw[i:i+31])
  assert len(decoded)==1;f=decoded[0];assert not any(f['request_delta'].values())
  if suffix=='.bootstrap':assert c.control_status_command(f'{nonce:016x}').endswith(f'v12 control {nonce:016x}\n'.encode())
  if previous is not None:
   action='prepare_update' if suffix=='.prepare' else 'bootrom' if suffix=='.bootrom' else None
   if action:assert action.encode() in c.mutation_command(previous,f'{nonce:016x}',action)
  if suffix=='.prepare':drain=f['rows']['W12_V12_COMMAND']['update_deadline_us']
  if suffix in ('.ready','.bootrom'):
   c.require_update_off(f,manifest);assert f['rows']['W12_V12_COMMAND']['update_deadline_us']==drain
  previous=f
 print('PASS real control row widths:',body+800,'/8704B;',8704-body-800,'bytes conservative margin; actual-worker fragmented Parser replay + CRC rejection')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('action',choices=['generate','validate']);p.add_argument('--out',type=Path,required=True);p.add_argument('--cxx',default='c++');a=p.parse_args();generate(a.out,a.cxx) if a.action=='generate' else validate(a.out)
