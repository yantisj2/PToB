#!/usr/bin/env python3
"""Compile exact extracted pinned and corrected EP0 bodies under ASan/UBSan."""
import argparse,json,subprocess,sys,hashlib,re
from pathlib import Path
from usb_ep0_recovery import generate
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];a.out.mkdir();generate(a.deps/'zephyr',a.out/'fixed.c')
stock=(a.deps/'zephyr/drivers/usb/device/usb_dc_nrfx.c').read_text();fixed=(a.out/'fixed.c').read_text()
common=(a.deps/'zephyr/drivers/usb/common/nrf_usbd_common/nrf_usbd_common.c').read_text()
def function(body,name):
 m=re.search(r'^.*?\b'+name+r'\([^;]*?\)\s*\{',body,re.M);assert m,name
 start=m.start();at=body.index('{',m.start());depth=1;end=at+1
 while depth:
  if body[end]=='{':depth+=1
  if body[end]=='}':depth-=1
  end+=1
 return body[start:end]+'\n'
def setup(body):
 start=body.index('case NRF_USBD_COMMON_EVT_SETUP: {');end=body.index('\n\tdefault:',start)
 return 'static void accept_setup(void) {struct usbd_event evt={0};bool put_evt=false;switch(NRF_USBD_COMMON_EVT_SETUP){'+body[start:end]+'default:assert(0);}\nif(put_evt){struct usbd_event *e=usbd_evt_alloc();e->evt_type=evt.evt_type;e->evt=evt.evt;usbd_evt_put(e);}}\n'
cases=[]
for name,body in [('stock',stock),('fixed',fixed)]:
 src=(app/'tests/usb_ep0_fake.h').read_text()+setup(body)+function(common,'ev_setup_handler')
 src+='\n'.join(function(body,f) for f in ['usbd_work_process_setup','usbd_work_process_recvreq','usbd_work_process_ep_events','usbd_event_transfer_ctrl','usb_dc_ep_write','usb_dc_ep_read_wait','usb_dc_ep_read_continue'])
 src+=function(common,'nrf_usbd_common_irq_handler')
 src+=(app/'tests/usb_ep0_test.c').read_text();(a.out/(name+'.c')).write_text(src)
 cmd=[(__import__('os').environ.get('CC') or __import__('shutil').which('clang')),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(a.out/(name+'.c')),'-o',str(a.out/name)]
 if name=='fixed':cmd+=['-DFIXED=1']
 for phase,command in [('build',cmd),('run',[str(a.out/name)])]:
  q=subprocess.run(command,capture_output=True,text=True,timeout=20);(a.out/(name+'-'+phase+'.log')).write_text(q.stdout+q.stderr);cases.append({'name':name+'-'+phase,'exit_code':q.returncode})
  if q.returncode:print(q.stdout+q.stderr);break
(a.out/'diagnostic_under_test.h').write_text(function((app/'src/usb_reference_interface.c').read_text(),'ep0_diagnostic_request'))
for phase,command in [('build',[(__import__('os').environ.get('CC') or __import__('shutil').which('clang')),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(a.out),str(app/'tests/usb_ep0_diagnostic_test.c'),'-o',str(a.out/'diagnostic')]),('run',[str(a.out/'diagnostic')])]:
 q=subprocess.run(command,capture_output=True,text=True,timeout=20);(a.out/('diagnostic-'+phase+'.log')).write_text(q.stdout+q.stderr);cases.append({'name':'diagnostic-'+phase,'exit_code':q.returncode})
 if q.returncode:print(q.stdout+q.stderr);break
r={'status':'PASS' if len(cases)==6 and all(x['exit_code']==0 for x in cases) else 'FAIL','cases':cases,'hardware_access':False,'exact_extracted_source_sha256':{n:hashlib.sha256(s.encode()).hexdigest() for n,s in [('stock',stock),('fixed',fixed),('common',common)]}}
(a.out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r));sys.exit(r['status']!='PASS')
