"""Bounded Linux vendor-IN observation, executed through the enrolled Pi guard.

Copyright (c) 2026 Jonathan Yantis. MIT.
No resets, driver detach, CDC opens, register writes, daemons or clock changes.
The existing debugfs view reads eight runtime registers and uses runtime-PM
get/put internally. Only an already active, ancestry-matched controller is used.
"""
import ctypes
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import shutil
import stat
import subprocess
import tempfile
import time

if globals().get('__package__'):
    from .contract import decode


def read(path, name=None):
    return (Path(path)/name if name else Path(path)).read_text().strip()


class RealtimePair(ctypes.Structure):
    _fields_=[('before',ctypes.c_uint64),('realtime',ctypes.c_uint64),('after',ctypes.c_uint64)]


_realtime_pair_reader=None


def clocks():
    if _realtime_pair_reader is None:
        # Preserve the existing five-clock read order and its full bracket.
        a=time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        d=dict(monotonic_ns=time.monotonic_ns(),boottime_ns=time.clock_gettime_ns(time.CLOCK_BOOTTIME),
               realtime_ns=time.time_ns(),tai_ns=time.clock_gettime_ns(time.CLOCK_TAI))
        d.update(raw_before_ns=a,raw_after_ns=time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW))
        d['realtime_bracket_method']='legacy-wide';return d
    a=time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
    d=dict(monotonic_ns=time.monotonic_ns(),boottime_ns=time.clock_gettime_ns(time.CLOCK_BOOTTIME))
    pair=RealtimePair();ctypes.set_errno(0)
    if _realtime_pair_reader(ctypes.byref(pair)):
        raise ValueError('native realtime clock pair failed errno='+str(ctypes.get_errno()))
    d.update(realtime_ns=int(pair.realtime),realtime_raw_before_ns=int(pair.before),
             realtime_raw_after_ns=int(pair.after),realtime_bracket_method='native-pair')
    d.update(tai_ns=time.clock_gettime_ns(time.CLOCK_TAI),raw_before_ns=a,
             raw_after_ns=time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW))
    if not a<=pair.before<=pair.after<=d['raw_after_ns']:
        raise ValueError('native realtime pair outside full clock bracket')
    return d


def cleanup_clocks():
    """Error/cleanup snapshots cannot depend on a failing experimental helper."""
    global _realtime_pair_reader
    previous=_realtime_pair_reader;_realtime_pair_reader=None
    try:return clocks()
    finally:_realtime_pair_reader=previous


def final_clocks(errors):
    try:return clocks()
    except Exception as e:
        errors.append(dict(stage='final_clock_pair',error=str(e)))
        return cleanup_clocks()


def discover(serial,owned=False,root=Path('/sys/bus/usb/devices')):
    if not isinstance(serial,str) or not re.fullmatch(r'[!-~]{0,112}[0-9a-fA-F]{16}',serial) or int(serial[-16:],16)==0:
        raise ValueError('full serial with nonzero factory silicon suffix required')
    silicon=serial[-16:].lower(); candidates=[]
    for p in root.iterdir():
        if (p/'idVendor').exists() and read(p,'idVendor')=='2fe3' and read(p,'idProduct')=='0100':
            candidates.append((p,read(p,'serial')))
    matching=[(p,s) for p,s in candidates if s[-16:].lower()==silicon]
    if len(matching)!=1 or matching[0][1]!=serial:
        raise ValueError('exact serial absent or duplicate silicon identities')
    p=matching[0][0]
    interfaces=[]
    for q in root.glob(p.name+':*'):
        if [read(q,x) for x in ('bInterfaceClass','bInterfaceSubClass','bInterfaceProtocol')]==['ff','54','01']:
            interfaces.append(q)
    if len(interfaces)!=1 or ((interfaces[0]/'driver').exists() and
                             (not owned or (interfaces[0]/'driver').resolve().name!='usbfs')):
        raise ValueError('vendor interface absent, duplicate, or already bound; no detach permitted')
    interface=int(read(interfaces[0],'bInterfaceNumber'),16)
    bus=int(read(p,'busnum'));device=int(read(p,'devnum'));usbroot=(root/f'usb{bus}').resolve();controller=usbroot.parent
    direct=bool(re.fullmatch(r'\d+-\d+',p.name))
    info=dict(serial=serial,silicon=silicon,path=p.name,sysfs_path=str(p.resolve()),bus=bus,device=device,
              speed_mbps=read(p,'speed'),interface=interface,vendor='2fe3',product='0100',
              controller=str(controller),driver=(controller/'driver').resolve().name,
              controller_compatible=(controller.parent/'of_node/compatible').read_bytes().replace(b'\0',b',').decode(),
              direct_root_port=direct,usbfs=f'/dev/bus/usb/{bus:03d}/{device:03d}')
    return info,p,controller


