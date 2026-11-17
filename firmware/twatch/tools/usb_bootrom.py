#!/usr/bin/env python3
"""Enter watch ROM through the bounded console, or return from ROM to its app.
Requires pyserial and esptool 4.12. No flash writes. Exact factory/build required.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,fcntl,json,re,subprocess,time
from pathlib import Path
import serial
from serial.tools import list_ports
import framed_console as console

class QuietSerial(serial.Serial):
 # Opening a diagnostic console must not pulse the native USB reset signals.
 def _update_dtr_state(self):pass
 def _update_rts_state(self):pass

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('action',choices=('enter','run'))
 p.add_argument('--factory',required=True)
 p.add_argument('--receipt',type=Path,required=True,help='Exact installed native build receipt')
 p.add_argument('--out',type=Path,required=True,help='Fresh ignored artifacts directory')
 a=p.parse_args();factory=a.factory.lower()
 assert re.fullmatch(r'[0-9a-f]{2}(:[0-9a-f]{2}){5}',factory)
 repo=Path(__file__).resolve().parents[3];out=a.out.resolve()
 assert repo/'artifacts' in out.parents and not out.exists()
 assert subprocess.run(['git','-C',str(repo),'check-ignore','--quiet',str(out)]).returncode==0
 r=json.loads(a.receipt.read_text());assert r['artifact_class']=='PTOB_TWATCH_RC' and r['device_id']=='0000'+factory.replace(':','')
 manifest=dict(r['runtime_identity'],factory_mac=factory,r2_profile='r2-g1',console_frame_bytes=8704,fault_watchdog='SUSTAINED_FAULT_120S_V1',control_contract=r.get('control_contract'))
 ports=[q for q in list_ports.comports() if (q.vid,q.pid)==(0x303a,0x1001) and (q.serial_number or '').lower()==factory]
 assert len(ports)==1,'Exact native USB watch required'
 port=ports[0].device;out.mkdir(mode=0o700)
 with open('/tmp/ptob-lab-TWATCH-'+factory.replace(':','')+'.lock','a') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
  for name in (port,port.replace('/cu.','/tty.')):
   q=subprocess.run(['lsof','-nPlX',name],capture_output=True,text=True,timeout=5)
   assert q.returncode==1 and not q.stdout.strip() and not q.stderr.strip(),'USB already owned or ownership unknown'
  with QuietSerial(port,115200,timeout=.05,write_timeout=.5,exclusive=True) as usb, (out/'serial.log').open('wb') as log:
   frames=[];session=None
   def request(action=None,prior=None):
    nonlocal session
    nonce=console.fresh_nonce();parser=console.Parser(nonce,manifest,session)
    usb.write((console.control_status_command(nonce) if r.get('control_contract')=='BOUNDED_CONTROL_V1' else console.status_command(nonce)) if action is None else console.mutation_command(prior,nonce,action))
    until=time.monotonic()+1.5
    while time.monotonic()<until:
     data=usb.read(8192);log.write(data);log.flush();found=parser.feed(data)
     if found:
      f=found[-1];session=f['begin']['session'];frames.append(f)
      (out/'frames.json').write_text(json.dumps(frames,indent=2)+'\n')
      assert not any(f['request_delta'].values()),'Loss during request'
      return f
    raise RuntimeError('No complete timely CRC response; no reset issued')
   from esptool.targets.esp32s3 import ESP32S3ROM
   if a.action=='enter':
    f=request();startup_until=time.monotonic()+8
    # Wait for first presentation before starting the immutable two-second drain.
    # This is a startup wait, never an extension of an admitted maintenance lease.
    while f['rows']['TWATCH_UI']['ready']=='0' and f['rows']['TWATCH_UI']['errors']=='0' and f['rows']['W12_V12_COMMAND']['update_stage']=='0':
     assert time.monotonic()<startup_until,'Display startup not ready; no maintenance requested'
     time.sleep(.1);f=request()
    stage=f['rows']['W12_V12_COMMAND']['update_stage']
    if stage=='0':
     f=request('prepare_update',f)
     assert f['rows']['W12_V12_COMMAND']['accepted']=='1','Maintenance refused'
    else:
     # Resume only an already completed original drain, never renew its lease.
     assert stage=='2' and f['rows']['W12_V12_COMMAND']['application_drained']=='1','Original maintenance not ready'
    deadline=int(f['rows']['W12_V12_COMMAND']['update_deadline_us'])
    while True:
     f=request();cmd=f['rows']['W12_V12_COMMAND']
     assert cmd['update_stage']!='3','Original drain faulted; no reset issued'
     if cmd['application_drained']=='1':break
     assert int(f['rows']['W12_V12_STATUS']['monotonic_us'])<deadline
     time.sleep(.02)
    console.require_update_off(f,r)
    assert int(cmd['update_completed_us'])<deadline
    f=request('bootrom',f);assert f['rows']['W12_V12_COMMAND']['accepted']=='1'
    # A software reset re-enumerates USB. Close before rediscovering identity.
   else:
    esp=ESP32S3ROM(usb);esp.connect(mode='no_reset')
    assert bytes(esp.read_mac()).hex()==factory.replace(':','')
    esp.write_reg(esp.RTC_CNTL_OPTION1_REG,0,esp.RTC_CNTL_FORCE_DOWNLOAD_BOOT_MASK)
    esp.watchdog_reset()
  if a.action=='enter':
   deadline=time.monotonic()+8;matched=[]
   while time.monotonic()<deadline:
    matched=[q for q in list_ports.comports() if (q.vid,q.pid)==(0x303a,0x1001) and (q.serial_number or '').lower()==factory]
    if matched:break
    time.sleep(.1)
   assert len(matched)==1,'Watch did not re-enumerate'
   with QuietSerial(matched[0].device,115200,timeout=.1,exclusive=True) as usb:
    esp=ESP32S3ROM(usb);esp.connect(mode='no_reset')
    assert bytes(esp.read_mac()).hex()==factory.replace(':','')
  (out/'receipt.json').write_text(json.dumps({'status':'ROM_IDENTITY_VERIFIED' if a.action=='enter' else 'APP_RESET_ISSUED','factory':factory,'action':a.action,'flash_written':False,'runtime_verification_required':a.action=='run'},indent=2)+'\n')
  print('PASS:',a.action,'(no flash writes)')
if __name__=='__main__':main()
