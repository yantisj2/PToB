#!/usr/bin/env python3
"""Real terminal hold/expiry with synthetic chronyc; no USB or daemon access."""
from pathlib import Path
import errno,fcntl,json,os,pty,re,shutil,select,struct,subprocess,tempfile,termios,time
HERE=Path(__file__).resolve().parent
root=Path(tempfile.mkdtemp(prefix='readout-hold-',dir=HERE/'.build'))
shutil.copy2(HERE/'.build/ptob-receiver',root/'ptob-receiver')
mode=root/'mode';mode.write_text('good')
fake=root/'chronyc';fake.write_text("#!/usr/bin/python3\nfrom pathlib import Path\nimport time\nif Path(__file__).with_name('mode').read_text()=='good':\n print(f'424C4500,BLE,1,{time.time()+.025:.9f},.025,0,.000000079,0,0,.2,.2,.05,1,Normal')\nelse:\n print('00000000,,0,0,0,0,0,0,0,0,0,0,0,Not synchronised')\n")
fake.chmod(0o700)
master,slave=pty.openpty();fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',24,80,0,0))
task=subprocess.Popen([str(root/'ptob-receiver'),'--chrony-only','--chronyc',str(fake),'--chrony-command-socket','/tmp/ptob-unused-ui-test.sock','--duration','15','--no-ntp','--jsonl',str(root/'capture.jsonl')],stdin=slave,stdout=slave,stderr=slave)
os.close(slave);output=bytearray();checks=0

def check(ok,label):
 global checks
 assert ok,f'{label}; evidence {root}'
 checks+=1

def frames():
 result=[]
 for frame in bytes(output).split(b'\x1b[H')[1:]:
  if b'\x1b[0m' not in frame:continue
  text=re.sub(r'\x1b\[[0-9;?]*[A-Za-z]','',frame.decode('utf-8',errors='replace'))
  result.append(text)
 return result

def poll():
 if select.select([master],[],[],.025)[0]:
  try:output.extend(os.read(master,65536))
  except OSError as e:
   if e.errno!=errno.EIO:raise

def wait_for(predicate,start=0):
 until=time.monotonic()+5
 while time.monotonic()<until:
  for f in frames()[start:]:
   if predicate(f):return f
  poll()
 raise AssertionError(f'Terminal state missing; evidence {root}')

try:
 good=wait_for(lambda f:'CHRONY  TRACKING' in f and 'CHRONY EST RMS  [ns]' in f)
 check('HELD' not in good,'live frame uses nanoseconds without a hold label')
 cursor=len(frames());mode.write_text('missing')
 held=wait_for(lambda f:'HELD <500ms' in f,cursor)
 held_at=time.monotonic();check('UTC withheld' not in held and 'CHRONY EST RMS  [ns]' in held,'large time and ns RMS remain during the initial gap')
 expired=wait_for(lambda f:'UTC withheld' in f and 'HELD <500ms' not in f,cursor)
 check(time.monotonic()-held_at>=.3,'the hold is visible before a prolonged outage replaces it')
 check('UTC WITHHELD' in expired,'prolonged outage visibly withholds time')
 cursor=len(frames());mode.write_text('good')
 recovered=wait_for(lambda f:'CHRONY  TRACKING' in f and 'HELD <500ms' not in f,cursor)
 check('CHRONY EST RMS  [ns]' in recovered and 'UTC withheld' not in recovered,'fresh readings replace outage state on recovery')
 cursor=len(frames());os.write(master,b'c')
 statistics=wait_for(lambda f:'CHRONY / STATISTICS' in f,cursor)
 check('EST RMS  79 ns' in statistics,'the chrony statistics page uses whole nanoseconds too')
 for name,frame in [('good',good),('held',held),('outage',expired),('recovered',recovered)]:
  (root/(name+'.txt')).write_text(frame)
 os.write(master,b'q')
 until=time.monotonic()+5
 while task.poll() is None and time.monotonic()<until:poll()
 task.wait(timeout=.5);check(task.returncode==0,'q closes the real terminal cleanly')
finally:
 if task.poll() is None:
  task.terminate()
  until=time.monotonic()+5
  while task.poll() is None and time.monotonic()<until:poll()
  if task.poll() is None:task.kill();task.wait(timeout=2)
 (root/'terminal.ansi').write_bytes(output);os.close(master)
rows=[json.loads(s) for s in (root/'capture.jsonl').read_text().splitlines()]
check(any(r['type']=='usb_status' and r.get('chrony_user_utc_ns') is None and (r.get('chrony') or {}).get('connected') for r in rows),'held pixels never become delivered UTC')
check(not any(r['type'] in ('usb_raw','usb_helper_start','chrony_service_start') for r in rows),'the fixture never opens USB or starts chronyd')
check(rows[-1]['type']=='usb_end','capture writer closes')
print(f'PASS {checks} actual terminal hold/expiry/recovery checks; synthetic chronyc only; {root}')