def owners(node,device,own_fd=None):
    related={node}
    for t in device.resolve().rglob('ttyACM*'):
        related.add('/dev/'+t.name)
    found=[];unreadable=[]
    for p in Path('/proc').iterdir():
        if not p.name.isdigit():continue
        try:
            for fd in (p/'fd').iterdir():
                if own_fd is not None and int(p.name)==os.getpid() and fd.name==str(own_fd):continue
                try:target=os.readlink(fd)
                except FileNotFoundError:continue
                if target in related:found.append(dict(pid=int(p.name),path=target))
        except FileNotFoundError:continue
        except PermissionError:unreadable.append(int(p.name))
    if unreadable:raise ValueError('cannot establish USB/CDC ownership')
    if found:raise ValueError('existing USB/CDC owner; refusing to seize '+str(found))
    return found


class Control(ctypes.Structure):
    _fields_=[('request_type',ctypes.c_uint8),('request',ctypes.c_uint8),('value',ctypes.c_uint16),
              ('index',ctypes.c_uint16),('length',ctypes.c_uint16),('timeout',ctypes.c_uint32),('data',ctypes.c_void_p)]


def ioctl(fd,number,arg):
    libc=ctypes.CDLL(None,use_errno=True)
    result=libc.ioctl(fd,number,arg)
    if result<0:
        code=ctypes.get_errno();raise OSError(code,os.strerror(code))
    return result


def request(fd,interface,number,length,allow_local_estimate=False):
    if type(allow_local_estimate) is not bool:raise ValueError('local-only diagnostic permission must be boolean')
    if (number,length) not in ((0x51,80),(0x52,256),(0x53,256),(0x55,64),(0x57,256)):
        raise ValueError('not an allowlisted vendor-IN timing request')
    # UFA1 requires an OUT marker first. This IN-only capture deliberately does
    # not send one or infer a token. UTE1 requires a separately named local-only
    # diagnostic option and never replaces a strict source request.
    if number==0x55 or number==0x57 and not allow_local_estimate:raise ValueError('presentation/marker request disabled in default observer')
    buf=ctypes.create_string_buffer(length);ctl=Control(0xc1,number,0,interface,length,1000,ctypes.addressof(buf))
    before=clocks();row=dict(request=number,before=before)
    try:
        n=ioctl(fd,(3<<30)|(ctypes.sizeof(Control)<<16)|(ord('U')<<8),ctypes.byref(ctl))
        row.update(length=n,wire_hex=buf.raw[:n].hex())
        if n!=length:row['error']=dict(errno=None,message='short timing response')
    except OSError as e:
        row['error']=dict(errno=e.errno,message=str(e))
    row['after']=clocks();return row


def marker(fd,interface,token):
    """Only authorized OUT: transient, zero-data UFA1 snapshot then its IN.

    No generic OUT transfer API is exposed. Failure never retries the OUT or
    fetches a different token. The OUT bracket, not IN arrival, is the witness.
    """
    if type(token) is not int or not 1<=token<=65535:raise ValueError('invalid marker token')
    row=dict(token=token,out_before=clocks())
    number=(3<<30)|(ctypes.sizeof(Control)<<16)|(ord('U')<<8)
    out=Control(0x41,0x54,token,interface,0,1000,None)
    try:
        n=ioctl(fd,number,ctypes.byref(out))
        row['out_after']=clocks();row['out_length']=n
        if n!=0:raise ValueError('nonzero marker OUT length')
        buf=ctypes.create_string_buffer(64);fetch=Control(0xc1,0x55,token,interface,64,1000,ctypes.addressof(buf))
        row['in_before']=clocks();n=ioctl(fd,number,ctypes.byref(fetch));row['in_after']=clocks()
        row.update(length=n,wire_hex=buf.raw[:n].hex())
        if n!=64:raise ValueError('short marker fetch')
    except (OSError,ValueError) as e:
        row.setdefault('out_after',clocks());row['error']=dict(errno=getattr(e,'errno',None),message=str(e))
    return row


