"""Validate actual C++ diagnostic payload through the production CRC parser."""
from pathlib import Path
import argparse,sys,zlib,re
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import framed_console as c
p=argparse.ArgumentParser();p.add_argument('--payload',type=Path,required=True);a=p.parse_args()
nonce='0000000000000001';boot='0000000000000003'
m=dict(factory_mac='02:00:00:00:30:01',source_commit='a'*40,build_id='b'*64,r2_profile='r2-g1',r2_enrollment_sha256='c'*64,r2_group_id='0123456789abcdef',r2_key_epoch=1,r2_client_identity='0000020000003001',n30_frame_kind='DUAL',n30_ui_revision=5)
def frame(payload):
 rows=f"W12_R2_STATUS profile=r2-g1 enrollment_sha={m['r2_enrollment_sha256']} self=3 client_capabilities=0 client_control=0 source=0 serving=0 group_id={m['r2_group_id']} key_epoch=1 client_identity={m['r2_client_identity']}\nW12_V12_STATUS factory={m['factory_mac']} source={m['source_commit']} build={m['build_id']} session={boot} GPS_inputs=0\n"+payload+f"W12_V12_COMMAND nonce={nonce}\nN30_UI profile=RC GPS=ABSENT page=0 display=64x128 path=LOR upstream=AUTO\n"
 body=(f"\nW12_V12_BEGIN version=3 nonce={nonce} ticket=0000000000000002 factory={m['factory_mac']} session={boot} source={m['source_commit']} build={m['build_id']} request_us=1000000 deadline_us=2500000 ticket_deadline_us=3000000\n"+rows).encode()
 end=f'W12_V12_END version=3 nonce={nonce} ticket=0000000000000002 rows=10 crc32={zlib.crc32(body):08x} request_us=1000000 completed_us=1500000 deadline_us=2500000 body_driver_drained=1 counter_exhausted=0 capture_id=0000000000000000 capture_active=0 capture_deadline_us=0'
 for key,value in c.base.COUNTERS.items():end+=f' {key}=0 {value}=0 req_{key}=0'
 end=end.encode();return body+end+f' end_crc32={zlib.crc32(end,zlib.crc32(body)):08x}\n'.encode()
payload=a.payload.read_text();raw=frame(payload);assert len(raw)<=8704
parser=c.Parser(nonce,m);got=[]
for i in range(0,len(raw),17):got+=parser.feed(raw[i:i+17])
assert len(got)==1;c.base.require_coverage(got[0]);assert c.mutation_command(got[0],'0000000000000004','dual read 12 24').endswith(b' dual read 12 24\n')
for old,new in [('authority=UNCHANGED','authority=GRANTED'),('nonconsuming=1','nonconsuming=0'),('parent_lost=24','parent_lost=0'),('source_records=8','source_records=80'),('first_refusal=64','first_refusal=4096')]:
 assert old in payload
 try:c.Parser(nonce,m).feed(frame(payload.replace(old,new)))
 except RuntimeError:pass
 else:raise AssertionError('Invalid state history/authority accepted: '+old)
for command in ('dual read -1 0','dual read 0 4294967296','dual read 00 0','dual read 0 0 extra'):
 try:c.mutation_command(got[0],'0000000000000004',command)
 except RuntimeError:pass
 else:raise AssertionError('Invalid cursor accepted: '+command)
print('PASS actual dual/parent payload fragmented CRC replay, explicit overwritten history, authority/coverage/cursor refusal')
