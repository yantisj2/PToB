#!/usr/bin/env python3
"""Inspect a retained offline N30 build; never opens hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,re,struct
from pathlib import Path
from build import sha,validate_inputs,device_header,lora_config_header,validate_configuration,check_main_stack,validate_transmitter,load_registry,provisional_profile,validate_profile_mode,fine_profile,fine_header,clock_backend

def validate_layout(out,r):
 data=(Path(out)/'target/partition_table/partition-table.bin').read_bytes();apps=[]
 for start in range(0,len(data)-31,32):
  magic,kind,subtype,offset,size=struct.unpack_from('<HBBII',data,start)
  if magic!=0x50aa:continue
  if kind==0:apps.append((subtype,offset,size))
 if apps!=[(0,65536,1048576)]:raise ValueError('Actual application partition differs from reviewed N30 layout')
 if r['application_bytes']>apps[0][2]:raise ValueError('Application exceeds actual slot')

def inspect(out):
 out=Path(out).resolve();r=json.loads((out/'receipt.json').read_text());inputs=out/'local';target=out/'target'
 if r.get('artifact_class')!='PTOB_N30_RC' or r.get('version') not in ('0.1.0','0.1.1','0.1.2','0.1.3','0.1.4','0.1.5','0.1.6','0.1.7','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24') or r.get('profile')!='RC' or r.get('status') not in ('OFFLINE_NOT_FLASHABLE','COMMISSIONING_CANDIDATE_PHYSICAL_UNKNOWN') or r.get('deployment_authorized') is not False or r.get('hardware_validation')!='NOT_PERFORMED':raise ValueError('Offline RC-only N30 receipt required')
 if r['status']=='COMMISSIONING_CANDIDATE_PHYSICAL_UNKNOWN' and (r.get('commissioning_candidate') is not True or r['source_dirty']):raise ValueError('Commissioning candidate requires explicit selection and clean source')
 if not re.fullmatch('[0-9a-f]{40}',r['public_commit']):raise ValueError('Missing source commit')
 for name,digest in r['artifacts'].items():
  if sha(out/name)!=digest:raise ValueError('Build artifact changed: '+name)
 for name,digest in r['sources'].items():
  if sha(out/'source'/name)!=digest:raise ValueError('Source snapshot changed: '+name)
 source_hash=hashlib.sha256(json.dumps(r['sources'],sort_keys=True,separators=(',',':')).encode()).hexdigest()
 if source_hash!=r['source_sha256']:raise ValueError('Source closure identity mismatch')
 e=json.loads((inputs/'enrollment.json').read_text());d=json.loads((inputs/'device.json').read_text());validate_inputs(e,d)
 validate_profile_mode(d,r['estimated_relay'],r['listening_percent'])
 if r.get('provisional_path_profile')!=provisional_profile(d):raise ValueError('Provisional aggregate profile/provenance differs from device input')
 if sha(inputs/'registry.json')!=r['registry_sha256'] or sha(inputs/'registry_generator.py')!=r['registry_generator_sha256'] or sha(inputs/'device.json')!=r['device_config_sha256']:raise ValueError('Input identity changed')
 if (inputs/'ptob_device.h').read_text()!=device_header(d):raise ValueError('Device header does not match N30 inputs')
 headers=[('ptob_device.h','ptob_device.h'),('ptob_registry.h','ptob_registry.h'),('r2-enrollment.h','r2_bench_enrollment.h'),('n30_lora_config.h','n30_lora_config.h')]
 if r['version'] not in ('0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24'):headers=headers[:-1]
 else:
  validate_transmitter(r['lora_transmitter_factory'],d,load_registry(inputs/'registry.json'))
  if (inputs/'n30_lora_config.h').read_text()!=lora_config_header(d,e,r['lora_transmitter_factory']):raise ValueError('LoRa configuration binding mismatch')
 if r['version'] in ('0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24'):
  fp=json.loads((inputs/'fine-profile.json').read_text())
  if fp!=r['fine_capture_profile'] or fine_profile(d,inputs/'fine-profile.json' if fp['mode']!='CURRENT_ONLY' else None)!=fp or (inputs/'fine_capture_config.h').read_text()!=fine_header(fp):raise ValueError('Raw fine-capture profile/header mismatch')
  if r.get('fine_capture')!='RAW_MCPWM1_CURRENT_SW128_RING32_V1':raise ValueError('Missing raw-observer contract')
  headers.append(('fine_capture_config.h','fine_capture_config.h'))
  if r['version'] in ('0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24') and r.get('clock_backend')!=clock_backend(fp):raise ValueError('Fine clock backend identity missing or inconsistent')
 for name,copy in headers:
  if (inputs/name).read_bytes()!=(target/'esp-idf/main'/copy).read_bytes():raise ValueError('Compiler input mismatch')
 identity=json.loads((target/'v12_build_identity.json').read_text())
 fields=[r['public_commit'],sha(target/'sdkconfig'),r['dependencies']['esp_idf']['controller_sha256'],sha(inputs/'r2-enrollment.h'),sha(inputs/'ptob_device.h'),sha(inputs/'ptob_registry.h'),str(r['listening_percent']),'ON' if r['estimated_relay'] else 'OFF','OFF',*([sha(inputs/'n30_lora_config.h')] if r['version'] in ('0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24') else []),'PTOB_N30_'+r['version']]
 if r['version'] in ('0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24'):
  fine_sha=sha(inputs/'fine_capture_config.h');fields.insert(-1,fine_sha)
  if identity.get('fine_capture_header_sha256')!=fine_sha:raise ValueError('Runtime fine-profile binding mismatch')
 binding=hashlib.sha256('|'.join(fields).encode()).hexdigest()
 if binding!=identity['build_id'] or identity!=r['runtime_identity']:raise ValueError('Runtime binding mismatch')
 validate_layout(out,r)
 if r['version']=='0.2.17' and r.get('packet_window_diagnostics')!='FIXED8_CALLBACK64_CONTINUOUS_MCPWM1_V1_DEFAULT_OFF':raise ValueError('N30 packet-window observer contract missing')
 if r['version'] in ('0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24') and r.get('packet_window_diagnostics')!='FIXED128_CALLBACK64_CONTINUOUS_MCPWM1_LATE128_V1_DEFAULT_OFF':raise ValueError('N30 128-window observer contract missing')
 compiled=json.loads((target/'compile_commands.json').read_text());validate_configuration((target/'sdkconfig').read_text(),compiled,lora_rx=r['version'] in ('0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24'))
 if r['version'] in ('0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24'):
  for row in compiled:
   if '/firmware/' in row['file'] and '-DV12_G1_FAILED_PARENT_RETRY_US=60000000' not in row['command'].split():raise ValueError('N30 failed-parent retry budget missing')
 if r['version'] in ('0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24') and r.get('no_time_sync_watchdog_us')!=300000000:raise ValueError('N30 full300s no-time watchdog required')
 if r['version'] in ('0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24') and (r.get('source_recovery_policy')!='ACCUMULATED_LOCK_LOSS_V1' or r.get('physical_fit_policy')!='CAPTURE_FROZEN_DENSE64_1S_ACCEL_V1'):raise ValueError('N30 source recovery and physical fitting policies missing')
 if r['version'] in ('0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24') and r.get('timing_diagnostics')!='RUNTIME_RING16_CURSOR_V1':raise ValueError('N30 runtime diagnostic contract missing')
 if not 0<r['application_bytes']<=r['dependencies']['application_max_bytes'] or sha(out/r['application'])!=r['application_sha256']:raise ValueError('Application bounds/identity mismatch')
 snapshot={};repo=Path(r['app_path']).parents[1];sdk=Path(r['sdk_path'])
 for row in compiled:
  q=Path(row['file']).resolve()
  if q.is_relative_to(sdk):name='sdk/'+str(q.relative_to(sdk));path=q
  elif q.is_relative_to(out):name='generated/'+str(q.relative_to(out));path=q
  elif q.is_relative_to(repo):
   relative=str(q.relative_to(repo));name='repo/'+relative;path=out/'source'/relative
   if r['sources'].get(relative)!=sha(path):raise ValueError('Compiled source missing from closed snapshot')
  else:raise ValueError('Compiled source outside public SDK/repository/generated boundaries')
  snapshot[name]=sha(path)
 if snapshot!=r['compiled_sources']:raise ValueError('Compiled source closure mismatch')
 if r['dependencies']!=json.loads((out/'source/firmware/n30/dependencies.lock.json').read_text()):raise ValueError('Dependency lock differs from source snapshot')
 if check_main_stack(out)!=r['main_stack_budget']:raise ValueError('Stack budget changed')
 return {'status':'COMMISSIONING_FILES_VERIFIED_HARDWARE_REVIEW_REQUIRED' if r['status']=='COMMISSIONING_CANDIDATE_PHYSICAL_UNKNOWN' else 'OFFLINE_BUILD_VERIFIED_NOT_FLASHABLE','profile':'RC','GPS':'ABSENT','runtime_build_id':binding,'application_bytes':r['application_bytes'],'hardware_access':False,'hardware_validation':'NOT_PERFORMED','main_stack_budget':r['main_stack_budget']}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('out',type=Path);a=p.parse_args();print(json.dumps(inspect(a.out),indent=2))
if __name__=='__main__':main()
