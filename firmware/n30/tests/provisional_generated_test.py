#!/usr/bin/env python3
"""N30 generated profile -> actual RC7 multicast -> two synthetic receivers.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Run the public W12 G1 host tests first to produce --objects; no hardware I/O.
"""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1];W12=APP.parent/'w12'
sys.path.insert(0,str(APP/'tools'))
from build import device_header,validate_inputs,provisional_profile,validate_profile_mode
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--objects',type=Path,required=True);a=p.parse_args()
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 e={'group_id':'123456789abcdef0','key_epoch':1,'psk':'01'*32}
 d={'board':'meshnology_n30_heltec_v3','factory_mac':'00:00:00:00:00:64','device_id':'0000000000000064','label':'N3-2','legacy_wire_id':4,'display_rotation':'cw'}
 profile={'schema':1,'scope':'AGGREGATE_PATH_RELATIVE_PPS','offset_ns':47133,'root':'000000000000000b','root_boot':'0000000000000016','source_epoch':9,'receiver':'000000000000012c','receiver_source_sha256':'1'*64,'receiver_phase_correction_ns':10543,'evidence_sha256':'2'*64,'baseline_evidence_sha256':'3'*64}
 objects=[a.objects.resolve()/(n+'.o') for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec','rc_dynamic','rc_boundary','rc_slots','g1_hmac','sha256','utils')]
 # Keep the real encoder/HMAC, corrected packet association and receiver map.
 # Only synthetic receiver identities vary; the publisher's output is common.
 original=(W12/'tests/rc7_provisional_path_test.cpp').read_text()
 fixture=original.replace('uint32_t actual_epoch=9){','uint32_t actual_epoch=9,unsigned receiver_slot=0){',1).replace('client.make_request(cap,300,400,','client.make_request(cap,300+receiver_slot,400+receiver_slot,',1)
 assert fixture!=original and 'receiver_slot' in fixture
 (out/'receiver_fixture.cpp').write_text(fixture);cases=[]
 for name,config,expected in [('default',d,-6940),('enabled',{**d,'provisional_path':profile},193),('wrong-root-boot',{**d,'provisional_path':dict(profile,root_boot='0000000000000017')},-6940)]:
  path=out/name;path.mkdir();validate_inputs(e,config);validate_profile_mode(config,True,100)
  header=device_header(config);(path/'ptob_device.h').write_text(header);pr=provisional_profile(config)
  source='#include "ptob_device.h"\n#include "rc7_provisional_config.hpp"\n#define main previous_sign_main\n#include "'+str(out/'receiver_fixture.cpp')+'"\n#undef main\nint main(){constexpr auto c=v12::rc7_provisional_config();static_assert(c.offset_ns=='+str(pr['offset_ns'] if pr else 0)+');for(unsigned receiver=0;receiver<2;receiver++)assert(receive_phase(c,true,9,receiver)=='+str(expected)+');assert(c.select(100,1,11,22,9,true)==0);return 0;}\n'
  (path/'program.cpp').write_text(source)
  compiler=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
  cmd=[compiler,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-return-type','-fsanitize=address,undefined','-I'+str(path),'-I'+str(W12/'main'),'-I'+str(W12/'main/r2/rc7/include'),'-I'+str(W12/'main/r2/r2-g1'),str(path/'program.cpp'),*map(str,objects),'-o',str(path/'test')]
  with (path/'compile.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=40)
  subprocess.run([path/'test'],check=True,timeout=20)
  cases.append({'case':name,'receiver_count':2,'phase_ns':expected,'generated_header_sha256':hashlib.sha256(header.encode()).hexdigest()})
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'ACTUAL_N30_GENERATED_CONFIG_TO_RC7_CODEC_HMAC_ASSOCIATION_RECEIVER_MAP_SYNTHETIC','cases':cases,'hardware_access':False},indent=2)+'\n')
 print('PASS N30 generated publisher-wide offset: two receiver identities, nominal conversion once, wrong scope and zero default')
if __name__=='__main__':main()
