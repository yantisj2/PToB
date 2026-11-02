"""Read-only exact-bound Pi/M-HAT observation. Copyright 2026 Jonathan Yantis. MIT."""
import hashlib,json,os,pathlib,re,subprocess,time
from decimal import Decimal
P=pathlib.Path
os.umask(0o077)
os.sched_setaffinity(0,{0,1,2})
def bt():return time.clock_gettime(time.CLOCK_BOOTTIME)
def command(argv,timeout=20):
 try:
  p=subprocess.run(argv,capture_output=True,text=True,timeout=timeout)
  return dict(rc=p.returncode,stdout=p.stdout,stderr=p.stderr)
 except BaseException as e:return dict(error=repr(e))
def textfile(path):
 try:return P(path).read_text().strip()
 except BaseException as e:return dict(error=repr(e))
def jsonfile(path):
 try:return json.loads(P(path).read_text())
 except BaseException as e:return dict(read_error=repr(e))
def save(name,data):
 try:
  tmp=work/(name+'.tmp');tmp.write_text(json.dumps(data,allow_nan=False));tmp.replace(work/name)
 except BaseException as e:out['export_errors'].append(dict(stage='checkpoint '+name,error=repr(e)))
def stamp(value):
 a,b=value.split('#');n=Decimal(a)*1000000000
 if n!=n.to_integral_value():raise ValueError('non-integral timestamp')
 return int(n),int(b)
def snapshot():
 lo=time.clock_gettime_ns(time.CLOCK_BOOTTIME);real=time.time_ns();raw=time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW);hi=time.clock_gettime_ns(time.CLOCK_BOOTTIME)
 s=jsonfile('/run/ptob-dashboard/status.json');r=s.get('receiver',{});d=r.get('data') or {};q=s.get('source_status',{});t=s.get('tracking') or {}
 token=[d.get('boot'),q.get('root'),q.get('root_boot'),q.get('epoch'),q.get('parent'),q.get('context')]
 age=bt()-s.get('observed_boottime_s',-1000)
 good=bool(0<=age<=3 and r.get('fresh') and not r.get('error') and
  all(type(d.get(k)) is int and d[k]==0 for k in ('error','receiver_error','capture_error','uart_error')) and
  d.get('source')==PARAMS['expected_source'] and d.get('version')=='0.5.5' and
  d.get('following') and d.get('clock_valid') and d.get('offer_valid') and q.get('current') and
  all(x is not None for x in token) and all(re.fullmatch('[0-9a-f]{16}',str(token[i])) and int(token[i],16)!=0 for i in (0,1,2,4,5)) and
  s.get('pps',{}).get('fresh') and s.get('chain',{}).get('complete') and
  t.get('reference')=='PTOB' and t.get('leap')=='Normal' and
  any(x.get('kind')=='#' and x.get('state')=='*' and x.get('name')=='PTOB' for x in s.get('sources',[])))
 transfer=jsonfile('/run/ptob-clocks/transfer.json');server=jsonfile('/run/ptob-clocks/server.json');now=bt()
 transfer_fresh=0<=now-transfer.get('observed_boottime_s',-1000)<=2
 server_fresh=0<=now-server.get('observed_boottime_s',-1000)<=2
 workload_good=bool(transfer_fresh and server_fresh and transfer.get('running') is True and server.get('running') is True and server.get('ready') is True and transfer.get('tai_offset_s')==37 and server.get('tai_offset_s')==37)
 return dict(boottime=bt(),realtime_ns=real,monotonic_raw_ns=raw,clock_bracket_ns=[lo,hi],status=s,eligible=good,context=token,workload_good=workload_good,transfer_fresh=transfer_fresh,server_fresh=server_fresh,
  temperature=textfile('/sys/class/thermal/thermal_zone0/temp'),transfer=transfer,server=server)
UNITS=['chrony','ptob-telemetry','ptob-dashboard','ptob-phc2sys','ptob-ptp4l','ptob-ptp-wifi']
def units():return command(['systemctl','show',*UNITS,'--property=Id,ActiveState,SubState,MainPID,NRestarts,ExecMainStartTimestampMonotonic'])
def boundary():
 files={}
 paths=[P('/etc/chrony/chrony.conf'),P('/etc/ptob/pi5/receiver.json'),P('/opt/ptob/pi5/clock_chain.py')]
 paths += list(P('/etc/chrony').rglob('*.conf'))+list(P('/etc/chrony').rglob('*.sources'))+list(P('/opt/ptob/pi5/dashboard').glob('*.py'))
 for unit in UNITS+['ptob-pps-tuning']:
  paths += list(P('/etc/systemd/system').glob(unit+'.service'))+list(P('/etc/systemd/system').glob(unit+'.service.d/*'))
 for p in sorted(set(paths)):
  if p.is_file():
   try:files[str(p)]=hashlib.sha256(p.read_bytes()).hexdigest()
   except BaseException as e:files[str(p)]=dict(error=repr(e))
 ps=command(['ps','-eo','pid,ppid,lstart,args'])
 if 'stdout' in ps:ps['stdout']='\n'.join(x for x in ps['stdout'].splitlines() if any(k in x for k in ['chronyd','phc2sys','ptp4l','ptpd','timemaster','ts2phc','ntpd','timesyncd']))
 irq=textfile('/proc/interrupts');affinity={}
 if isinstance(irq,str):
  for row in irq.splitlines():
   if 'pps' in row.lower() and ':' in row:
    num=row.split(':')[0].strip()
    if num.isdigit():affinity[num]=textfile('/proc/irq/'+num+'/smp_affinity_list')
 return dict(boottime_s=bt(),utc_ns=time.time_ns(),kernel=os.uname().release,files=files,units=units(),
  tracking=command(['chronyc','-c','-n','tracking']),sources=command(['chronyc','-c','-n','sources']),
  sourcestats=command(['chronyc','-c','-n','sourcestats']),refclock=command(['chronyc','-c','-n','selectdata','PTOB']),
  effective_chrony=command(['chronyd','-p','-f','/etc/chrony/chrony.conf']),clock_processes=ps,
  interrupts=irq,pps_irq_affinity=affinity,governor=textfile('/sys/devices/system/cpu/cpufreq/policy0/scaling_governor'),
  l1=textfile('/sys/bus/pci/devices/0002:01:00.0/link/l1_aspm'),
  dynamic_events=textfile('/sys/kernel/tracing/dynamic_events'),status=snapshot())