class NativeRead(ctypes.Structure):
    _fields_=[('before',ctypes.c_uint64),('after',ctypes.c_uint64),('mfindex',ctypes.c_uint32),('valid',ctypes.c_uint32)]


class RuntimeReader:
    def __init__(self,controller,method='seek-read',native_source=None):
        if method not in ('seek-read','pread','native-pread'):raise ValueError('unreviewed host register read method')
        self.method=method
        if (controller/'driver').resolve().name!='xhci-hcd' or read(controller,'power/runtime_status')!='active':
            raise ValueError('controller must already be active under xhci-hcd')
        if (controller.parent/'of_node/compatible').read_bytes()!=b'snps,dwc3\0':
            raise ValueError('unreviewed controller family')
        self.path=Path('/sys/kernel/debug/usb/xhci')/controller.name/'reg-runtime'
        self.fd=os.open(self.path,os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW)
        self.reads=0;self.budget_start=time.monotonic_ns();self.used=0
        self.native_temp=None;self.native_build=None;self.native_partial_reads=[]
        if method=='native-pread':
            try:
                if not isinstance(native_source,str):raise ValueError('reviewed native user-space source required')
                compiler=shutil.which('cc')
                if not compiler:raise ValueError('existing public C compiler unavailable; no installation attempted')
                self.native_temp=tempfile.TemporaryDirectory(prefix='ptob-usb-runtime-')
                directory=Path(self.native_temp.name);source=directory/'reader.c';library=directory/'reader.so'
                source.write_text(native_source)
                argv=[compiler,'-std=c11','-Wall','-Wextra','-Werror','-O2','-fPIC','-shared',str(source),'-o',str(library)]
                before=time.monotonic_ns();cpu=resource.getrusage(resource.RUSAGE_CHILDREN)
                build=subprocess.run(argv,capture_output=True,text=True,timeout=20)
                after=time.monotonic_ns();end=resource.getrusage(resource.RUSAGE_CHILDREN)
                self.native_build=dict(command=argv,exit_code=build.returncode,stdout=build.stdout,stderr=build.stderr,
                    source_sha256=hashlib.sha256(native_source.encode()).hexdigest(),elapsed_monotonic_ns=after-before,
                    compiler_child_cpu_ns=round(((end.ru_utime-cpu.ru_utime)+(end.ru_stime-cpu.ru_stime))*1e9),
                    startup_included_in_population_elapsed=True,temporary_library_removed_at_exit=False)
                if build.returncode:raise ValueError('native helper compile failed: '+build.stderr[-2000:])
                self.native_build['library_sha256']=hashlib.sha256(library.read_bytes()).hexdigest()
                self.native_library=ctypes.CDLL(str(library),use_errno=True)
                self.native_function=self.native_library.ptob_runtime_burst
                self.native_function.argtypes=[ctypes.c_int,ctypes.c_uint32,ctypes.c_uint64,ctypes.POINTER(NativeRead),ctypes.POINTER(ctypes.c_uint32)]
                self.native_function.restype=ctypes.c_int
                self.native_clock_function=self.native_library.ptob_realtime_pair
                self.native_clock_function.argtypes=[ctypes.POINTER(RealtimePair)]
                self.native_clock_function.restype=ctypes.c_int
            except BaseException as e:
                os.close(self.fd)
                if self.native_temp:
                    self.native_temp.cleanup()
                    if self.native_build:self.native_build['temporary_library_removed_at_exit']=True
                if self.native_build:e.native_build=self.native_build
                raise

    def native_reads(self,limit):
        now=time.monotonic_ns()
        if now-self.budget_start>=1_000_000_000:self.budget_start=now;self.used=0
        if self.used+limit>1024:raise ValueError('MFINDEX read budget exhausted')
        rows=(NativeRead*24)();count=ctypes.c_uint32();ctypes.set_errno(0)
        result=self.native_function(self.fd,limit,500000,rows,ctypes.byref(count));error=ctypes.get_errno()
        self.used+=count.value;self.reads+=count.value
        self.native_partial_reads=[dict(mfindex=int(r.mfindex) if r.valid else None,before_raw_ns=int(r.before),
            after_raw_ns=int(r.after),valid=bool(r.valid),method=self.method) for r in rows[:count.value]]
        if result:
            failure=ValueError('native runtime read failed errno='+str(error))
            failure.native_partial_reads=self.native_partial_reads;raise failure
        return self.native_partial_reads

    def sample(self):
        if self.method=='native-pread':return self.native_reads(1)[0]
        now=time.monotonic_ns()
        if now-self.budget_start>=1_000_000_000:self.budget_start=now;self.used=0
        if self.used>=1024:raise ValueError('MFINDEX read budget exhausted')
        self.used+=1;self.reads+=1
        before=time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        if self.method=='pread':data=os.pread(self.fd,1024,0)
        else:
            os.lseek(self.fd,0,os.SEEK_SET);data=os.read(self.fd,1024)
        after=time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        found=re.findall(rb'^MFINDEX\s*=\s*0x([0-9a-fA-F]{8})$',data,re.M)
        if len(found)!=1 or int(found[0],16)>=16384:raise ValueError('unrecognized MFINDEX register view')
        return dict(mfindex=int(found[0],16),before_raw_ns=before,after_raw_ns=after,method=self.method)

    def burst(self):
        if self.method=='native-pread':return self.native_reads(24)
        rows=[];last=None;transitions=0
        for _ in range(24):
            r=self.sample();rows.append(r)
            if last is not None and (r['mfindex']-last)%16384==1:transitions+=1
            last=r['mfindex']
            if transitions>=2 or r['after_raw_ns']-rows[0]['before_raw_ns']>=500_000:break
        return rows

    def close(self):
        os.close(self.fd)
        if self.native_temp:
            self.native_temp.cleanup();self.native_build['temporary_library_removed_at_exit']=True


