"""Bounded post-window local/egress protocol health. Jonathan Yantis. MIT."""
import datetime,hashlib,json,pathlib,secrets,socket,struct,subprocess,time
P=pathlib.Path
def status():
 s=json.loads(P('/run/ptob-dashboard/status.json').read_text())
 return dict(observed_boottime_s=s.get('observed_boottime_s'),source_status=s.get('source_status'),chain=s.get('chain'),tracking=s.get('tracking'),ethernet_link=s.get('ethernet_link'))
binding=json.loads(P('/etc/ptob/pi5/receiver.json').read_text())
if binding.get('particle_id')!=PARAMS['module_id'] or PARAMS['source'] not in binding.get('allowed_sources',[]):raise ValueError('exact enrolled HAT binding mismatch')
start=status()
initial_source_complete=bool(start['chain'].get('complete') and start['tracking'].get('reference')=='PTOB')
out=dict(schema=1,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),initial=start,ntp=[],ptp_packets=[],errors=[],clock_settings_changed=False,
 limits=['Four NTP responses are to Pi loopback, not remote client network delivery.','PTP is passive Pi wlan0 egress protocol observation, not remote receipt or timing accuracy.','WiFi uses software timestamps/ARB timescale; Ethernet unconnected.','No independent UTC or electrical output accuracy is measured.'])
out['input_chain_complete_initial']=initial_source_complete
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
 s.settimeout(2);s.connect(('127.0.0.1',123))
 for i in range(4):
  packet=bytearray(48);packet[0]=0x23;token=secrets.token_bytes(8);packet[40:48]=token;a=time.monotonic_ns()
  try:
   s.send(packet);data=s.recv(4096);b=time.monotonic_ns()
   if len(data)<48 or data[24:32]!=token or data[0]&7!=4:raise ValueError('NTP reply/origin mismatch')
   out['ntp'].append(dict(valid_response=data[0]>>6!=3 and 0<data[1]<16,stratum=data[1],leap=data[0]>>6,refid_hex=data[12:16].hex(),refid_ptob=data[12:16]==b'PTOB',local_round_trip_ns=b-a))
  except BaseException as e:out['ntp'].append(dict(valid_response=False,error=repr(e)))
  time.sleep(.1)
try:
 config=P('/etc/ptob/pi5/ptpd-wifi.conf').read_text();out['ptpd_config_sha256']=hashlib.sha256(config.encode()).hexdigest()
 out['ptpd_mode_lines']=[line for line in config.splitlines() if any(k in line for k in ['interface','domain','timescale','timestamp','unicast','ip_mode','masteronly','slaveonly'])]
 out['routes']=subprocess.run(['ip','-j','route'],capture_output=True,text=True,timeout=5).stdout
 with socket.socket(socket.AF_PACKET,socket.SOCK_RAW,socket.htons(3)) as s:
  s.bind(('wlan0',0));s.settimeout(.5);begin=time.monotonic();out['ptp_observation_start_boottime_s']=time.clock_gettime(time.CLOCK_BOOTTIME)
  while time.monotonic()-begin<12:
   try:frame,addr=s.recvfrom(65535)
   except socket.timeout:continue
   if len(frame)<42 or frame[12:14]!=b'\x08\x00':continue
   ip=frame[14:];ihl=(ip[0]&15)*4
   if ip[0]>>4!=4 or ip[9]!=17 or len(ip)<ihl+8:continue
   src,dst,length,checksum=struct.unpack_from('!HHHH',ip,ihl)
   if src not in (319,320) and dst not in (319,320):continue
   data=ip[ihl+8:ihl+length]
   if len(data)<34 or data[1]&15!=2 or data[4]!=24:continue
   size=struct.unpack_from('!H',data,2)[0]
   if size<34 or size>len(data):continue
   out['ptp_packets'].append(dict(elapsed_s=time.monotonic()-begin,packet_type=addr[2],egress=addr[2]==socket.PACKET_OUTGOING,source_ip=socket.inet_ntoa(ip[12:16]),destination_ip=socket.inet_ntoa(ip[16:20]),source_port=src,destination_port=dst,
    message_type=data[0]&15,sequence=struct.unpack_from('!H',data,30)[0],source_port_identity=data[20:30].hex(),domain=data[4],bytes=size))
  out['ptp_observation_seconds']=time.monotonic()-begin
except BaseException as e:out['errors'].append(dict(stage='PTP egress observation',error=repr(e)))
tx=[x for x in out['ptp_packets'] if x['egress']]
pairs={(x['source_port_identity'],x['sequence']) for x in tx if x['message_type']==0}&{(x['source_port_identity'],x['sequence']) for x in tx if x['message_type']==8}
out['summary']=dict(ntp_loopback_valid=sum(x['valid_response'] for x in out['ntp']),ntp_loopback_total=len(out['ntp']),ntp_loopback_ptob_refid=sum(x.get('refid_ptob') is True for x in out['ntp']),
 ptp_egress_packets=len(tx),ptp_egress_sync_follow_up_pairs=len(pairs),ptp_egress_announces=sum(x['message_type']==11 for x in tx),remote_client_delivery_measured=False,independent_accuracy_measured=False)
out['final']=status()
out['tai_realtime_brackets']=[]
try:
 for i in range(16):
  a=time.clock_gettime_ns(time.CLOCK_REALTIME);t=time.clock_gettime_ns(time.CLOCK_TAI);b=time.clock_gettime_ns(time.CLOCK_REALTIME)
  out['tai_realtime_brackets'].append(dict(realtime_before_ns=a,tai_ns=t,realtime_after_ns=b,bracket_ns=b-a,
   tai_minus_realtime_lower_ns=t-b,tai_minus_realtime_upper_ns=t-a,tai_minus_realtime_midpoint_ns=t-(a+b)//2))
except BaseException as e:out['errors'].append(dict(stage='CLOCK_TAI read-only brackets',error=repr(e)))
out['chrony_config']=dict(path='/etc/chrony/chrony.conf')
try:
 cfg=P('/etc/chrony/chrony.conf').read_bytes();out['chrony_config'].update(sha256=hashlib.sha256(cfg).hexdigest(),text=cfg.decode())
except BaseException as e:out['chrony_config']['error']=repr(e)
out['right_utc']=dict(path='/usr/share/zoneinfo/right/UTC',exists=P('/usr/share/zoneinfo/right/UTC').exists())
try:
 utc=P('/usr/share/zoneinfo/right/UTC');out['right_utc'].update(resolved=str(utc.resolve()),sha256=hashlib.sha256(utc.read_bytes()).hexdigest(),bytes=utc.stat().st_size)
except BaseException as e:out['right_utc']['error']=repr(e)
out['post_capture_ptp_configs']={}
for name in ['/etc/ptob/pi5/ptp4l.conf','/etc/ptob/pi5/ptpd-wifi.conf']:
 try:
  data=P(name).read_bytes();out['post_capture_ptp_configs'][name]=dict(sha256=hashlib.sha256(data).hexdigest(),text=data.decode())
 except BaseException as e:out['post_capture_ptp_configs'][name]=dict(error=repr(e))
try:
 p=subprocess.run(['chronyc','-c','-n','tracking'],capture_output=True,text=True,timeout=5);out['chrony_tracking']=dict(rc=p.returncode,stdout=p.stdout,stderr=p.stderr)
except BaseException as e:out['errors'].append(dict(stage='chrony tracking',error=repr(e)))
print(json.dumps(out,allow_nan=False))
