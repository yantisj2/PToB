#!/usr/bin/env python3
"""Build W12 RC with public ESP-IDF and ignored local inputs; never opens USB.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,shlex,hashlib,json,os,re,shutil,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1]
sys.path.insert(0,str(REPO/'registry'))
from registry import load as load_registry,header as registry_header
VERSION='0.2.24'
GNSS_PROFILE='meshnology_w12_l76k_rx39_pps41'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def git(p,*args):return subprocess.check_output(['git','-C',str(p),*args],text=True).strip()
def dump(p,v):p.write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
def source_files():return sorted(p for p in APP.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc')
def validate_gm_successor_compilation(receipt,compiled):
 if receipt.get('gm_successor_policy')!='GM7_V3_AUTHENTICATED_NOMINEE_RECOVERY_3S_V1':raise ValueError('GM successor policy missing')
 for suffix in ('/native_g1.cpp','/gm7/gm7_codec.c'):
  rows=[r for r in compiled if r['file'].endswith(suffix)]
  if len(rows)!=1 or '-DPTOB_GM_SUCCESSOR=1' not in rows[0]['command'].split():raise ValueError('GM successor consumer/codec not compiled: '+suffix)
  if suffix.endswith('gm7_codec.c') and '-Os' not in rows[0]['command'].split():raise ValueError('Size-bounded GM control codec required')
  if receipt.get('time_zone_contract') and '-DR2_TZ_WIRE=1' not in rows[0]['command'].split():raise ValueError('Current coarse TZ wire not compiled: '+suffix)
def validate_inputs(enrollment,device):
 if set(enrollment)!={'group_id','key_epoch','psk'}:raise ValueError('Expected shared group enrollment schema')
 group=enrollment['group_id'];epoch=enrollment['key_epoch'];key=bytes.fromhex(enrollment['psk'])
 if not(re.fullmatch('[0-9a-f]{16}',group) and int(group,16) and type(epoch)is int and 0<epoch<2**32 and len(key)==32 and any(key)):raise ValueError('Invalid group enrollment')
 required={'board','factory_mac','device_id','label','legacy_wire_id'}
 if not required<=set(device) or set(device)-required-{'gnss_profile','provisional_path'}:raise ValueError('Unexpected device fields')
 provisional_profile(device)
 if 'gnss_profile' in device and device['gnss_profile']!=GNSS_PROFILE:raise ValueError('Unsupported GNSS route; explicit reviewed W12 L76K RX39/PPS41 profile required')
 mac=device['factory_mac'];identity=device['device_id'];label=device['label'];wire=device['legacy_wire_id']
 if not(device['board']=='meshnology_w12_esp32s3' and re.fullmatch('[0-9a-f]{2}(:[0-9a-f]{2}){5}',mac) and int(mac.replace(':',''),16) and not int(mac[:2],16)&1 and identity=='0000'+mac.replace(':','') and re.fullmatch('[A-Za-z0-9_-]{1,24}',label) and type(wire)is int and 1<=wire<=254):raise ValueError('Explicit W12 factory binding/label required')
 return key

def gnss_map_diagnostic(device,enabled):
 if type(enabled)is not bool:raise ValueError('Explicit boolean fast-map diagnostic selection required')
 if enabled and (device.get('board')!='meshnology_w12_esp32s3' or device.get('gnss_profile')!=GNSS_PROFILE):raise ValueError('Fast-map diagnostic requires the explicit verified W12 L76K profile')
 return enabled

def validate_map_compilation(receipt,device,compiled,config):
 enabled=gnss_map_diagnostic(device,receipt.get('gnss_fast_map_diagnostic'))
 for suffix in ('/native_main.cpp','/gnss_pps.cpp','/s3_clock_reference.c'):
  matches=[row['command'].split() for row in compiled if row['file'].endswith(suffix)]
  if len(matches)!=1:raise ValueError('One exact compiled fast-map source required: '+suffix)
  tokens=matches[0];prefix='-DPTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC='
  if [x for x in tokens if x.startswith(prefix)]!=[prefix+str(int(enabled))]:raise ValueError('Compiled fast-map treatment differs from explicit build option')
  if enabled:
   for flag in ('-DV12_R2_RX_SHADOW=1','-DW12_COPY_BOARD_PROFILE=12','-DW12_GNSS_VERIFIED_INPUTS=1','-DW12_GNSS_W12_POWER=1','-DW12_GNSS_RX_PIN=39','-DW12_GNSS_PPS_PIN=41','-DW12_GNSS_BAUD=9600'):
    prefix=flag.split('=',1)[0]+'='
    if [x for x in tokens if x.startswith(prefix)]!=[flag]:raise ValueError('Fast-map compiled profile/ABI flag missing or ambiguous: '+flag)
 if enabled:
  for flag in ('CONFIG_BT_CTRL_PINNED_TO_CORE=0','CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0=y','CONFIG_ESP_MAIN_TASK_AFFINITY=0x0'):
   if config.splitlines().count(flag)!=1:raise ValueError('Fast-map diagnostic requires the exact validated core0 task/controller configuration')
 return enabled

def validate_size_compilation(receipt,compiled,config):
 if receipt.get('application_optimization')!='W12_MAIN_OS_SDK_DEBUG_V1':raise ValueError('W12 application size policy missing')
 rows=[shlex.split(row['command']) for row in compiled if '__idf_main.dir/' in row['command']]
 if not rows or not any(row['file'].endswith('/native_main.cpp') and '__idf_main.dir/' in row['command'] for row in compiled):raise ValueError('W12 main component compile commands missing')
 for tokens in rows:
  optimization=[v for v in tokens if re.fullmatch(r'-O(?:[0-3sgz]|fast)',v)]
  if not optimization or optimization[-1]!='-Os':raise ValueError('W12 main component must compile with effective -Os')
 for flag in ('CONFIG_COMPILER_OPTIMIZATION_DEBUG=y','CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE=y','CONFIG_COMPILER_OPTIMIZATION_ASSERTION_LEVEL=2'):
  if flag not in config.splitlines():raise ValueError('SDK optimization/assertion policy changed: '+flag)

def phase_offsets(version=VERSION):
 return {'schema':1,'receive_ns':7671,'relay_residual_ns':3030 if version in ('0.2.8','0.2.9') else 4026,'scope':'W12_RELATIVE_PPS_PROVISIONAL','training':'W12_FINE_BLE100_AND_T096_CHILD_WITH_EXISTING_RECEIVER_CORRECTION','other_units_profiles':'OPERATOR_REQUESTED_UNMEASURED_TRANSFER','RF_bound':'UNKNOWN','UTC_bound':'UNKNOWN'}

def provisional_profile(device):
 p=device.get('provisional_path')
 if p is None:
  if 'provisional_path' in device:raise ValueError('Explicit profile must be an object')
  return None
 required={'schema','scope','offset_ns','root','root_boot','source_epoch','receiver','receiver_source_sha256','receiver_phase_correction_ns','evidence_sha256','baseline_evidence_sha256'}
 if type(p)is not dict or not required<=set(p) or set(p)-required-{'allow_source_epoch_transfer'}:raise ValueError('Unexpected provisional aggregate-path fields')
 if 'allow_source_epoch_transfer' in p and type(p['allow_source_epoch_transfer'])is not bool:raise ValueError('Epoch transfer requires explicit boolean opt-in')
 if type(p['schema'])is not int or p['schema']!=1 or p['scope']!='AGGREGATE_PATH_RELATIVE_PPS':raise ValueError('Wrong provisional profile schema/scope')
 if type(p['offset_ns'])is not int or not -100000<=p['offset_ns']<=100000 or not p['offset_ns']:raise ValueError('Provisional offset outside domain')
 if type(p['source_epoch'])is not int or not 0<p['source_epoch']<2**32:raise ValueError('Exact source epoch required')
 if type(p['receiver_phase_correction_ns'])is not int or not -100000<=p['receiver_phase_correction_ns']<=100000:raise ValueError('Receiver correction metadata required')
 for key in ('root','root_boot','receiver'):
  if not isinstance(p[key],str) or not re.fullmatch('[0-9a-f]{16}',p[key]) or not int(p[key],16):raise ValueError('Exact nonzero identity/boot required: '+key)
 for key in ('receiver_source_sha256','evidence_sha256','baseline_evidence_sha256'):
  if not isinstance(p[key],str) or not re.fullmatch('[0-9a-f]{64}',p[key]) or not int(p[key],16):raise ValueError('Exact evidence/source identity required: '+key)
 identity={**p,'publisher':device['device_id']}
 digest=hashlib.sha256(json.dumps(identity,sort_keys=True,separators=(',',':')).encode()).hexdigest()
 if not int(digest[:16],16):raise ValueError('Zero profile identity')
 return {**identity,'id':digest[:16],'provenance_sha256':digest,'qualification':'PROVISIONAL_AGGREGATE_PATH_UNKNOWN_RF_UTC','residual_jitter':'MEASURE_SEPARATELY_NOT_A_BOUND','source_epoch_transfer':'UNMEASURED_PROVISIONAL' if p.get('allow_source_epoch_transfer',False) else 'EXACT_TRAINING_EPOCH'}

def device_header(device):
 p=provisional_profile(device)
 provisional=''
 if p:
  for key,macro in [('id','ID'),('publisher','PUBLISHER'),('root','ROOT'),('root_boot','ROOT_BOOT')]:provisional+='#define PTOB_PROVISIONAL_'+macro+' UINT64_C(0x'+p[key]+')\n'
  provisional+='#define PTOB_PROVISIONAL_SOURCE_EPOCH '+str(p['source_epoch'])+'\n#define PTOB_PROVISIONAL_OFFSET_NS '+str(p['offset_ns'])+'\n'
  provisional+='#define PTOB_PROVISIONAL_ALLOW_SOURCE_EPOCH_TRANSFER '+str(int(p.get('allow_source_epoch_transfer',False)))+'\n'
  provisional+='#define PTOB_PROVISIONAL_PROVENANCE_SHA256 "'+p['provenance_sha256']+'"\n'
 return provisional+('#pragma once\n#define PTOB_FACTORY_MAC {'+','.join('0x'+x for x in device['factory_mac'].split(':'))+'}\n#define PTOB_DEVICE_LABEL '+json.dumps(device['label'])+'\n#define PTOB_LEGACY_WIRE_ID '+str(device['legacy_wire_id'])+'\n#define PTOB_GNSS_INDICATORS '+str(int('gnss_profile' in device))+'\n')

def fine_header(device,enabled):
 return '#pragma once\n#include <stdint.h>\n#define PTOB_W12_FINE_FACTORY UINT64_C(0x'+device['device_id']+')\n#define PTOB_W12_FINE_RX_GPIO 16\n#define PTOB_W12_FINE_SELECTED '+str(int(enabled))+'\n'

def fine_backend(enabled,legacy=False):
 return {'mode':'MCPWM1_RW_RX_GPIO16_EXPERIMENTAL' if enabled else 'LEGACY_GPTIMER20',
         'counter_hz':80000000 if enabled else 20000000,'coordinate_hz':20000000,
         'fine_slew_threshold_ns':2000 if enabled else 1000,'fine_slew_boundary':'STRICTLY_BELOW' if enabled else 'INCLUSIVE',
         'relay_phase_convention_ns':1568000 if enabled else None,'direct_GR3':'DESCRIPTOR_ONLY' if legacy else 'PDU191_FINE_LEGACY_PDU159_DESCRIPTOR',
         'RF_bound':'UNKNOWN','UTC_bound':'UNKNOWN','hardware_validation':'NOT_PERFORMED'}

def validate_fine_compilation(receipt,device,compiled):
 enabled=receipt.get('fine_clock')
 if type(enabled)is not bool or receipt.get('clock_backend')!=fine_backend(enabled,receipt.get('version')=='0.1.23'):raise ValueError('Exact fine-clock backend metadata required')
 if enabled and (receipt['listening_percent']!=100 or receipt.get('gnss_fast_map_diagnostic')):raise ValueError('Fine clock requires BLE100 and ordinary GNSS mapping')
 if enabled and provisional_profile(device):raise ValueError('Descriptor aggregate offsets cannot transfer to the fine backend')
 for suffix in ('/native_main.cpp','/gnss_pps.cpp','/native_g1.cpp','/periodic_rx.cpp'):
  rows=[row['command'].split() for row in compiled if row['file'].endswith(suffix)]
  if len(rows)!=1:raise ValueError('One exact fine-clock source required: '+suffix)
  if [x for x in rows[0] if x.startswith('-DPTOB_W12_FINE_CLOCK')]!=(['-DPTOB_W12_FINE_CLOCK=1'] if enabled else []):raise ValueError('Compiled fine-clock domain differs from selection')
 owners=[row for row in compiled if row['file'].endswith('/w12_fine_clock.cpp')]
 if len(owners)!=int(enabled):raise ValueError('Fine-clock peripheral owner differs from selection')
 return enabled

def verify_sdk(sdk,lock):
 pin=lock['esp_idf']
 if git(sdk,'rev-parse','HEAD')!=pin['commit'] or git(sdk,'status','--porcelain','--untracked-files=all'):raise ValueError('Pinned clean public ESP-IDF required')
 status=git(sdk,'submodule','status','--recursive')
 if any(line[:1] in '-+U' and not line.endswith('components/openthread/openthread/third_party/mbedtls/repo') for line in status.splitlines()):raise ValueError('SDK submodule missing or wrong revision')
 subprocess.run(['git','-C',str(sdk),'submodule','foreach','--recursive','--quiet','test -z "$(git status --porcelain --untracked-files=all)"'],check=True,stdout=subprocess.DEVNULL)
 for name,hashkey in [('components/bt/controller/lib_esp32c3_family/esp32s3/libbtdm_app.a','controller_sha256'),('components/esp_driver_gptimer/src/gptimer_priv.h','gptimer_private_header_sha256')]:
  if sha(sdk/name)!=pin[hashkey]:raise ValueError('SDK controller/private ABI mismatch')
 for name,digest in lock.get('fine_capture_abi_sha256',{}).items():
  if sha(sdk/name)!=digest:raise ValueError('Pinned fine-capture ABI changed: '+name)
 return status

def lora_header(device,enrollment,transmitter):
 key=bytes.fromhex(enrollment['psk'])
 return '#pragma once\n#include <cstdint>\n#define PTOB_W12_FACTORY_ID UINT64_C(0x'+device['device_id']+')\n#define PTOB_W12_LORA_TX_FACTORY UINT64_C(0x'+transmitter+')\n#define PTOB_W12_GROUP_ID UINT64_C(0x'+enrollment['group_id']+')\n#define PTOB_W12_KEY_EPOCH '+str(enrollment['key_epoch'])+'\nstatic constexpr uint8_t PTOB_W12_GROUP_KEY[32]={'+','.join(map(str,key))+'};\n'
def verify_radio(path,lock):
 if git(path,'rev-parse','HEAD')!=lock['radiolib']['commit'] or git(path,'status','--porcelain','--untracked-files=all'):raise ValueError('Pinned clean public RadioLib required')
 if sha(path/'license.txt')!=lock['radiolib']['license_sha256']:raise ValueError('RadioLib license changed')
def validate_dual_compilation(r,compiled):
 tokens=next(x['command'].split() for x in compiled if x['file'].endswith('/native_main.cpp'))
 for flag in ('-DPTOB_W12_SYNC_WATCHDOG=1','-DPTOB_W12_DUAL_UI=1','-DPTOB_W12_LORA_RX=1' if r['lora_rx'] else '-DV12_LORA_FORCED_OFF=1'):
  if flag not in tokens:raise ValueError('Missing compiled runtime feature: '+flag)
 if r.get('rc9'):
  if '-DPTOB_RC9=1' not in tokens or not r['lora_rx'] or not r['estimated_relay']:raise ValueError('RC9 compile/profile mismatch')
 if r.get('no_time_sync_watchdog_us')!=300000000:raise ValueError('Watchdog metadata differs')
 if r['lora_rx'] and not r['fine_clock']:raise ValueError('LoRa requires fine counter')

def main():
 match=re.search(r'^set\(PROJECT_VER "([^\"]+)"\)$',(APP/'CMakeLists.txt').read_text(),re.MULTILINE)
 if not match or match.group(1)!=VERSION:raise ValueError('Python/CMake firmware versions must agree before building')
 p=argparse.ArgumentParser(description=__doc__)
 for name in ['idf','out','enrollment','device-config']:p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--profile',choices=['RC'],required=True);p.add_argument('--listening-percent',type=int,choices=[50,100],default=100)
 p.add_argument('--rc9',action='store_true');p.add_argument('--fine-clock',action='store_true',help='Experimental fixed W12 GPIO16 80MHz fine receiver, BLE100 only; physical validation separate');p.add_argument('--gnss-fast-map-diagnostic',action='store_true');p.add_argument('--estimated-relay',action='store_true');p.add_argument('--registry',type=Path,default=REPO/'registry/example.json');p.add_argument('--radiolib',type=Path);p.add_argument('--lora-transmitter-factory',default='0000000000000000');p.add_argument('--development',action='store_true');a=p.parse_args()
 os.umask(0o077);sdk=a.idf.resolve();out=a.out.resolve()
 if out.exists() or REPO/'artifacts' not in out.parents:p.error('Fresh output directory below ignored artifacts/ required')
 if a.estimated_relay and a.listening_percent!=100:p.error('Estimated relay requires BLE100')
 if a.fine_clock and (a.listening_percent!=100 or a.gnss_fast_map_diagnostic):p.error('Fine clock requires BLE100 and ordinary GNSS mapping')
 for path in [a.enrollment,a.device_config]:
  if subprocess.run(['git','-C',str(REPO),'check-ignore','--quiet',str(path.resolve())]).returncode:p.error('Enrollment and device inputs must be Git-ignored')
 repository_dirty=bool(git(REPO,'status','--porcelain','--untracked-files=all'))
 # Only the complete W12 source closure and registry generator affect this
 # image. Preserve/report unrelated work without mislabeling committed sources.
 dirty=bool(git(REPO,'status','--porcelain','--untracked-files=all','--','firmware/w12','registry/registry.py'))
 if dirty and not a.development:p.error('Commit reviewed public inputs before a candidate build')
 lock=json.loads((APP/'dependencies.lock.json').read_text());modules=verify_sdk(sdk,lock)
 e=json.loads(a.enrollment.read_text());d=json.loads(a.device_config.read_text());key=validate_inputs(e,d);fast_map=gnss_map_diagnostic(d,a.gnss_fast_map_diagnostic);registry=load_registry(a.registry)
 if a.fine_clock and provisional_profile(d):p.error('Fine clock requires an uncalibrated device input; descriptor offsets cannot transfer')
 if provisional_profile(d) and not(a.estimated_relay and a.listening_percent==100):p.error('Provisional aggregate-path profile requires estimated relay BLE100')
 tx=a.lora_transmitter_factory
 if not re.fullmatch('[0-9a-f]{16}',tx) or (int(tx,16) and (registry['devices'].get(tx,{}).get('target')!='t096' or tx==d['device_id'])):p.error('Exact registry T096 transmitter factory required')
 if bool(a.radiolib)!=bool(int(tx,16)) or (a.radiolib and not a.fine_clock):p.error('LoRa RX requires fine clock, public RadioLib and explicit transmitter binding')
 if a.rc9 and not(a.radiolib and a.estimated_relay):p.error('RC9 requires LoRa RX and BLE100 relay owner')
 if a.radiolib:verify_radio(a.radiolib.resolve(),lock)
 out.mkdir(parents=True);target=out/'target';target.mkdir();inputs=out/'local';inputs.mkdir()
 sources={str(f.relative_to(APP)):sha(f) for f in source_files()};source_hash=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
 for n in sources:
  q=out/'source'/n;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(APP/n,q)
 for path,name in [(a.enrollment,'enrollment.json'),(a.device_config,'device.json'),(a.registry,'registry.json'),(REPO/'registry/registry.py','registry_generator.py')]:shutil.copyfile(path,inputs/name)
 (inputs/'ptob_registry.h').write_text(registry_header(registry))
 (inputs/'ptob_device.h').write_text(device_header(d))
 (inputs/'w12_lora_config.h').write_text(lora_header(d,e,tx))
 (inputs/'w12_fine_config.h').write_text(fine_header(d,a.fine_clock))
 # Registry/generator/device hashes bind display metadata and local input changes into runtime enrollment identity.
 enrollment_header=inputs/'r2-enrollment.h';enrollment_header.write_text('/* source:'+source_hash+' registry:'+sha(a.registry)+' generator:'+sha(REPO/'registry/registry.py')+' device:'+sha(a.device_config)+' */\nstatic const r2_group_enrollment provision={{{'+','.join(map(str,key))+'},UINT64_C(0x'+e['group_id']+'),'+str(e['key_epoch'])+'},UINT64_C(0x'+d['device_id']+'),'+json.dumps(d['label'])+',40000};\n')
 r={'schema':1,'artifact_class':'PTOB_W12_RC','version':VERSION,'status':'INCOMPLETE','public_commit':git(REPO,'rev-parse','HEAD'),'source_dirty':dirty,'repository_dirty':repository_dirty,'source_sha256':source_hash,'sources':sources,'dependencies':lock,'sdk_path':str(sdk),'app_path':str(APP),'sdk_submodules':modules.splitlines(),'time_zone_contract':'COARSE_TZ_V1_ORDERED_DYNAMIC_LOCAL_DISPLAY','button_power_contract':'OFF_MAIN_HOLD4_DRAIN2_RELEASE40_GPIO0_WAKE_V1','shadow_holdover_contract':'BLE_THERMAL_SHADOW_120S_RATE_ANCHOR_V3','application_optimization':'W12_MAIN_OS_SDK_DEBUG_V1','profile':'RC','listening_percent':a.listening_percent,'estimated_relay':a.estimated_relay,'tx_actual_capture':False,'precision':'UNQUALIFIED','installed':False,'fine_clock':a.fine_clock,'no_time_sync_watchdog_us':300000000,'lora_rx':bool(a.radiolib),'rc9':a.rc9,'pa_termination_policy':'SDK_HCI_ACK_EXACT_OWNER_V1','leader_recovery_policy':'FENCED_SCOPE_CACHE_CLEAR_1500MS_V3','gm_successor_policy':'GM7_V3_AUTHENTICATED_NOMINEE_RECOVERY_3S_V1','rc9_server_recovery_policy':'QUALIFIED_BLE_DISPLAY_DRAIN_ORIGINAL_CHILD_LEASE_V1' if a.rc9 else None,'fallback_recovery_policy':'AUTO_RC9_PASSIVE_NORMAL_RETRY_V1' if a.rc9 else None,'lora_transmitter_factory':tx,'radiolib_path':str(a.radiolib.resolve()) if a.radiolib else None,'clock_backend':fine_backend(a.fine_clock),'gnss_fast_map_diagnostic':fast_map,'registry_sha256':sha(a.registry),'registry_generator_sha256':sha(REPO/'registry/registry.py'),'enrollment_mode':'LOCAL_BUILD_TIME_NO_NVS_WRITE','device_config_sha256':sha(a.device_config),'provisional_path_profile':provisional_profile(d),'phase_offsets':phase_offsets(),'bluetooth_clock_map':'FINE_RAW_CURRENT_BRACKET' if a.fine_clock else 'GPTIMER_DRIVER_BRACKET','programmed_clock_projection':'MEAN4_VALIDATED_NO_TRIMMING' if a.estimated_relay else None}
 dump(out/'receipt.json',r)
 env=os.environ.copy()
 for k in ['CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','EXTRA_COMPONENT_DIRS','SDKCONFIG_DEFAULTS']:env.pop(k,None)
 # This target has no managed components; use only the pinned SDK and local source.
 env.update(IDF_COMPONENT_MANAGER='0',IDF_PATH=str(sdk),PYTHONPYCACHEPREFIX=str(out/'pycache'),IDF_CCACHE_ENABLE='0')
 command=[sys.executable,str(sdk/'tools/idf.py'),'-C',str(APP),'-B',str(target),'-G','Unix Makefiles','-D','SDKCONFIG='+str(target/'sdkconfig'),'-D','V12_SOURCE_REVISION='+r['public_commit'],'-D','V12_R2_ENROLLMENT_HEADER='+str(enrollment_header),'-D','PTOB_DEVICE_HEADER='+str(inputs/'ptob_device.h'),'-D','PTOB_REGISTRY_HEADER='+str(inputs/'ptob_registry.h'),'-D','PTOB_FINE_HEADER='+str(inputs/'w12_fine_config.h'),'-D','PTOB_W12_FINE_CLOCK='+('ON' if a.fine_clock else 'OFF'),'-D','PTOB_W12_GNSS_INDICATORS='+('ON' if 'gnss_profile' in d else 'OFF'),'-D','PTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC='+('ON' if fast_map else 'OFF'),'-D','V12_R2_G1_LISTENING_PERCENT='+str(a.listening_percent),'-D','V12_R2_RC7_TX_ESTIMATE_LAB='+('ON' if a.estimated_relay else 'OFF'),'-D','CCACHE_ENABLE=OFF']
 command+=['-D','PTOB_LORA_HEADER='+str(inputs/'w12_lora_config.h'),'-D','PTOB_W12_LORA_RX='+('ON' if a.radiolib else 'OFF'),'-D','PTOB_RADIOLIB='+str(a.radiolib.resolve() if a.radiolib else '')]
 command+=['-D','PTOB_RC9='+('ON' if a.rc9 else 'OFF'),'build']
 dump(out/'command.json',command)
 try:
  with (out/'build.log').open('w') as log:subprocess.run(command,env=env,cwd=REPO,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=900)
  verify_sdk(sdk,lock)
  if sources!={str(f.relative_to(APP)):sha(f) for f in source_files()}:raise ValueError('Source changed during build')
  image=target/'ptob_w12.bin'
  if not 0<image.stat().st_size<=lock['application_max_bytes']:raise ValueError('Application exceeds preserved slot ceiling')
  config=(target/'sdkconfig').read_text()
  for k in ['CONFIG_BT_NIMBLE_NVS_PERSIST=y','CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=y']:
   if k in config:raise ValueError('Forbidden storage/radio configuration')
  validate_fine_compilation(r,d,json.loads((target/'compile_commands.json').read_text()))
  validate_gm_successor_compilation(r,json.loads((target/'compile_commands.json').read_text()))
  validate_map_compilation(r,d,json.loads((target/'compile_commands.json').read_text()),config)
  validate_size_compilation(r,json.loads((target/'compile_commands.json').read_text()),config)
  compiler=next(x for x in json.loads((target/'compile_commands.json').read_text()) if x['file'].endswith('/native_main.cpp'))
  toolchain_version=subprocess.check_output([compiler['command'].split()[0],'-dumpfullversion'],text=True).strip()
  if toolchain_version!='14.2.0':raise ValueError('Expected Xtensa GCC14.2.0')
  compiler_path=Path(compiler['command'].split()[0])
  compiler_banner=subprocess.check_output([str(compiler_path),'--version'],text=True).splitlines()[0]
  if 'esp-14.2.0_20260121' not in compiler_banner:raise ValueError('Compiler release mismatch')
  r['compiler_version']=compiler_banner
  nm=str(compiler_path.with_name(re.sub(r'(g\+\+|gcc)$','nm',compiler_path.name)))
  symbols=subprocess.check_output([nm,str(target/'ptob_w12.elf')],text=True)
  if re.search(r'\b(esp_wifi_init|esp_wifi_start|nvs_flash_init|nvs_set_blob|nvs_set_u32)\b',symbols):raise ValueError('Unexpected WiFi activation or NVS write dependency')
  validate_dual_compilation(r,json.loads((target/'compile_commands.json').read_text()))
  validate_gm_successor_compilation(r,json.loads((target/'compile_commands.json').read_text()))
  identity=json.loads((target/'v12_build_identity.json').read_text());r.update(runtime_identity=identity,application='target/ptob_w12.bin',application_sha256=sha(image),application_bytes=image.stat().st_size,artifacts={str(q.relative_to(out)):sha(q) for q in out.rglob('*') if q.is_file() and (q.suffix in ['.bin','.elf','.map','.su'] or q.name in ['sdkconfig','compile_commands.json','v12_build_identity.json','r2-enrollment.h','ptob_device.h','ptob_registry.h','enrollment.json','device.json','registry.json','registry_generator.py','w12_fine_config.h','w12_lora_config.h'])},status='DEVELOPMENT_NOT_FLASHABLE' if dirty else 'BUILD_COMPLETE_PHYSICAL_UNKNOWN')
  from stack_budget import check as check_main_stack
  r['main_stack_budget']=check_main_stack(out)
  r['compiled_sources']={}
  for row in json.loads((target/'compile_commands.json').read_text()):
   q=Path(row['file']).resolve()
   for prefix,base in [('sdk',sdk),('app',APP),('generated',out),*([('radiolib',a.radiolib.resolve())] if a.radiolib else [])]:
    if q.is_relative_to(base):r['compiled_sources'][prefix+'/'+str(q.relative_to(base))]=sha(q);break
   else:raise ValueError('Unexpected compiled source')
  if a.radiolib:
   verify_radio(a.radiolib.resolve(),lock)
   for q in a.radiolib.resolve().rglob('*'):
    if q.is_file() and '.git' not in q.parts:
     dest=out/'dependencies/RadioLib'/q.relative_to(a.radiolib.resolve());dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(q,dest)
   r['radiolib_sources']={str(q.relative_to(out/'dependencies/RadioLib')):sha(q) for q in (out/'dependencies/RadioLib').rglob('*') if q.is_file()}
  dump(out/'receipt.json',r)
  from inspect_build import inspect
  dump(out/'inspection.json',inspect(out,allow_development=a.development))
  print('Build and standalone identity/layout checks passed:',out)
 except BaseException:r['status']='FAILED';dump(out/'receipt.json',r);raise
if __name__=='__main__':
    import sys
    sys.path.insert(0, str(REPO / 'tools'))
    from firmware_artifacts import run_builder
    run_builder(main, REPO)
