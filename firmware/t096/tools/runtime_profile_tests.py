#!/usr/bin/env python3
"""Changed production profile manager/parser and lifecycle boundaries; no RF."""
import argparse,hashlib,json,subprocess
from pathlib import Path
app=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--deps',type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args();out=a.out.resolve();out.mkdir(exist_ok=False)
for n in ['zephyr/kernel.h','zephyr/sys/atomic.h','zephyr/random/random.h','zephyr/bluetooth/bluetooth.h','zephyr/bluetooth/conn.h','zephyr/bluetooth/gatt.h']:
 f=out/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Public boundary supplied by fixture. */\n')
s=(app/'src/ble_service.c').read_text();first=s[s.index('static void service_stop_owned(bool migration)\n{'):s.index('\nvoid r2_service_stop(void)')];tail=s[s.index('void r2_service_profile_status(uint32_t out[5]){'):s.index('/* RC always observes')]
(out/'runtime_profile_under_test.h').write_text(first+'\n'+tail)
start=s.index('static void __attribute__((noinline)) normal_relay_tick(');part=s.index(' struct r2_local_demand incoming=',start)
(out/'normal_relay_expiry_under_test.h').write_text(s[part:s.index(' if(relay_transition&&!relay_selected)',part)])
relay=(app/'src/relay_service.c').read_text();begin=relay.index('bool r2_relay_service_serving(')
(out/'relay_ready_under_test.h').write_text(relay[begin:relay.index('/* A passive scheduling miss',begin)])
# Preserve actual production manager under test, not a reimplementation.
cc=(__import__('os').environ.get('CC') or __import__('shutil').which('clang'));flags=[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DCONFIG_BT_PER_ADV=1','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol')]
(out/'service_snapshot_under_test.h').write_text(s[s.index('void r2_service_snapshot('):s.index('\nvoid r2_service_ui(')])
(out/'build_identity.h').write_text('#define R2_VERSION "0.14.46"\n#define R2_SOURCE_SHA "'+'a'*64+'"\n#define R2_CHILD_COMMIT "'+'b'*40+'"\n#define R2_SOURCE_DIRTY 0\n')
(out/'r2_enrollment.h').write_text('static const struct r2_enrollment r2_enrollment={0};\n')
results=[]
with (out/'frame-overlap.log').open('x') as log:
 subprocess.run([*flags,str(app/'tests/frame_overlap_test.c'),str(app/'src/core.c'),'-o',str(out/'frame-overlap')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 subprocess.run([str(out/'frame-overlap')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
results.append({'case':'frame-overlap','passed':True})
try:
 for name,files in [('runtime-profile',[app/'tests/runtime_profile_test.c']),('relay-expiry',[app/'tests/normal_relay_expiry_test.c']),('relay-publication-ready',[app/'tests/relay_publication_ready_test.c']),('runtime-parser',[app/'tests/runtime_profile_parse_test.c',app/'src/core.c']),('service-snapshot',[app/'tests/service_snapshot_test.c'])]:
  with (out/(name+'.log')).open('x') as log:
   subprocess.run([*flags,*map(str,files),'-o',str(out/name)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
   subprocess.run([str(out/name)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  results.append({'case':name,'passed':True})
 if a.deps:
  tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib';crypto=[app/'protocol'/x for x in ['role_announcement.c','group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c']]+[tiny/'source'/x for x in ['hmac.c','sha256.c','utils.c']]
  for name,files,extra,modes in [('transport',[app/'tests/runtime_transport_restart_test.c',*crypto],[],[[]]),('radio-reset',[app/'tests/radio_service_test.c',*[app/'src'/n for n in ['radio_precision.c','relay_precision.c','core.c','clock.c','nmea.c','precision.c']],*crypto],[],[['16'],['17'],['20']]),('reply',[app/'tests/reply_test.c',app/'src/core.c'],['-DR2_NATIVE_BLE=1','-include',str(app/'tests/zephyr_fake.h')],[[]])]:
   with (out/(name+'.log')).open('x') as log:
    subprocess.run([*flags,'-I'+str(tiny/'include'),*extra,*map(str,files),'-o',str(out/name)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
    for args in modes:subprocess.run([str(out/name),*args],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
   results.append({'case':name,'passed':True})
finally:(out/'receipt.json').write_text(json.dumps({'scope':'ACTUAL_PRODUCTION_MANAGER_1001_CYCLES_AND_PARSER_ASAN_UBSAN_PUBLIC_BOUNDARIES_NO_RF','results':results,'source_fingerprints':{str(f.relative_to(app)):hashlib.sha256(f.read_bytes()).hexdigest() for f in app.rglob('*') if f.is_file() and '__pycache__' not in f.parts}},indent=2)+'\n')
print('PASS',len(results),'actual production profile manager/parser checks')
