#!/usr/bin/env python3
"""Builder header -> production config -> emitted RC7/receiver map, no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1];sys.path.insert(0,str(APP/'tools'))
from build import device_header,validate_inputs,provisional_profile

def main():
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--objects',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 enrollment={'group_id':'123456789abcdef0','key_epoch':1,'psk':'01'*32}
 # Deliberately synthetic factory identity100 matches the production-codec fixture.
 device={'board':'meshnology_w12_esp32s3','factory_mac':'00:00:00:00:00:64','device_id':'0000000000000064','label':'Synthetic','legacy_wire_id':2}
 profile={'schema':1,'scope':'AGGREGATE_PATH_RELATIVE_PPS','offset_ns':47000,'root':'000000000000000b','root_boot':'0000000000000016','source_epoch':9,'receiver':'000000000000012c','receiver_source_sha256':'1'*64,'receiver_phase_correction_ns':10543,'evidence_sha256':'2'*64,'baseline_evidence_sha256':'3'*64,'allow_source_epoch_transfer':True}
 objects=[a.objects.resolve()/(n+'.o') for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec','rc_dynamic','rc_boundary','rc_slots','g1_hmac','sha256','utils')]
 compiler=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').is_file() else 'clang++');rows=[]
 def run_case(name,d,expected,negative=False):
  path=out/name;path.mkdir();validate_inputs(enrollment,d);header=device_header(d);(path/'ptob_device.h').write_text(header)
  if negative:
   old=(APP/'main/rc7_provisional_config.hpp').read_text().replace('#ifdef PTOB_PROVISIONAL_ID\n  return {PTOB_PROVISIONAL_ID,','#ifdef PTOB_PROVISIONAL_PATH_ID\n  return {PTOB_PROVISIONAL_PATH_ID,',1)
   assert old!=(APP/'main/rc7_provisional_config.hpp').read_text();(path/'rc7_provisional_config.hpp').write_text(old)
  config=provisional_profile(d);expected_id=int(config['id'],16) if config else 0
  source='#include "ptob_device.h"\n#include "rc7_provisional_config.hpp"\n#define main standalone_sign_main\n#include "'+str(APP/'tests/rc7_provisional_path_test.cpp')+'"\n#undef main\nint main(){constexpr auto c=v12::rc7_provisional_config();static_assert(c.id==UINT64_C('+str(expected_id)+'));assert(receive_phase(c)=='+str(expected)+');return 0;}\n'
  (path/'program.cpp').write_text(source)
  cmd=[compiler,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-return-type','-fsanitize=address,undefined','-I'+str(path),'-I'+str(APP/'main'),'-I'+str(APP/'main/r2/rc7/include'),'-I'+str(APP/'main/r2/r2-g1'),str(path/'program.cpp'),*map(str,objects),'-o',str(path/'test')]
  (path/'command.json').write_text(json.dumps(cmd,indent=2)+'\n');r=subprocess.run(cmd,capture_output=True,text=True,timeout=40);log=r.stdout+r.stderr;(path/'compile.log').write_text(log)
  if negative:assert r.returncode!=0 and 'Generated provisional profile differs from compiled production config' in log
  else:
   assert r.returncode==0,log;r=subprocess.run([path/'test'],capture_output=True,text=True,timeout=20);(path/'run.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
  rows.append({'case':name,'status':'EXPECTED_OLD_MACRO_COMPILE_REFUSAL' if negative else 'PASS_ACTUAL_GENERATED_CONFIG_AND_EMITTED_TIMESTAMP','generated_header_sha256':hashlib.sha256(header.encode()).hexdigest(),'expected_phase_ns':expected,'profile_id':config['id'] if config else None})
 run_case('default-zero',device,-6940)
 enabled={**device,'provisional_path':profile};run_case('positive-generated',enabled,60)
 run_case('negative-generated',{**device,'provisional_path':{**profile,'offset_ns':-47000}},-93940)
 run_case('old-macro-negative-control',enabled,60,True)
 receipt={'status':'PASS','scope':'ACTUAL_BUILDER_GENERATED_HEADER_TO_PRODUCTION_CONFIG_TO_EMITTED_CODEC_AND_RECEIVER_MAP; SYNTHETIC_NO_RF','cases':rows,'hardware_access':False};(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
if __name__=='__main__':main()
