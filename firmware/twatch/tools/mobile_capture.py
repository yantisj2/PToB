#!/usr/bin/env python3
"""Bounded exact-watch RF/display/IMU capture. No flash, reset or transmitter access.
Default is read-only. --relay explicitly tests a watch-side parent pin, then
restores AUTO; it requires an initially unpinned AUTO watch. Raw logs stay ignored.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,fcntl,json,os,plistlib,re,subprocess,time
from pathlib import Path
import serial
from serial.tools import list_ports
import framed_console as c
from receiver_report import summarize as receiver_summary
from power_capture import continuity as power_continuity
from roaming_capture import BidirectionalRoaming,auto_restored,report as roaming_report

def desktop():
 if os.uname().sysname!='Darwin':raise RuntimeError('This hardware guard supports macOS only')
 data=plistlib.loads(subprocess.check_output(['/usr/sbin/ioreg','-n','Root','-d','1','-a'],timeout=3));sessions=[]
 def walk(n):
  if isinstance(n,list):
   for v in n:walk(v)
  elif isinstance(n,dict):sessions.extend(n.get('IOConsoleUsers',[]));walk(n.get('IORegistryEntryChildren',[]))
 walk(data);active=[s for s in sessions if s.get('kCGSSessionOnConsoleKey')]
 if len(active)!=1 or active[0].get('CGSSessionScreenIsLocked',False):raise RuntimeError('Desktop locked or unknown')
class QuietSerial(serial.Serial):
 def _update_dtr_state(self):pass
 def _update_rts_state(self):pass

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--factory',required=True);p.add_argument('--receipt',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
 p.add_argument('--imu',action='store_true',help='Request read-only IMU diagnostics at 2 Hz; includes corrected acceleration counts')
 p.add_argument('--radio-only',action='store_true',help='Evaluate receiver/display continuity only; do not request or qualify IMU behavior')
 p.add_argument('--fine-monitor',action='store_true',help='Read the continuous production fine owner without starting or stopping it; may accompany --timing')
 p.add_argument('--fine-shadow',action='store_true',help='Finite RX-only GPIO17 fine raw observer; requires reviewed shadow image')
 p.add_argument('--fine-windows',action='store_true',help='Finite 64-packet RX-only fine association cohort; requires reviewed shadow image')
 p.add_argument('--timing',action='store_true',help='Finite every4 physical timestamp observer; at most600 seconds, no radio or IMU changes')
 p.add_argument('--wake-face',action='store_true',help='Wake current face once after10s for an isolated display-load receiver test; at least120 seconds')
 p.add_argument('--ble-cycle',action='store_true',help='Cycle BLE OFF/ON while keeping LoRa ON; requires at least120 seconds and both initially ON')
 p.add_argument('--radio-cycle',action='store_true',help='Exercise Settings BLE/LoRa OFF and ON, restore both ON; at least 120 seconds')
 p.add_argument('--seconds',type=int,default=180);p.add_argument('--relay',help='Exact authenticated relay device ID; optional watch-only selection test')
 p.add_argument('--bidirectional',action='store_true',help='With --relay: fresh RC1 -> current direct GM -> RC1,60s per admitted leg, then AUTO; radio-only,360..600s')
 a=p.parse_args();os.umask(0o077);factory=a.factory.lower()
 assert re.fullmatch(r'[0-9a-f]{2}(:[0-9a-f]{2}){5}',factory)
 assert 30<=a.seconds<=1800
 assert sum((a.timing,a.fine_shadow,a.fine_windows))<=1
 assert not a.fine_monitor or a.radio_only and not (a.fine_shadow or a.fine_windows or a.imu)
 assert not (a.fine_shadow or a.fine_windows) or a.seconds<=600 and a.radio_only and not (a.radio_cycle or a.ble_cycle or a.imu or a.wake_face) and (not a.relay or a.fine_windows)
 assert not a.timing or a.seconds<=600 and a.radio_only and not (a.radio_cycle or a.ble_cycle or a.relay or a.imu or a.wake_face)
 assert not (a.radio_only and a.imu),'Choose receiver-only or IMU diagnostics'
 assert not a.wake_face or a.seconds>=120 and not (a.radio_cycle or a.ble_cycle or a.relay or a.imu)
 assert not (a.radio_cycle and a.ble_cycle)
 assert not (a.radio_cycle or a.ble_cycle) or a.seconds>=120 and not a.relay
 assert not a.relay or re.fullmatch(r'[0-9a-f]{16}',a.relay) and int(a.relay,16) and a.seconds>=180
 assert not a.bidirectional or a.relay and a.radio_only and 360<=a.seconds<=600 and not (a.timing or a.fine_shadow or a.fine_windows or a.radio_cycle or a.ble_cycle or a.imu or a.wake_face)
 repo=Path(__file__).resolve().parents[3];out=a.out.resolve()
 assert repo/'artifacts' in out.parents and not out.exists()
 assert subprocess.run(['git','-C',str(repo),'check-ignore','--quiet',str(out)]).returncode==0
 receipt=json.loads(a.receipt.read_text());assert receipt['artifact_class']=='PTOB_TWATCH_RC' and receipt['device_id']=='0000'+factory.replace(':','')
 manifest=dict(receipt['runtime_identity'],factory_mac=factory,r2_profile='r2-g1',console_frame_bytes=8704,fault_watchdog='SUSTAINED_FAULT_120S_V1',fine_capture_profile=receipt.get('fine_capture_profile',{}),lora_shadow_schedule=receipt.get('lora_shadow_schedule'))
 desktop();ports=[q for q in list_ports.comports() if (q.vid,q.pid)==(0x303a,0x1001) and (q.serial_number or '').lower()==factory];assert len(ports)==1,'Exact watch missing'
 port=ports[0].device;out.mkdir(mode=0o700);frames=[];session=None;restore=False;failure=None;roaming=None
 with open('/tmp/ptob-lab-TWATCH-'+factory.replace(':','')+'.lock','a') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
  for name in (port,port.replace('/cu.','/tty.')):
   q=subprocess.run(['lsof','-nPlX',name],capture_output=True,text=True,timeout=5);assert q.returncode==1 and not q.stdout.strip() and not q.stderr.strip(),'Port owned or unknown'
  with QuietSerial(port,115200,timeout=.03,write_timeout=.5,exclusive=True) as usb,(out/'serial.log').open('wb') as raw,(out/'frames.jsonl').open('w') as journal:
   def request(action=None,prior=None):
    nonlocal session
    desktop();nonce=c.fresh_nonce();m=dict(manifest)
    if action and action.startswith('fine '):m['watch_frame']='FINE'
    if action and action.startswith('window '):m['watch_frame']='WINDOW_EDGE' if action.startswith('window edges ') else 'WINDOW_LATE' if action.startswith('window late ') else 'WINDOW_PACKET'
    if action and action.startswith('timing '):m['watch_frame']='TIMING'
    if action=='lora diag':m['watch_frame']='LORA'
    if action=='imu diag':m['watch_frame']='IMU'
    if action=='dual read 0 0':m['watch_frame']='DUAL'
    parser=c.Parser(nonce,m,session);usb.write(c.status_command(nonce) if action is None else c.mutation_command(prior,nonce,action));deadline=time.monotonic()+1.5
    while time.monotonic()<deadline:
     data=usb.read(8192);raw.write(data);found=parser.feed(data)
     if found:
      f=found[-1];session=f['begin']['session'];assert not any(f['request_delta'].values()),'Console loss'
      f['capture_elapsed_s']=time.monotonic()-started;frames.append(f);journal.write(json.dumps(f)+'\n');journal.flush()
      assert f['rows']['TWATCH_UI'].get('errors')=='0','Display error; stop capture before relay test'
      if not a.imu and not a.radio_only:
       assert f['rows']['TWATCH_UI'].get('imu_errors','0')=='0','IMU error; preserve failed capture'
       if f['rows']['TWATCH_UI'].get('ready')=='1':assert f['rows']['TWATCH_UI'].get('imu','1')=='1','IMU not ready'
      return f
    raise RuntimeError('Original 1.5-second response bound expired')
   started=time.monotonic();last_lora=last_dual=-10;pinned=False;restored=False;radio_steps=[];radio_restore=False;wake_step=None;timing_active=False;timing_cursor=0;fine_active=False;fine_cursor=0;window_active=False;window_done=False;window_ids=[]
   def radio_state(f,ble,lora):
    # LoRa diagnostics carry a compact BLE row; pair with the preceding
    # full status from this same verified boot and bounded request sequence.
    full=next(q for q in reversed(frames) if 'client_running' in q['rows']['W12_R2_STATUS'])
    assert 0<=f['capture_elapsed_s']-full['capture_elapsed_s']<1.5,'Stale BLE owner snapshot'
    b=full['rows']['W12_R2_STATUS'];l=f['rows']['N30_LORA_HARDWARE']
    if receipt.get('ble_power_lifecycle') in c.CONTROLLER_OFF_CONTRACTS:
     u=f['rows']['TWATCH_UI']
     assert u.get('ble_power_stage')==('0' if ble else '6'),'Controller lifecycle not settled'
     assert u.get('ble_controller')==('2' if ble else '0'),'Unexpected actual controller state'
     assert u.get('ble_host_running')==str(int(ble)),'Unexpected actual host task state'
     assert u.get('ble_power_requested')==str(int(ble)),'Wrong BLE power request'
     assert ble or u.get('ble_power_off_ack')=='1','Controller OFF not acknowledged'
     if 'ble_power_worker' in u:assert u['ble_power_worker']=='0','Transient power worker did not retire'
    if ble:
     assert b['requested']=='1' and b['client_running']=='1' and b['fault']=='0','BLE did not restart'
    else:
     assert all(b[k]=='0' for k in ('requested','scan','advertising','synced','connecting','client_running','client_stopping','rx_armed')) and b['off_ack']=='1','BLE did not retire'
     assert b['stop_generation']==b['stop_completed'] and b['client_stop_generation']==b['client_stop_completed'],'BLE retirement generation mismatch'
    if lora:
     assert l['requested']=='1' and l['active']=='1' and l['fault']=='0','LoRa did not restart'
    else:
     assert l['requested']=='0' and l['active']=='0' and all(l[k]=='1' for k in ('off_ack','SPI_retired','capture_released','reset_asserted')),'LoRa did not retire'
     assert l['generation']==l['off_ack_generation'],'LoRa retirement generation mismatch'
   def radio_step(action,ble,lora):
    f=request();f=request(action,f);assert f['rows']['W12_V12_COMMAND']['accepted']=='1','UI radio action refused'
    deadline=time.monotonic()+(6 if ble and receipt.get('ble_power_lifecycle') in c.CONTROLLER_OFF_CONTRACTS else 3);last=None
    while time.monotonic()<deadline:
     time.sleep(.15);f=request();f=request('lora diag',f)
     try:radio_state(f,ble,lora);return f
     except AssertionError as error:last=error
    raise last

   try:
    first=request()
    if a.relay:
     status=first['rows']['W12_R2_STATUS']
     assert status.get('rp','').split('/')[0]=='0000000000000000' and first['rows']['W12_V12_COMMAND'].get('um')=='0/0/0','Relay test requires verified unpinned AUTO (unknown schema refused)'
     if a.bidirectional:roaming=BidirectionalRoaming(first,a.relay)
     if a.fine_windows:
      first=request('lora diag',first);radio_state(first,True,True)
      restore=True;first=request('rc-parent '+a.relay,first)
      assert first['rows']['W12_V12_COMMAND']['accepted']=='1','Relay selection refused'
      pinned=True;join_deadline=time.monotonic()+45
      while True:
       first=request();r=first['rows']['W12_R2_STATUS'];rc7=r.get('rc7','').split('/')
       if r.get('joined_peer')==a.relay and r.get('following')=='1' and len(rc7)==4 and rc7[0]=='2' and rc7[2]=='1' and int(rc7[3])>0:break
       assert time.monotonic()<join_deadline,'Authenticated requested relay did not become current within45s'
       time.sleep(.5)
    if a.fine_monitor:
     first=request('fine read 0',first);fs=first['rows']['N30_FINE_STATUS']
     assert fs['active']=='1' and fs['released']=='0' and fs['error']=='0' and first['rows']['N30_FINE_CONTRACT']['backend']=='1','Continuous production fine owner required'
    if a.fine_shadow or a.fine_windows:
     first=request('lora diag',first);radio_state(first,True,True)
     first=request('fine read 0',first);fs=first['rows']['N30_FINE_STATUS']
     assert fs['active']=='0' and fs['released']=='1','Fine observer already owned or not released'
     assert fs['pins']=='1' and fs['mode']=='TWATCH_NATIVE_RW_RX17_SHADOW','Reviewed shadow image required'
     if a.fine_shadow:
      fine_active=True;first=request('fine start '+str(a.seconds*1000)+' 4',first)
     else:
      window_active=True;first=request('window start '+str(a.seconds*1000)+' 64',first)
     assert first['rows']['W12_V12_COMMAND']['accepted']=='1','Fine observer start refused'
    if a.timing:
     first=request('timing read 0',first);assert first['rows']['N30_TIMING_STATUS']['active']=='0','Timing observer already owned'
     first=request('timing start '+str(a.seconds*1000)+' 4',first);assert first['rows']['W12_V12_COMMAND']['accepted']=='1','Timing start refused'
     timing_active=True
    if a.radio_cycle or a.ble_cycle or a.bidirectional:
     initial=request('lora diag',first);radio_state(initial,True,True)
    while time.monotonic()-started<a.seconds and not (roaming and roaming.done):
     tick=time.monotonic();elapsed=tick-started;f=request()
     if roaming:
      action=roaming.action(f)
      if action:
       restore=True;f=request(action,f);roaming.acknowledged(f)
      if roaming.done:restore=False
     if a.wake_face and wake_step is None and elapsed>=10:
      f=request('ui page 0',f);assert f['rows']['W12_V12_COMMAND']['accepted']=='1','Display wake refused'
      wake_step={'action':'ui page 0','elapsed_s':f['capture_elapsed_s']}
     actions=[('ui radio ble',False,True),('ui radio ble',True,True)] if a.ble_cycle else [('ui radio ble',False,True),('ui radio lora',False,False),('ui radio ble',True,False),('ui radio lora',True,True)]
     if (a.radio_cycle or a.ble_cycle) and len(radio_steps)<len(actions) and elapsed>=30+10*len(radio_steps):
      radio_restore=True;action,ble,lora=actions[len(radio_steps)];f=radio_step(action,ble,lora)
      radio_steps.append({'action':action,'elapsed_s':f['capture_elapsed_s'],'ble_on':ble,'lora_on':lora,'verified':True})
      if len(radio_steps)==len(actions):radio_restore=False
     if a.relay and not a.bidirectional and elapsed>=30 and not pinned:
      restore=True;f=request('rc-parent '+a.relay,f);assert f['rows']['W12_V12_COMMAND']['accepted']=='1';pinned=True
     if a.relay and not a.bidirectional and elapsed>=a.seconds-60 and pinned and not restored:
      if a.fine_windows:
       f=request('window stop',f);assert f['rows']['W12_V12_COMMAND']['accepted']=='1','Window stop before AUTO refused'
       f=request('fine read 0',f);fs=f['rows']['N30_FINE_STATUS']
       assert fs['active']=='0' and fs['released']=='1' and fs['OE']=='0' and fs['IRQ']=='0' and fs['channels']=='0','Window owner not released before AUTO'
       window_done=True
      f=request('rc-parent auto',f);assert f['rows']['W12_V12_COMMAND']['accepted']=='1';f=request();assert f['rows']['W12_R2_STATUS'].get('rp','').split('/')[0]=='0000000000000000','AUTO restoration not observed';restore=False;restored=True
     if a.fine_shadow or a.fine_monitor:
      f=request('fine read '+str(fine_cursor),f);fs=f['rows']['N30_FINE_STATUS']
      assert fs['error']=='0' and fs['contained']=='0','Fine capture fault'
      if int(fs['count']):fine_cursor=int(f['rows']['N30_FINE_EDGE'+str(int(fs['count'])-1)]['n'])
     if a.fine_windows and not window_done:
      f=request('window packet',f);ws=f['rows']['N30_WINDOW_STATUS']
      assert ws['owner_error']=='0' and ws['contained']=='0','Window owner fault'
      if ws['ready']=='1':
       ident=ws['window'];cursor=0;count=0
       for page in range(17):
        f=request('window edges '+str(cursor),f);es=f['rows']['N30_WINDOW_STATUS']
        assert es['window']==ident and es['packet_crc']==ws['packet_crc'],'Window changed before ACK'
        n=int(es['count']);count+=n
        if n:cursor=int(f['rows']['N30_WINDOW_EDGE'+str(n-1)]['n'])
        if n<4:break
       else:raise RuntimeError('Window edge pagination exceeded bound')
       window_ids.append({'window':int(ident),'reason':int(ws['reason']),'edges':count})
       if ws['active']=='1':
        f=request('window ack '+ident,f);assert f['rows']['W12_V12_COMMAND']['accepted']=='1','Window ACK refused'
       else:window_done=True
      if f['rows']['N30_WINDOW_STATUS']['active']=='0':window_done=True
     if a.timing:
      f=request('timing read '+str(timing_cursor),f);t=f['rows']['N30_TIMING_STATUS']
      if int(t['count']):timing_cursor=int(f['rows']['N30_TIMING_SAMPLE'+str(int(t['count'])-1)]['n'])
     if elapsed-last_lora>=5:
      f=request('lora diag',f);last_lora=elapsed
      if a.bidirectional:radio_state(f,True,True)
     if a.imu:f=request('imu diag',f)
     if elapsed-last_dual>=1:f=request('dual read 0 0',f);last_dual=elapsed
     time.sleep(max(0,.5-(time.monotonic()-tick)))
    if roaming and not roaming.done:raise RuntimeError('Original bounded roaming capture ended before all legs completed')
   except BaseException as e:
    failure=repr(e)
   finally:
    if fine_active or window_active:
     try:
      f=request();f=request('window stop' if window_active else 'fine stop',f)
      assert f['rows']['W12_V12_COMMAND']['accepted']=='1','Fine stop refused'
      if window_active:
       late_cursor=0
       for page in range(33):
        f=request('window late '+str(late_cursor),f);ls=f['rows']['N30_WINDOW_STATUS'];n=int(ls['count'])
        if n:late_cursor=int(f['rows']['N30_WINDOW_LATE'+str(n-1)]['window'])
        if n<4:break
       else:raise RuntimeError('Late window pagination exceeded bound')
      f=request('fine read 0',f);fs=f['rows']['N30_FINE_STATUS']
      assert fs['active']=='0' and fs['released']=='1' and fs['OE']=='0' and fs['IRQ']=='0' and fs['channels']=='0','Fine owner not released'
      fine_active=window_active=False
      f=request();f=request('lora diag',f);radio_state(f,True,True)
     except BaseException as e:failure=(failure or '')+'; fine release failed: '+repr(e)
    if timing_active:
     try:
      f=request();f=request('timing stop',f);assert f['rows']['W12_V12_COMMAND']['accepted']=='1';timing_active=False
     except BaseException as e:failure=(failure or '')+'; timing stop failed: '+repr(e)
    if radio_restore:
     try:
      f=request();f=request('lora diag',f)
      for key,action in [('W12_R2_STATUS','ui radio ble'),('N30_LORA_HARDWARE','ui radio lora')]:
       if f['rows'][key]['requested']=='0':
        f=request(action,f);assert f['rows']['W12_V12_COMMAND']['accepted']=='1';time.sleep(.3);f=request();f=request('lora diag',f)
      radio_state(f,True,True);radio_restore=False
     except BaseException as e:failure=(failure or '')+'; radio restore failed: '+repr(e)
    if restore:
     try:
      f=request();f=request('rc-parent auto',f);assert f['rows']['W12_V12_COMMAND']['accepted']=='1';f=request();assert f['rows']['W12_R2_STATUS'].get('rp','').split('/')[0]=='0000000000000000','AUTO restoration not observed'
      if a.bidirectional:
       deadline=time.monotonic()+3
       while not auto_restored(f) and time.monotonic()<deadline:time.sleep(.1);f=request()
       assert auto_restored(f),'AUTO original owner drain not completed'
      restore=False
     except BaseException as e:failure=(failure or '')+'; AUTO restore failed: '+repr(e)
    raw.flush()
 ui=[f['rows']['TWATCH_UI'] for f in frames];valid=[u for u in ui if u.get('clock')=='1'];regressions=0
 for x,y in zip(valid,valid[1:]):
  if int(y.get('sampled_us','0'))>int(x.get('sampled_us','0')) and int(y.get('utc_us','0'))<int(x.get('utc_us','0')):regressions+=1
 if a.imu and any(u.get('imu_errors','0')!='0' or u.get('ready')=='1' and u.get('imu')!='1' for u in ui):failure=(failure or '')+'; IMU unhealthy; diagnostic capture retained through requested duration'
 summary={'status':'PASS' if not failure and not regressions else 'FAIL','scope':'FINITE_HARDWARE_OBSERVATION_NOT_PRECISION_QUALIFICATION','imu_evaluated':not a.radio_only,'frames':len(frames),'clock_frames':len(valid),'backward_clock_steps':regressions,'max_render_us':max((int(u.get('render_us',0)) for u in ui),default=0),'max_flush_us':max((int(u.get('flush_us',0)) for u in ui),default=0),'last_ui':ui[-1] if ui else {},'radio_steps':radio_steps,'wake_step':wake_step,'timing_active_pending':timing_active,'fine_active_pending':fine_active,'fine_production_monitored':a.fine_monitor,'window_active_pending':window_active,'window_cohort':window_ids,'window_reason_free':sum(w['reason']==0 for w in window_ids),'window_faults':sum(w['reason']!=0 for w in window_ids),'radio_restore_pending':radio_restore,'relay_restore_pending':restore,'relay_requested':a.relay,'failure':failure}
 summary['power_clock']=power_continuity(frames)
 if (a.ble_cycle or a.radio_cycle) and receipt.get('ble_power_lifecycle') in c.CONTROLLER_OFF_CONTRACTS and not summary['power_clock']['passed']:
  summary['status']='FAIL';summary['failure']=(summary['failure'] or '')+'; clock continuity failed during radio power cycle'
 summary['bidirectional']=roaming_report(frames,roaming)
 (out/'receiver.json').write_text(json.dumps(receiver_summary(frames),indent=2)+'\n')
 (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
 if summary['status']!='PASS':raise SystemExit(1)
if __name__=='__main__':main()