def environment():
    def optional(path):
        try:return read(path)
        except OSError:return None
    def command(argv):
        try:return subprocess.run(argv,capture_output=True,text=True,timeout=5).stdout.splitlines()
        except (OSError,subprocess.TimeoutExpired) as e:return ['OBSERVATION_ERROR: '+str(e)]
    irq=[line for line in read('/proc/interrupts').splitlines() if 'ptob-pps' in line]
    irq_numbers=[line.split(':')[0].strip() for line in irq]
    processes=command(['ps','-eo','pid,comm,args'])
    clock_names={'chronyd','phc2sys','ptp4l','ptpd','systemd-timesyncd','ntpd','timemaster','ts2phc'}
    return dict(clocks=clocks(),loadavg=optional('/proc/loadavg'),stat=optional('/proc/stat'),
                services=command(['systemctl','is-active','chrony','ptob-telemetry','ptob-dashboard',
                                  'ptob-phc2sys','ptob-ptp4l','ptob-ptp-wifi']),
                clock_processes=[line for line in processes if len(line.split())>=2 and line.split()[1] in clock_names],
                pps_irq=irq,irq_affinity={i:optional(Path('/proc/irq')/i/'smp_affinity_list') for i in irq_numbers},
                temperature_mc=optional('/sys/class/thermal/thermal_zone0/temp'),
                clocksource=optional('/sys/devices/system/clocksource/clocksource0/current_clocksource'),
                available_clocksources=optional('/sys/devices/system/clocksource/clocksource0/available_clocksource'),
                governor=optional('/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor'))


def prepare_host(runtime,seconds,result,audit):
    """Bounded host-only observations preceding the fixed acquisition window.

    Populate the caller-owned result before reads, so failures retain every
    attempted burst. This does not train or warm up any source-map model.
    """
    if type(seconds) is not int or not 0<=seconds<=10 or seconds and runtime is None:
        raise ValueError('host preparation requires0..10seconds and an enabled runtime observer')
    result.update(requested_seconds=seconds,rate_hz=8,bursts=[],start=clocks(),source_requests=0)
    start=time.monotonic_ns();next_read=start;next_audit=start
    try:
        while time.monotonic_ns()-start<seconds*1_000_000_000:
            now=time.monotonic_ns()
            if now<next_read:time.sleep((next_read-now)/1e9)
            if time.monotonic_ns()-start>=seconds*1_000_000_000:break
            if time.monotonic_ns()>=next_audit:
                audit();next_audit=time.monotonic_ns()+1_000_000_000
            burst=dict(clocks=clocks());result['bursts'].append(burst)
            burst['reads']=runtime.burst()
            next_read+=125_000_000
            if next_read<=time.monotonic_ns():next_read=time.monotonic_ns()+125_000_000
    finally:
        result['elapsed_monotonic_ns']=time.monotonic_ns()-start;result['end']=final_clocks(result.setdefault('errors',[]))