binding=json.loads(P('/etc/ptob/pi5/receiver.json').read_text())
if binding.get('particle_id')!=PARAMS['module_id'] or PARAMS['expected_source'] not in binding.get('allowed_sources',[]):raise ValueError('exact enrolled HAT binding mismatch')
dev=P('/dev/pps-ptob').resolve(strict=True);base=P('/sys/class/pps')/dev.name
if not (base/'name').read_text().strip().startswith('ptob-pps'):raise ValueError('wrong PPS input')
work=P('/run')/PARAMS['run_id']
if work.exists():raise ValueError('unique remote directory required')
work.mkdir(mode=0o700)
out=dict(schema=2,mode=PARAMS['mode'],observer_source_sha256=PTOB_SOURCE_SHA256,observer_cpus=sorted(os.sched_getaffinity(0)),poll_s=.05,
 remote_checkpoint=str(work),observer_pid=os.getpid(),stop_file=str(work/'stop'),snapshots=[],edges=[],errors=[],export_errors=[],binding_verified=True)
out['initial']=boundary()
if PARAMS['mode']=='capture':
 first=out['initial']['status']
 if not first['eligible'] or not first['workload_good'] or first['context']!=PARAMS['settled_context']:raise ValueError('settled source or serving workload changed before acquisition; no fixed window started')
cursor_result=command(['journalctl','-n','1','-o','json','--no-pager']);cursor=None
try:cursor=json.loads(cursor_result['stdout'].splitlines()[-1])['__CURSOR']
except BaseException:pass
logs={str(p):dict(offset=p.stat().st_size,inode=p.stat().st_ino) for p in P('/var/log/chrony').glob('*') if p.is_file()}
start=bt();out['start']=start;out['start_utc_ns']=time.time_ns()
next_status=start;next_poll=start;next_checkpoint=start+30;next_units=start+15;last=None;last_tick=None;stable=None;cohort=None
duration=PARAMS.get('seconds',600);warmup=PARAMS['mode']=='warmup'
out['service_observations']=[]
try:
 while bt()-start<duration:
  if (work/'stop').exists():out['operator_stop']=True;break
  now=bt()
  try:
   value=(base/'assert').read_text().strip()
   if value!=last:
    tick,seq=stamp(value);out['edges'].append(dict(boottime=bt(),raw=value,seed=last is None))
    if last_tick is not None and (seq-last_tick[1]!=1 or not 500000000<tick-last_tick[0]<1500000000):stable=None
    last=value;last_tick=(tick,seq)
  except BaseException as e:out['errors'].append(dict(stage='pps',boottime=bt(),error=repr(e)));stable=None
  if now>=next_status:
   row=snapshot();out['snapshots'].append(row)
   if not row['eligible'] or not row['workload_good'] or row['context']!=cohort:stable=None
   if row['eligible'] and row['workload_good'] and stable is None:stable=now
   cohort=row['context'];next_status+=1
   if next_status<now:out['errors'].append(dict(stage='status-deadline',lateness_s=now-next_status));next_status=now+1;stable=None
  if now>=next_units:
   u=units();out['service_observations'].append(dict(boottime=bt(),units=u))
   if u!=out['initial']['units']:stable=None
   next_units+=15
  if now>=next_checkpoint:
   out['elapsed_s']=now-start;save('partial.json',out);next_checkpoint+=30
  if warmup and stable is not None and now-stable>=120:
   out['ready']=True;out['settled_s']=now-stable;out['settled_context']=cohort;break
  next_poll+=.05
  delay=next_poll-bt()
  if delay>0:time.sleep(delay)
  else:next_poll=bt()
except BaseException as e:out['failure']=repr(e)
finally:
 out['end']=bt();out['end_utc_ns']=time.time_ns();out.setdefault('ready',False)
 save('captured.json',out)
 # All subsequent export operations are best-effort and cannot discard acquisition.
 for stage,fn in [('post_status',snapshot),('final',boundary)]:
  try:out[stage]=fn()
  except BaseException as e:out['export_errors'].append(dict(stage=stage,error=repr(e)))
  save('capture.json',out)
 if not warmup:
  out['log_flush']=command(['chronyc','cyclelogs'])
 out['logs']={}
 for name,info in logs.items():
  try:
   p=P(name);stat=p.stat()
   if stat.st_ino!=info['inode'] or stat.st_size<info['offset']:raise ValueError('rotated/truncated; separately retained')
   with p.open('rb') as f:f.seek(info['offset']);blob=f.read()
   out['logs'][name]=dict(start_offset=info['offset'],end_offset=info['offset']+len(blob),inode=info['inode'],text=blob.decode(errors='replace'))
  except BaseException as e:out['logs'][name]=dict(error=repr(e),**info)
 out['journal']=command(['journalctl','--after-cursor='+cursor,'-o','json','--no-pager'],timeout=30) if cursor else dict(error='cursor unavailable',initial=cursor_result)
 save('capture.json',out)
 print(json.dumps(out,allow_nan=False))
