#!/usr/bin/env python3
"""Offline production TZ codecs and lease tests. MIT, Jonathan Yantis 2026."""
import argparse,hashlib,hmac,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
sources=[app/'protocol'/n for n in ['group_auth.c','gm7_codec.c','gm7_air.c','gm7_schedule.c','group_join.c','rc_dynamic.c']]
sources += [tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]
cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-ffunction-sections','-fdata-sections','-Wl,-dead_strip','-DR2_TZ_WIRE=1',*['-I'+str(d) for d in [app/'include',app/'protocol',tiny/'include']],*map(str,sources),str(app/'tests/time_zone_test.c'),'-o',str(out/'test')]
with (out/'compile.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
with (out/'run.log').open('w') as f:subprocess.run([str(out/'test')],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
# Exercise the actual production GM receipt hook with a past HF capture.
service=(app/'src/gm7_service.c').read_text()
start=service.index('static void observe_coarse_tz(');brace=service.index('{',start)
end=brace+1;depth=1
while depth:
 if service[end]=='{':depth+=1
 elif service[end]=='}':depth-=1
 end+=1
assert 'observe_coarse_tz(&packet, received, b.kernel_hi);' in service
(out/'gm7_tz_observer_under_test.h').write_text(service[start:end]+'\nstatic void under_test(struct gm7_packet p,int received,struct gm7_bridge b,uint64_t rx){(void)rx;observe_coarse_tz(&p,received,b.kernel_hi); }\n')
observer_cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_TZ_WIRE=1','-I'+str(app/'protocol'),'-I'+str(out),str(app/'protocol/gm7_bridge.c'),str(app/'tests/gm7_time_zone_test.c'),'-o',str(out/'gm7-tz-observer')]
with (out/'gm7-observer-compile.log').open('w')as f:subprocess.run(observer_cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
with (out/'gm7-observer-run.log').open('w')as f:subprocess.run([str(out/'gm7-tz-observer')],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
# Frozen synthetic wire vectors plus an independent stdlib HMAC check.
vpath=app.parents[1]/'protocol/time-zone-v1/vectors.json'
v=json.loads(vpath.read_text());actual={line.split()[1]:line.split()[2] for line in (out/'run.log').read_text().splitlines() if line.startswith('VECTOR ')}
ctx=b'\x01'+int(v['group_id'],16).to_bytes(8,'little')+v['key_epoch'].to_bytes(4,'little')
for row in v['vectors']:
 assert actual[row['name']]==row['hex'],row['name']
 w=bytes.fromhex(row['hex'])[row['body_offset']:];tag=row['tag_offset']
 assert w[row['tz_offset']:row['tz_offset']+4]==bytes.fromhex('0200d4fe')
 assert hmac.new(bytes.fromhex(v['key_hex']),row['domain'].encode()+ctx+w[:tag],hashlib.sha256).digest()[:16]==w[tag:]
(out/'receipt.json').write_text(json.dumps({'status':'PASS','command':cmd,'gm_observer_command':observer_cmd},indent=2)+'\n')
print('PASS TZ codecs, signed offsets, coarse 3-period lease/renewal, independent HMAC and frozen synthetic vectors')