def capture(serial,seconds=60,rate=8,mfindex=True,markers=False,host_read_method='seek-read',native_source=None,local_estimate=False,realtime_bracket_method='legacy-wide',host_warmup_seconds=0):
    global _realtime_pair_reader
    previous_pair_reader=_realtime_pair_reader
    if previous_pair_reader is not None:raise ValueError('another native clock pair context is already active')
    observer_start=time.monotonic_ns();observer_cpu=time.process_time_ns();observer_child=resource.getrusage(resource.RUSAGE_CHILDREN)
    if os.geteuid()!=0:raise ValueError('root is required to audit all owners and claim the timing interface')
    if type(seconds) is not int or not 1<=seconds<=600 or rate not in (8,16,32):
        raise ValueError('bounded capture requires seconds1..600, rate8/16/32')
    if markers and not mfindex:raise ValueError('marker candidate requires host-counter observation')
    if type(local_estimate) is not bool:raise ValueError('local estimate diagnostic requires explicit boolean')
    if realtime_bracket_method not in ('legacy-wide','native-pair') or realtime_bracket_method=='native-pair' and (host_read_method!='native-pread' or not mfindex):
        raise ValueError('native realtime pair requires the explicit native-pread helper')
    if host_read_method not in ('seek-read','pread','native-pread'):raise ValueError('unreviewed host register read method')
    if type(host_warmup_seconds) is not int or not 0<=host_warmup_seconds<=10 or host_warmup_seconds and not mfindex:
        raise ValueError('host warmup requires0..10seconds and host register observation')
    if not callable(globals().get('decode')):raise ValueError('strict wire decoder must be bundled before hardware access')
    os.sched_setaffinity(0,{0,1,2});info,device,controller=discover(serial)
    if mfindex and (not info['direct_root_port'] or info['speed_mbps']!='12'):
        raise ValueError('MFINDEX experiment requires direct full-speed topology')
    info['owners_before']=owners(info['usbfs'],device)
    directory=Path('/run/lock/ptob-usb');directory.mkdir(mode=0o700,exist_ok=True)
    st=directory.lstat()
    if not stat.S_ISDIR(st.st_mode) or st.st_uid!=0 or st.st_mode & 0o077:raise ValueError('unsafe ownership directory')
    lock=os.open(directory/(info['silicon']+'.lock'),os.O_CREAT|os.O_RDWR|os.O_CLOEXEC|os.O_NOFOLLOW,0o600)
    st=os.fstat(lock)
    if not stat.S_ISREG(st.st_mode) or st.st_uid!=0 or st.st_nlink!=1 or st.st_mode & 0o077:raise ValueError('unsafe ownership lock')
    fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    fd=None;runtime=None;claimed=False;interface=ctypes.c_uint(info['interface'])
    out=dict(schema=1,mode='SHADOW_UFA1_MARKERS' if markers else 'SHADOW_IN_ONLY',device=info,kernel=platform.release(),
             source_sha256=globals().get('PTOB_SOURCE_SHA256'),boot_id=read('/proc/sys/kernel/random/boot_id'),
             clock_resolutions_s={k:time.clock_getres(getattr(time,k)) for k in ('CLOCK_MONOTONIC_RAW','CLOCK_MONOTONIC','CLOCK_BOOTTIME','CLOCK_REALTIME','CLOCK_TAI')},
             rate=rate,requested_seconds=seconds,observer_cpus=sorted(os.sched_getaffinity(0)),
             register_read_budget_per_s=1024,register_burst_max_ns=500000,register_burst_max_reads=24,
             register_burst_time_is_stop_threshold_not_latency_bound=True,
             host_read_method=host_read_method,
             host_warmup_seconds=host_warmup_seconds,
             realtime_bracket_method=realtime_bracket_method,
             local_estimate_diagnostic_hz=1 if local_estimate else 0,
             acquisition_window_begins_after_preparation=True,acquisition_window_ends_before_cleanup=True,
             samples=[],pps=[],status=[],errors=[],interface_released=False)
    start_cpu=time.process_time_ns();start=time.monotonic_ns();out['start']=clocks();last_pps=None;next_status=start
    next_marker=start;token=0;physical_boot=None;last_marker_sequence=0
    try:
        out['environment_before']=environment()
        ppsdev=Path('/dev/pps-ptob').resolve(strict=True);ppsbase=Path('/sys/class/pps')/ppsdev.name
        if not read(ppsbase,'name').startswith('ptob-pps'):raise ValueError('wrong PPS control device')
        out['pps_device']=str(ppsdev)
        fd=os.open(info['usbfs'],os.O_RDWR|os.O_CLOEXEC|os.O_NOFOLLOW)
        # Recheck sysfs identity after open; never reconnect a changed device.
        fresh,_,_=discover(serial)
        if fresh!=dict((k,v) for k,v in info.items() if k!='owners_before'):raise ValueError('USB enumeration changed before claim')
        ioctl(fd,0x8004550f,ctypes.byref(interface));claimed=True
        if mfindex:
            runtime=RuntimeReader(controller,host_read_method,native_source);out['register_path']=str(runtime.path)
            if runtime.native_build:out['native_build']=runtime.native_build
            if realtime_bracket_method=='native-pair':_realtime_pair_reader=runtime.native_clock_function
        def audit():
            if read(controller,'power/runtime_status')!='active':raise ValueError('controller runtime state changed')
            if read('/proc/sys/kernel/random/boot_id')!=out['boot_id']:raise ValueError('host boot changed')
            fresh,_,_=discover(serial,owned=True)
            if any(fresh[k]!=info[k] for k in fresh):raise ValueError('USB enumeration/topology changed')
            owners(info['usbfs'],device,own_fd=fd)
        if host_warmup_seconds:
            out['host_preparation_warmup']={}
            prepare_host(runtime,host_warmup_seconds,out['host_preparation_warmup'],audit)
        # Preparation/compilation and cleanup remain explicit observer cost,
        # outside the predeclared acquisition window. Historical receipts used
        # an earlier start; never rewrite those populations to this protocol.
        out['preparation']=dict(elapsed_monotonic_ns=time.monotonic_ns()-observer_start,
            process_cpu_ns=time.process_time_ns()-observer_cpu,completed=clocks(),register_reads=runtime.reads if runtime else 0)
        start=time.monotonic_ns();start_cpu=time.process_time_ns();out['start']=clocks();next_status=start;next_marker=start
        if runtime and runtime.native_build:runtime.native_build['startup_included_in_population_elapsed']=False
        next_sample=start
        while time.monotonic_ns()-start<seconds*1_000_000_000:
            now=time.monotonic_ns()
            if now<next_sample:time.sleep((next_sample-now)/1e9)
            if time.monotonic_ns()-start>=seconds*1_000_000_000:break
            now=time.monotonic_ns();row=dict(index=len(out['samples']),clocks=clocks())
            out['samples'].append(row)  # Retain partial attempts if a later observation fails.
            if runtime:row['host_before']=runtime.burst()
            if markers and now>=next_marker:
                token+=1;row['marker']=marker(fd,info['interface'],token)
                if runtime:row['host_marker_after']=[runtime.sample()]
                if 'error' in row['marker']:raise ValueError('marker transport failed; no retry/reset/rebind')
                anchor=decode(bytes.fromhex(row['marker']['wire_hex']),info['silicon'])
                if anchor['contract']!='UFA1' or anchor['token']!=token or anchor['sequence']<=last_marker_sequence:
                    raise ValueError('marker contract/token/sequence mismatch')
                last_marker_sequence=anchor['sequence']
                if physical_boot is None:physical_boot=anchor['boot']
                if anchor['boot']!=physical_boot:raise ValueError('MCU boot changed; fresh capture required')
                next_marker+=1_000_000_000
                if next_marker<=now:next_marker=now+1_000_000_000
            row['source']=request(fd,info['interface'],0x53,256)
            if 'error' in row['source']:raise ValueError('source transport failed; no retry/reset/rebind')
            decoded=decode(bytes.fromhex(row['source']['wire_hex']),info['silicon'])
            if decoded['contract']!='UTS1' or decoded['kind']!=1:raise ValueError('wrong source contract/kind')
            if physical_boot is None:physical_boot=decoded['boot']
            if decoded['boot']!=physical_boot:raise ValueError('MCU boot changed; fresh capture required')
            row['source_live_state']=dict(valid=decoded['valid'],reason=decoded['reason'],usb_epoch=decoded['usb_epoch'],
                                          capture_epoch=decoded['capture_epoch'],map_generation=decoded['map_generation'])
            if runtime:row['host_after']=[runtime.sample()]
            if now>=next_status:
                row['current']=request(fd,info['interface'],0x52,256)
                if 'error' in row['current']:raise ValueError('current-counter transport failed')
                current=decode(bytes.fromhex(row['current']['wire_hex']),info['silicon'])
                if current['contract']!='UTS1' or current['kind']!=2 or current['boot']!=physical_boot:
                    raise ValueError('current-counter contract/kind/MCU boot mismatch')
                if local_estimate:
                    row['estimate']=request(fd,info['interface'],0x57,256,allow_local_estimate=True)
                    if 'error' in row['estimate']:raise ValueError('local estimate diagnostic transport failed')
                    estimate=decode(bytes.fromhex(row['estimate']['wire_hex']),info['silicon'])
                    if estimate['contract']!='UTE1' or estimate['measurement'] or estimate['boot']!=physical_boot:
                        raise ValueError('local estimate contract/MCU boot mismatch')
                # UTS1 already carries the captured SOF. The legacy UFS1
                # observer fences the global USB epoch after >1s between its
                # own polls; sparse diagnostic requests would disturb source
                # continuity even while UTS1 delivery is healthy. Do not poll
                # that separate state machine in this source campaign.
                a=clocks()
                try:status=json.loads(read('/run/ptob-dashboard/status.json'))
                except (OSError,ValueError) as e:status=dict(error=str(e))
                out['status'].append(dict(before=a,after=clocks(),status=status))
                next_status+=1_000_000_000
                if next_status<=now:next_status=now+1_000_000_000
                audit()
            a=clocks();value=read(ppsbase,'assert')
            if value!=last_pps:
                out['pps'].append(dict(before=a,after=clocks(),assertion=value,seed=last_pps is None));last_pps=value
            if 'error' in row['source']:
                raise ValueError('vendor timing transport failed; no automatic reset/rebind')
            next_sample+=1_000_000_000//rate
            if next_sample<=time.monotonic_ns():
                out['errors'].append(dict(stage='cadence',clocks=clocks(),missed_schedule=True))
                next_sample=time.monotonic_ns()+1_000_000_000//rate
    except BaseException as e:
        out['errors'].append(dict(stage='capture',error=str(e),exception=type(e).__name__,clocks=cleanup_clocks()))
        if hasattr(e,'native_partial_reads'):out['errors'][-1]['native_partial_reads']=e.native_partial_reads
        if hasattr(e,'native_build'):out['native_build']=e.native_build
    finally:
        try:out['end']=final_clocks(out['errors'])
        finally:_realtime_pair_reader=previous_pair_reader
        out['process_cpu_ns']=time.process_time_ns()-start_cpu
        out['elapsed_monotonic_ns']=time.monotonic_ns()-start
        if runtime:
            try:runtime.close()
            except Exception as e:out['errors'].append(dict(stage='runtime_cleanup',error=str(e)))
            out['register_reads']=runtime.reads
            out['register_reads_preparation']=out.get('preparation',{}).get('register_reads',0)
            out['register_reads_acquisition']=runtime.reads-out['register_reads_preparation']
        if claimed:
            try:ioctl(fd,0x80045510,ctypes.byref(interface));out['interface_released']=True
            except OSError as e:out['errors'].append(dict(stage='release',error=str(e)))
        if fd is not None:os.close(fd)
        os.close(lock)
        out['max_rss_kib']=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        out['environment_after']=environment()
        children=resource.getrusage(resource.RUSAGE_CHILDREN)
        out['observer_total']=dict(elapsed_monotonic_ns=time.monotonic_ns()-observer_start,
            process_cpu_ns=time.process_time_ns()-observer_cpu,
            child_cpu_ns=round(((children.ru_utime-observer_child.ru_utime)+(children.ru_stime-observer_child.ru_stime))*1e9),
            includes='capture function preparation, compiler/environment child tasks, acquisition, release, cleanup and final environment')
    return out
