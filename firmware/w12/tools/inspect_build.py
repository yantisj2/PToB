#!/usr/bin/env python3
"""Verify immutable W12 build files and recompute the runtime identity. No USB.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,re
from pathlib import Path
from build import sha,validate_inputs,device_header,provisional_profile,validate_map_compilation,fine_header,validate_fine_compilation,lora_header,validate_dual_compilation,phase_offsets,validate_gm_successor_compilation

def inspect(out,allow_development=False):
 out=Path(out).resolve();r=json.loads((out/'receipt.json').read_text())
 if r.get('version') in ('0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') and r.get('button_power_contract')!='OFF_MAIN_HOLD4_DRAIN2_RELEASE40_GPIO0_WAKE_V1':raise ValueError('Button power contract missing')
 if r.get('version')=='0.2.18' and r.get('time_zone_contract')!='COARSE_TZ_V1_FIXED_MESH_3_PERIOD_LOCAL_DISPLAY':raise ValueError('Coarse TZ contract missing')
 if r.get('version') in ('0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') and r.get('time_zone_contract')!='COARSE_TZ_V1_ORDERED_DYNAMIC_LOCAL_DISPLAY':raise ValueError('Ordered dynamic TZ contract missing')
 if r.get('artifact_class')!='PTOB_W12_RC' or r.get('profile')!='RC' or r.get('version') not in ('0.1.0','0.1.1','0.1.2','0.1.3','0.1.4','0.1.5','0.1.6','0.1.7','0.1.8','0.1.9','0.1.10','0.1.11','0.1.12','0.1.13','0.1.14','0.1.15','0.1.16','0.1.17','0.1.18','0.1.19','0.1.20','0.1.21','0.1.22','0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):raise ValueError('Wrong artifact class/profile/version')
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
 if r['version'] in ('0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') and r.get('fallback_recovery_policy')!=('AUTO_RC9_PASSIVE_NORMAL_RETRY_V1' if r.get('rc9') else None):raise ValueError('RC9 AUTO recovery contract missing')
 if r['version'] in ('0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):
  if r.get('phase_offsets')!=phase_offsets(r['version']):raise ValueError('Phase offset disclosure differs from source')
  header=(out/'source/main/w12_phase_offsets.hpp').read_text()
  for key,value in [('ReceiveNs',7671),('RelayNs',phase_offsets(r['version'])['relay_residual_ns'])]:
   if not re.search(r'constexpr int32_t '+key+r'='+str(value)+r';',header):raise ValueError('Phase offset source mismatch')
 if r['version'] in ('0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') and r.get('bluetooth_clock_map')!=('FINE_RAW_CURRENT_BRACKET' if r['fine_clock'] else 'GPTIMER_DRIVER_BRACKET'):raise ValueError('Bluetooth clock map disclosure mismatch')
 if r['version'] in ('0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') and r.get('programmed_clock_projection')!=('MEAN4_VALIDATED_NO_TRIMMING' if r['estimated_relay'] else None):raise ValueError('Programmed-clock projection disclosure mismatch')
 if r['version']=='0.2.12' and r.get('leader_recovery_policy')!='FENCED_SCOPE_CACHE_CLEAR_1500MS_V1':raise ValueError('Leader recovery contract missing')
 if r['version'] in ('0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') and r.get('rc9_server_recovery_policy')!=('QUALIFIED_BLE_DISPLAY_DRAIN_ORIGINAL_CHILD_LEASE_V1' if r.get('rc9') else None):raise ValueError('RC9 server recovery contract missing')
 if r['version'] in ('0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') and r.get('leader_recovery_policy')!='FENCED_SCOPE_CACHE_CLEAR_1500MS_V3':raise ValueError('Leader recovery contract missing')
 if r['version'] in ('0.2.13','0.2.14') and r.get('leader_recovery_policy')!='FENCED_SCOPE_CACHE_CLEAR_1500MS_V2':raise ValueError('Leader recovery contract missing')
 if r['version'] in ('0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') and r.get('gm_successor_policy')!='GM7_V3_AUTHENTICATED_NOMINEE_RECOVERY_3S_V1':raise ValueError('GM7 successor routing contract missing')
 if r['version'] in ('0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') and r.get('pa_termination_policy')!='SDK_HCI_ACK_EXACT_OWNER_V1':raise ValueError('PA terminate acknowledgement contract missing')
 inputs=out/'local';target=out/'target';e=json.loads((inputs/'enrollment.json').read_text());d=json.loads((inputs/'device.json').read_text());validate_inputs(e,d)
 if r['version'] in ('0.1.16','0.1.17','0.1.18','0.1.19','0.1.20','0.1.21','0.1.22','0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') and r.get('provisional_path_profile')!=provisional_profile(d):raise ValueError('Provisional profile provenance differs from exact device input')
 if sha(inputs/'registry.json')!=r['registry_sha256'] or sha(inputs/'registry_generator.py')!=r['registry_generator_sha256'] or sha(inputs/'device.json')!=r['device_config_sha256']:raise ValueError('Local input identity changed')
 # The generated input, compiler copy and retained build identity must all agree.
 for name,copy in [('ptob_device.h','ptob_device.h'),('ptob_registry.h','ptob_registry.h'),('r2-enrollment.h','r2_bench_enrollment.h')]:
  if (inputs/name).read_bytes()!=(target/'esp-idf/main'/copy).read_bytes():raise ValueError('Compiler local input copy mismatch: '+name)
 identity=json.loads((target/'v12_build_identity.json').read_text())
 fields=[r['public_commit'],sha(target/'sdkconfig'),r['dependencies']['esp_idf']['controller_sha256'],sha(inputs/'r2-enrollment.h'),sha(inputs/'ptob_device.h'),sha(inputs/'ptob_registry.h'),str(r['listening_percent']),'ON' if r['estimated_relay'] else 'OFF','PTOB_W12_'+r['version']]
 if r['version'] in ('0.1.22','0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):
  if type(r.get('gnss_fast_map_diagnostic'))is not bool:raise ValueError('Exact fast-map diagnostic metadata required')
  fields.insert(-1,'ON' if r['gnss_fast_map_diagnostic'] else 'OFF')
 if r['version'] in ('0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):
  for name,digest in r['dependencies']['fine_capture_abi_sha256'].items():
   if sha(Path(r['sdk_path'])/name)!=digest:raise ValueError('Fine-capture private ABI identity changed: '+name)
  fields[-1:-1]=['ON' if r.get('fine_clock') else 'OFF',sha(inputs/'w12_fine_config.h')]
  if (inputs/'w12_fine_config.h').read_text()!=fine_header(d,r.get('fine_clock')) or (inputs/'w12_fine_config.h').read_bytes()!=(target/'esp-idf/main/w12_fine_config.h').read_bytes():raise ValueError('Fine counter header differs from explicit configuration/compiler copy')
 if r['version'] in ('0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):
  fields.insert(-1,sha(inputs/'w12_lora_config.h'))
  if (inputs/'w12_lora_config.h').read_text()!=lora_header(d,e,r['lora_transmitter_factory']) or (inputs/'w12_lora_config.h').read_bytes()!=(target/'esp-idf/main/w12_lora_config.h').read_bytes():raise ValueError('LoRa binding differs from compiler inputs')
  validate_dual_compilation(r,json.loads((target/'compile_commands.json').read_text()))
  for name,digest in r.get('radiolib_sources',{}).items():
   if sha(out/'dependencies/RadioLib'/name)!=digest:raise ValueError('RadioLib snapshot changed')
 if r['version'] in ('0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):fields.insert(-1,'ON' if r.get('rc9') else 'OFF')
 binding=hashlib.sha256('|'.join(fields).encode()).hexdigest()
 if identity.get('version')!=r['version']:raise ValueError('Embedded firmware version differs from candidate')
 if binding!=identity['build_id'] or identity!=r['runtime_identity']:raise ValueError('Runtime identity does not bind actual retained configuration/headers')
 if sha(out/r['application'])!=r['application_sha256'] or (out/r['application']).stat().st_size!=r['application_bytes']:raise ValueError('Application identity mismatch')
 compiled=json.loads((target/'compile_commands.json').read_text());snapshot={}
 if r.get('version') in ('0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):
  if r.get('shadow_holdover_contract')!=('BLE_THERMAL_SHADOW_120S_RATE_ANCHOR_V3' if r['version'] in ('0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') else 'BLE_THERMAL_SHADOW_120S_RAM128_CROSS1US_V2' if r['version']=='0.2.23' else 'BLE_THERMAL_SHADOW_1S_RAM128_P99_V1'):raise ValueError('Shadow holdover contract missing')
  for entry in compiled:
   if '/main/' in entry.get('file','') and entry['file'].endswith(('native_g1.cpp','native_main.cpp','display.cpp')) and '-DPTOB_W12_SHADOW_HOLDOVER=1' not in entry.get('command',''):raise ValueError('Shadow display/compiler selection missing')
 if r['version'] in ('0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):
  if r.get('holdover_servo_contract')!=('BLE_RATE_CONSENSUS_TEMPERATURE_SHADOW_V3' if r['version'] in ('0.2.27','0.2.28','0.2.29','0.2.30') else 'BLE_RATE_RETAINED_TEMPERATURE_SHADOW_V2' if r['version'] in ('0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') else 'BLE_THERMAL_RETAINED_PRODUCTION_V1') or r.get('holdover_test_contract')!='FINITE_LOCAL_OUTPUT_120S_BLE_WITNESS_V1':raise ValueError('Production holdover contract missing')
  for entry in compiled:
   if '/main/' in entry.get('file','') and entry['file'].endswith(('native_g1.cpp','native_main.cpp','display.cpp')) and '-DPTOB_W12_HOLDOVER_PRODUCTION=1' not in entry.get('command',''):raise ValueError('Production holdover compiler selection missing')
 if r['version'] in ('0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):
  if r.get('holdover_temperature_policy')!='SHADOW_ONLY':raise ValueError('Temperature shadow-only policy missing')
  for entry in compiled:
   if '/main/' in entry.get('file','') and entry['file'].endswith(('native_g1.cpp','native_main.cpp','display.cpp')) and '-DPTOB_W12_THERMAL_SHADOW_ONLY=1' not in entry.get('command',''):raise ValueError('Temperature must remain shadow-only in compiled production code')
 if r['version'] in ('0.2.27','0.2.28','0.2.29','0.2.30'):
  if r.get('holdover_rate_contract')!='CAUSAL_CHANNEL16_32_MEDIAN_DELTA5_V1':raise ValueError('Causal HOLD rate contract missing')
  for entry in compiled:
   if entry['file'].endswith('native_g1.cpp') and '-DPTOB_W12_HOLDOVER_RATE=1' not in entry.get('command',''):raise ValueError('HOLD rate compiler selection missing')
 if r['version'] in ('0.2.28','0.2.29','0.2.30'):
  if r.get('holdover_horizon_contract')!=('SAMPLED_PREFIX_Q99_1US_WITNESS_V2' if r['version']=='0.2.30' else 'SAMPLED_PREFIX_Q99_1US_V1'):raise ValueError('Sampled hold-duration contract missing')
  for entry in compiled:
   if entry['file'].endswith(('native_g1.cpp','native_main.cpp','display.cpp')) and '-DPTOB_W12_HOLDOVER_HORIZON=1' not in entry.get('command',''):raise ValueError('Duration UI/compiler selection missing')
 if r['version'] in ('0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):
  from build import validate_size_compilation
  validate_size_compilation(r,compiled,(target/'sdkconfig').read_text())
 if r['version'] in ('0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):validate_gm_successor_compilation(r,compiled)
 if r['version'] in ('0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):validate_fine_compilation(r,d,compiled)
 if r['version'] in ('0.1.22','0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):validate_map_compilation(r,d,compiled,(target/'sdkconfig').read_text())
 if r['version'] in ('0.1.3','0.1.4','0.1.5','0.1.6','0.1.7','0.1.8','0.1.9','0.1.10','0.1.11','0.1.12','0.1.13','0.1.14','0.1.15','0.1.16','0.1.17','0.1.18','0.1.19','0.1.20','0.1.21','0.1.22','0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):
  if (inputs/'ptob_device.h').read_text()!=device_header(d):raise ValueError('Device/GNSS header differs from explicit local profile')
  enabled=int('gnss_profile' in d)
  for suffix in ('/native_main.cpp','/gnss_pps.cpp'):
   command=next(row['command'] for row in compiled if row['file'].endswith(suffix))
   for flag in [f'-DW12_COPY_BOARD_PROFILE={12 if enabled else 2}',f'-DW12_GNSS_VERIFIED_INPUTS={enabled}',f'-DW12_GNSS_W12_POWER={enabled}','-DW12_GNSS_RX_PIN=39','-DW12_GNSS_PPS_PIN=41','-DW12_GNSS_BAUD=9600','-DW12_GNSS_R8_POWER=0',*(['-DPTOB_W12_LORA_RX=1'] if r.get('lora_rx') else ['-DV12_LORA_FORCED_OFF=1'])]:
    if flag not in command.split():raise ValueError('Compiled GNSS route differs from explicit local profile: '+flag)
 for row in compiled:
  file=Path(row['file']).resolve()
  if file.is_relative_to(Path(r['sdk_path'])):snapshot['sdk/'+str(file.relative_to(Path(r['sdk_path'])))]=sha(file)
  elif file.is_relative_to(Path(r['app_path'])):
   name=str(file.relative_to(Path(r['app_path'])));snapshot['app/'+name]=sha(file)
   if r['sources'].get(name)!=snapshot['app/'+name]:raise ValueError('Compiled application source mismatch')
  elif r.get('radiolib_path') and file.is_relative_to(Path(r['radiolib_path'])):
   name=str(file.relative_to(Path(r['radiolib_path'])));snapshot['radiolib/'+name]=sha(file)
   if snapshot['radiolib/'+name]!=r['radiolib_sources'].get(name):raise ValueError('Compiled RadioLib differs from retained snapshot')
  elif file.is_relative_to(out):snapshot['generated/'+str(file.relative_to(out))]=sha(file)
  else:raise ValueError('Unexpected source outside target/public SDK boundary')
 if snapshot!=r['compiled_sources']:raise ValueError('Compiled source closure changed')
 if r['version'] in ('0.1.7','0.1.8','0.1.9','0.1.10','0.1.11','0.1.12','0.1.13','0.1.14','0.1.15','0.1.16','0.1.17','0.1.18','0.1.19','0.1.20','0.1.21','0.1.22','0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30'):
  from stack_budget import check as check_main_stack
  if check_main_stack(out)!=r.get('main_stack_budget'):raise ValueError('Main task stack budget changed')
 return {'schema':1,'status':'IMMUTABLE_FILES_AND_RUNTIME_BINDING_VERIFIED','application_sha256':r['application_sha256'],'application_bytes':r['application_bytes'],'runtime_build_id':binding,'public_commit':r['public_commit'],'version':r['version'],'profile':'RC','estimated_relay':r['estimated_relay'],'actual_TX_capture':False,'precision':'UNQUALIFIED','hardware_access':False,**({'clock_backend':r['clock_backend']} if r['version'] in ('0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') else {}),'provisional_path_profile':provisional_profile(d),**({'main_stack_budget':r['main_stack_budget']} if r['version'] in ('0.1.7','0.1.8','0.1.9','0.1.10','0.1.11','0.1.12','0.1.13','0.1.14','0.1.15','0.1.16','0.1.17','0.1.18','0.1.19','0.1.20','0.1.21','0.1.22','0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30') else {})}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('out',type=Path);a=p.parse_args();print(json.dumps(inspect(a.out),indent=2))
if __name__=='__main__':main()
