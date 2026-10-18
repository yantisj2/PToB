#!/usr/bin/env python3
"""Render the actual G1 status formats at bounded numeric widths; no hardware."""
import argparse,ast,ctypes,json,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
LIB=ctypes.CDLL(None)
LIB.snprintf.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_char_p]
LIB.snprintf.restype=ctypes.c_int
BOOL=set('rc7 inputs coarse gps_fix advertising connecting encrypted confirmed enabled initialized failed worker_busy refresh_needed refresh_urgent requested scan pending sync host_busy command_pending off_ack host_ready owner_busy fault active_used ready serving following source clock_valid boot_client boot_client_attempted boot_client_cancelled group_member client_capabilities client_control g1_sdk_connect_pending g1_sdk_scan_active deadline_exceeded client_pending client_running client_stopping client_deadline_exceeded rx_installed rx_armed rx_deadline_exceeded periodic discovery selected_power_available GPS_inputs GPS_coarse ui_ready ui_valid capture_active counter_exhausted application_drained body_driver_drained'.split())
TEXT={'profile':'r2-g1','enrollment_sha':'f'*64,'factory':'02:00:00:00:00:07','source':'f'*40,'build':'f'*64,'ui_state':'UNAVAILABLE','ui_follow':'x'*24,'path7':'f'*178,'coverage':'HISTORICAL_INCOMPLETE','action':'x'*32,'reason':'x'*64}
def formats(text,token):
 out=[]
 for m in re.finditer(re.escape(token)+'\\(',text):
  tail=text[m.end():];quote=False;escape=False;stop=None
  for i,c in enumerate(tail):
   if quote:
    if escape:escape=False
    elif c=='\\':escape=True
    elif c=='"':quote=False
   elif c=='"':quote=True
   elif c==',':stop=i;break
  assert stop is not None
  f=''.join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"',tail[:stop]));out.append(f)
 return out
def render(f,wide):
 args=[]
 for spec in re.finditer(r'%[-+0-9.#]*(?:llu|lld|llx|u|d|x|s)',f):
  kind=spec.group();prefix=f[:spec.start()];key=prefix.split()[-1].split('=')[0]
  if kind.endswith('s'):v=ctypes.c_char_p(TEXT.get(key,'x'*64).encode())
  elif 'll' in kind:v=ctypes.c_longlong(-(1<<63) if wide and kind.endswith('d') else (1<<63)-1 if wide else 0) if kind.endswith('d') else ctypes.c_ulonglong((1<<64)-1 if wide else 0)
  else:
   bound=1 if key in ('cal','um') or (key=='uii' and prefix.split()[-1].count('/')<2) or key in BOOL or (key=='rp' and prefix.split()[-1].count('/')==5) else 7 if key in ('ui_lamps','uii') else 100 if key=='listening_percent' else 4 if key=='rp' and prefix.split()[-1].count('/')==2 else 255 if key in ('self','mode','lifecycle','coordinator','root','standby','members','role','last_api','last_api_peer','first_error_api','link_mask','gatt_mask','ui_page','ui_hold_percentile','update_stage') else 65535 if key in ('handle','mtu','peer_mtu_min','host_stack_min_bytes','host_stack_configured_bytes','main_stack_free_words') else (1<<32)-1
   v=ctypes.c_int((3056 if key=='cal' else -100000 if key=='pcfg' else -(1<<31)) if wide else 0) if kind.endswith('d') else ctypes.c_uint(bound if wide else 0)
  args.append(v)
 buf=ctypes.create_string_buffer(16384);n=LIB.snprintf(buf,len(buf),f.encode(),*args);assert 0<n<len(buf);return bytes(buf[:n])
def main():
 ap=argparse.ArgumentParser();ap.add_argument("--gps-shadow",action="store_true");ap.add_argument("--rc7-estimate",action="store_true");args=ap.parse_args()
 src=(APP/'main/native_main.cpp').read_text();status=src[src.index('void status('):src.index('void ui(')]
 defs='#define V12_R2_ENABLED 1\n#define V12_R2_CONFIG 13\n#define PRIu64 "llu"\n#define PRId64 "lld"\n#define PRIx64 "llx"\n#define PRIu32 "u"\n#define PRId32 "d"\n#define PRIx32 "x"\n'
 if args.rc7_estimate:defs+='#define V12_R2_RC7_TX_ESTIMATE_LAB 1\n'
 if args.gps_shadow:defs+='#define V12_GPS_SHADOW_OBSERVER 1\n'
 pp=subprocess.check_output(['/opt/homebrew/opt/llvm/bin/clang++','-E','-P','-x','c++','-'],input=defs+status,text=True)
 fs=formats(pp,'response.format');assert len(fs)==9,len(fs)
 log=(APP/'main/bounded_log.cpp').read_text();begin=log[log.index('void frame_begin('):log.index('bool frame_submit(')]
 bp=subprocess.check_output(['/opt/homebrew/opt/llvm/bin/clang++','-E','-P','-x','c++','-'],input=defs+begin,text=True);bfs=formats(bp,'f.format');assert len(bfs)==1
 trailer=log[log.index('const int n=std::snprintf(trailer,'):log.index('if(n<=0||n>=int(sizeof(trailer)))')];trailer=re.sub(r'std::snprintf\(trailer,sizeof\(trailer\),','trailer_format(',trailer)
 tp=subprocess.check_output(['/opt/homebrew/opt/llvm/bin/clang++','-E','-P','-x','c++','-'],input=defs+trailer,text=True);tfs=formats(tp,'trailer_format');assert len(tfs)==1
 body_capacity=int(re.search(r'ConsoleFrameBytes-(\d+)',(APP/'main/bounded_log.hpp').read_text())[1]);frame_capacity=int(re.search(r'ConsoleFrameBytes=(\d+)',(APP/'main/console_protocol.hpp').read_text())[1]);body_capacity=frame_capacity-body_capacity
 end_capacity=int(re.search(r'char trailer\[(\d+)\]',log)[1]);end=len(render(tfs[0],True))+len(' end_crc32=ffffffff\n')
 results=[]
 for wide in (False,True):
  rows=[render(f,wide) for f in fs];body=len(render(bfs[0],wide))+sum(map(len,rows));assert body<body_capacity,(wide,body,body_capacity);assert end<end_capacity,(end,end_capacity);assert body+end<=frame_capacity
  results.append({'maximum_counter_widths':wide,'body_bytes':body,'END_max_bytes':end,'whole_max_bytes':body+end,'rows':[{'kind':r.split()[0].decode(),'bytes':len(r)} for r in rows]})
 print(json.dumps({'status':'PASSED','body_capacity':body_capacity,'END_capacity':end_capacity,'whole_capacity':frame_capacity,'rows':9,'original_response_us':1500000,'cases':results},indent=2))
if __name__=='__main__':main()
