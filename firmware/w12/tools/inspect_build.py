#!/usr/bin/env python3
"""Verify immutable W12 build files and recompute the runtime identity. No USB.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,re
from pathlib import Path
from build import sha,validate_inputs,device_header,provisional_profile

def inspect(out,allow_development=False):
 out=Path(out).resolve();r=json.loads((out/'receipt.json').read_text())
 if r.get('artifact_class')!='PTOB_W12_RC' or r.get('profile')!='RC' or r.get('version') not in ('0.1.0','0.1.1','0.1.2','0.1.3','0.1.4','0.1.5','0.1.6','0.1.7','0.1.8','0.1.9','0.1.10','0.1.11','0.1.12','0.1.13','0.1.14','0.1.15','0.1.16','0.1.17','0.1.18'):raise ValueError('Wrong artifact class/profile/version')
 if not re.fullmatch('[0-9a-f]{40}',r.get('public_commit','')):raise ValueError('Exact source commit missing')
 if not 0<r.get('application_bytes',0)<=1048576 or r.get('listening_percent') not in (50,100) or (r.get('estimated_relay') and r['listening_percent']!=100):raise ValueError('Profile or application bounds changed')
 source_hash=hashlib.sha256(json.dumps(r['sources'],sort_keys=True,separators=(',',':')).encode()).hexdigest()
 if source_hash!=r['source_sha256']:raise ValueError('Source map identity changed')
 allowed={'BUILD_COMPLETE_PHYSICAL_UNKNOWN'}|({'DEVELOPMENT_NOT_FLASHABLE'} if allow_development else set())
 if r['status'] not in allowed or (r['source_dirty'] and not allow_development):raise ValueError('Incomplete or development build cannot be installed')
 for name,digest in r['artifacts'].items():
  if sha(out/name)!=digest:raise ValueError('Build artifact changed: '+name)
 for name,digest in r['sources'].items():
  if sha(out/'source'/name)!=digest:raise ValueError('Immutable source snapshot changed: '+name)
 inputs=out/'local';target=out/'target';e=json.loads((inputs/'enrollment.json').read_text());d=json.loads((inputs/'device.json').read_text());validate_inputs(e,d)
 if r['version'] in ('0.1.16','0.1.17','0.1.18') and r.get('provisional_path_profile')!=provisional_profile(d):raise ValueError('Provisional profile provenance differs from exact device input')
 if sha(inputs/'registry.json')!=r['registry_sha256'] or sha(inputs/'registry_generator.py')!=r['registry_generator_sha256'] or sha(inputs/'device.json')!=r['device_config_sha256']:raise ValueError('Local input identity changed')
 # The generated input, compiler copy and retained build identity must all agree.
 for name,copy in [('ptob_device.h','ptob_device.h'),('ptob_registry.h','ptob_registry.h'),('r2-enrollment.h','r2_bench_enrollment.h')]:
  if (inputs/name).read_bytes()!=(target/'esp-idf/main'/copy).read_bytes():raise ValueError('Compiler local input copy mismatch: '+name)
 identity=json.loads((target/'v12_build_identity.json').read_text())
 fields=[r['public_commit'],sha(target/'sdkconfig'),r['dependencies']['esp_idf']['controller_sha256'],sha(inputs/'r2-enrollment.h'),sha(inputs/'ptob_device.h'),sha(inputs/'ptob_registry.h'),str(r['listening_percent']),'ON' if r['estimated_relay'] else 'OFF','PTOB_W12_'+r['version']]
 binding=hashlib.sha256('|'.join(fields).encode()).hexdigest()
 if identity.get('version')!=r['version']:raise ValueError('Embedded firmware version differs from candidate')
 if binding!=identity['build_id'] or identity!=r['runtime_identity']:raise ValueError('Runtime identity does not bind actual retained configuration/headers')
 if sha(out/r['application'])!=r['application_sha256'] or (out/r['application']).stat().st_size!=r['application_bytes']:raise ValueError('Application identity mismatch')
 compiled=json.loads((target/'compile_commands.json').read_text());snapshot={}
 if r['version'] in ('0.1.3','0.1.4','0.1.5','0.1.6','0.1.7','0.1.8','0.1.9','0.1.10','0.1.11','0.1.12','0.1.13','0.1.14','0.1.15','0.1.16','0.1.17','0.1.18'):
  if (inputs/'ptob_device.h').read_text()!=device_header(d):raise ValueError('Device/GNSS header differs from explicit local profile')
  enabled=int('gnss_profile' in d)
  for suffix in ('/native_main.cpp','/gnss_pps.cpp'):
   command=next(row['command'] for row in compiled if row['file'].endswith(suffix))
   for flag in [f'-DW12_COPY_BOARD_PROFILE={12 if enabled else 2}',f'-DW12_GNSS_VERIFIED_INPUTS={enabled}',f'-DW12_GNSS_W12_POWER={enabled}','-DW12_GNSS_RX_PIN=39','-DW12_GNSS_PPS_PIN=41','-DW12_GNSS_BAUD=9600','-DW12_GNSS_R8_POWER=0','-DV12_LORA_FORCED_OFF=1']:
    if flag not in command.split():raise ValueError('Compiled GNSS route differs from explicit local profile: '+flag)
 for row in compiled:
  file=Path(row['file']).resolve()
  if file.is_relative_to(Path(r['sdk_path'])):snapshot['sdk/'+str(file.relative_to(Path(r['sdk_path'])))]=sha(file)
  elif file.is_relative_to(Path(r['app_path'])):
   name=str(file.relative_to(Path(r['app_path'])));snapshot['app/'+name]=sha(file)
   if r['sources'].get(name)!=snapshot['app/'+name]:raise ValueError('Compiled application source mismatch')
  elif file.is_relative_to(out):snapshot['generated/'+str(file.relative_to(out))]=sha(file)
  else:raise ValueError('Unexpected source outside target/public SDK boundary')
 if snapshot!=r['compiled_sources']:raise ValueError('Compiled source closure changed')
 if r['version'] in ('0.1.7','0.1.8','0.1.9','0.1.10','0.1.11','0.1.12','0.1.13','0.1.14','0.1.15','0.1.16','0.1.17','0.1.18'):
  from stack_budget import check as check_main_stack
  if check_main_stack(out)!=r.get('main_stack_budget'):raise ValueError('Main task stack budget changed')
 return {'schema':1,'status':'IMMUTABLE_FILES_AND_RUNTIME_BINDING_VERIFIED','application_sha256':r['application_sha256'],'application_bytes':r['application_bytes'],'runtime_build_id':binding,'public_commit':r['public_commit'],'version':r['version'],'profile':'RC','estimated_relay':r['estimated_relay'],'actual_TX_capture':False,'precision':'UNQUALIFIED','hardware_access':False,'provisional_path_profile':provisional_profile(d),**({'main_stack_budget':r['main_stack_budget']} if r['version'] in ('0.1.7','0.1.8','0.1.9','0.1.10','0.1.11','0.1.12','0.1.13','0.1.14','0.1.15','0.1.16','0.1.17','0.1.18') else {})}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('out',type=Path);a=p.parse_args();print(json.dumps(inspect(a.out),indent=2))
if __name__=='__main__':main()
