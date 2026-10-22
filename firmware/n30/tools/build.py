#!/usr/bin/env python3
"""Build offline N30 RC with public ESP-IDF and ignored local inputs; never opens USB.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,importlib.util,json,os,re,shutil,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1]
sys.path.insert(0,str(REPO/'registry'))
from registry import load as load_registry,header as registry_header
VERSION='0.2.14'
T096_LORA_FILES=['protocol/lora_rc8.c','protocol/lora_rc8.h','protocol/group_auth.h','include/lora_rc8_policy.h','include/precision_servo.h','src/lora_rc8_policy.c']
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def git(p,*args):return subprocess.check_output(['git','-C',str(p),*args],text=True).strip()
def dump(p,v):p.write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
def source_files():
 roots=[APP,REPO/'firmware/w12/main',REPO/'firmware/w12/legacy',REPO/'firmware/w12/tests/console_fakes']
 files={p for base in roots for p in base.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc'}
 files|={REPO/'firmware/t096'/p for p in T096_LORA_FILES}
 files|={REPO/'registry/registry.py',REPO/'firmware/w12/tools/build.py',REPO/'firmware/w12/tests/rc7_provisional_path_test.cpp',REPO/'firmware/w12/tools/stack_budget.py',REPO/'firmware/w12/tools/framed_console.py',REPO/'firmware/w12/tools/status_budget_test.py'}
 return sorted(files)
def shared_path_builder():
 name='_n30_w12_path_builder'
 if name not in sys.modules:
  spec=importlib.util.spec_from_file_location(name,REPO/'firmware/w12/tools/build.py')
  module=importlib.util.module_from_spec(spec);sys.modules[name]=module;spec.loader.exec_module(module)
 return sys.modules[name]
def provisional_profile(device):return shared_path_builder().provisional_profile(device)
def validate_profile_mode(device,relay,listening):
 if provisional_profile(device) and not(relay and listening==100):raise ValueError('Provisional aggregate-path profile requires estimated relay BLE100')
def validate_inputs(enrollment,device):
 if set(enrollment)!={'group_id','key_epoch','psk'}:raise ValueError('Expected shared group enrollment schema')
 group=enrollment['group_id'];epoch=enrollment['key_epoch'];key=bytes.fromhex(enrollment['psk'])
 if not(re.fullmatch('[0-9a-f]{16}',group) and int(group,16) and type(epoch)is int and 0<epoch<2**32 and len(key)==32 and any(key)):raise ValueError('Invalid group enrollment')
 required={'board','factory_mac','device_id','label','legacy_wire_id','display_rotation'}
 if not required<=set(device) or set(device)-required-{'provisional_path'}:raise ValueError('Exact N30 input fields required; GPS and pin overrides are unsupported')
 mac=device['factory_mac'];identity=device['device_id'];label=device['label'];wire=device['legacy_wire_id']
 if not(device['board']=='meshnology_n30_heltec_v3' and re.fullmatch('[0-9a-f]{2}(:[0-9a-f]{2}){5}',mac) and int(mac.replace(':',''),16) and not int(mac[:2],16)&1 and identity=='0000'+mac.replace(':','') and label in ('N3-1','N3-2') and type(wire)is int and 1<=wire<=254 and device['display_rotation'] in ('cw','ccw')):raise ValueError('Explicit N30 identity, reserved label and rotation required')
 provisional_profile(device)
 return key

def validate_transmitter(factory,device,registry):
 if not re.fullmatch('[0-9a-f]{16}',factory) or (int(factory,16) and (registry['devices'].get(factory,{}).get('target')!='t096' or factory==device['device_id'])):raise ValueError('Exact existing T096 factory identity required for passive LoRa')

def device_header(device):
 # Reuse the reviewed schema1 TOTAL-from-programmed-endpoint semantics. This
 # preserves the nominal ADDRESS conversion exactly once for every child.
 return shared_path_builder().device_header(device)+'#define PTOB_N30_ROTATE_CW '+str(int(device['display_rotation']=='cw'))+'\n'
def lora_config_header(device,enrollment,transmitter):
 key=bytes.fromhex(enrollment['psk'])
 return '#pragma once\n#include <cstdint>\n#define PTOB_N30_FACTORY_ID UINT64_C(0x'+device['device_id']+')\n#define PTOB_N30_LORA_TX_FACTORY UINT64_C(0x'+transmitter+')\n#define PTOB_N30_GROUP_ID UINT64_C(0x'+enrollment['group_id']+')\n#define PTOB_N30_KEY_EPOCH '+str(enrollment['key_epoch'])+'\nstatic constexpr uint8_t PTOB_N30_GROUP_KEY[32]={'+','.join(map(str,key))+'};\n'
def fine_profile(device,path):
 # No pad is selected by default. Fixed experiment pair only, never arbitrary GPIO.
 profile={'schema':1,'mode':'CURRENT_ONLY','factory':device['device_id'],'RX_GPIO':-1,'TX_GPIO':-1}
 if path:
  value=json.loads(Path(path).read_text())
  if set(value)!={'schema','mode','factory','RX_GPIO','TX_GPIO','board_family','physical_review_sha256','schematic_review_sha256'}:raise ValueError('Exact raw-marker profile fields required')
  if not(value['schema']==1 and value['mode'] in ('NATIVE_GPIO4_5_SHADOW','NATIVE_RW_GPIO4_5_SHADOW') and value['factory']==device['device_id'] and value['RX_GPIO']==4 and value['TX_GPIO']==5 and value['board_family']=='HELTEC_V3_V31_V32' and all(re.fullmatch('[0-9a-f]{64}',value[k]) for k in ('physical_review_sha256','schematic_review_sha256'))):raise ValueError('Only exact reviewed HeltecV3 GPIO4/5 raw-shadow profile is supported')
  profile=value
 return profile

def fine_header(profile):
 header='#pragma once\n#include <cstdint>\n#define PTOB_N30_FINE_FACTORY UINT64_C(0x'+profile['factory']+')\n#define PTOB_N30_FINE_PINS '+str(int(profile['mode'] in ('NATIVE_GPIO4_5_SHADOW','NATIVE_RW_GPIO4_5_SHADOW')))+'\n#define PTOB_N30_FINE_RX_GPIO '+str(profile['RX_GPIO'])+'\n#define PTOB_N30_FINE_TX_GPIO '+str(profile['TX_GPIO'])+'\n/* profile_sha256='+hashlib.sha256(json.dumps(profile,sort_keys=True).encode()).hexdigest()+' */\n'
 return header+('#define PTOB_N30_FINE_RW 1\n' if profile['mode']=='NATIVE_RW_GPIO4_5_SHADOW' else '')

def check_main_stack(out):
 import importlib.util
 path=REPO/'firmware/w12/tools/stack_budget.py'
 spec=importlib.util.spec_from_file_location('n30_selected_stack',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 result=module.check(out)
 if json.loads((Path(out)/'receipt.json').read_text()).get('version') not in ('0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14'):return result
 # Selected extra chains with explicit reserve; not a complete call graph.
 usage=[]
 for file in (Path(out)/'target').rglob('*.su'):
  usage.extend(line.split('\t') for line in file.read_text().splitlines() if line.strip())
 def frame(tail):
  rows=[r for r in usage if len(r)==3 and r[0].endswith(':'+tail)]
  if len(rows)!=1 or rows[0][2]!='static':raise ValueError('Missing/ambiguous N30 static frame: '+tail)
  return int(rows[0][1])
 f=result['frames_bytes'];root=f['main']+f['input']+f['command']
 paths={
 'LoRa_status':(result['configured_bytes'],4096,root+frame('void {anonymous}::respond_status(uint64_t, uint64_t)')+frame('void {anonymous}::lora_status_frame(uint64_t, uint64_t)')+frame('void n30_lora_diagnostics(v12::ConsoleFrame&)')),
 'main_UI_publish':(result['configured_bytes'],4096,f['main']+f['ui']+frame('void n30_display_publish(const v12::R2Status&, const DisplayState&, uint64_t)')+frame('void n30::select_lora(Model&, const n30_lora_view&, uint64_t)'))}
 def in_file(name,tail):
  files=list((Path(out)/'target').rglob(name+'.su'))
  if len(files)!=1:raise ValueError('Missing N30 stack unit: '+name)
  rows=[r.split('\t') for r in files[0].read_text().splitlines() if r.split('\t')[0].endswith(':'+tail)]
  if len(rows)!=1 or rows[0][2]!='static':raise ValueError('Unbounded N30 worker frame: '+tail)
  return int(rows[0][1])
 paths['display_worker']=(4096,1536,in_file('display.cpp','void {anonymous}::worker(void*)')+frame('void n30::ClockHold::draw(uint8_t*, const n30::Model&, const n30::View&, uint64_t) const')+frame('void n30::render(uint8_t*, const Model&, const View&, uint64_t)')+frame('void n30::{anonymous}::dial(uint8_t*, int, uint64_t, uint16_t)')+frame('void n30::{anonymous}::line(uint8_t*, int, int, int, int)')+frame('void n30::{anonymous}::pixel(uint8_t*, int, int, bool)'))
 paths['radio_receive']=(8192,4096,in_file('native_lora.cpp','void {anonymous}::worker(void*)')+in_file('native_lora.cpp','void {anonymous}::worker_step(bool)')+in_file('native_lora.cpp','int {anonymous}::receive()')+frame('n30_rc8_receive')+frame('rc8_receiver_process')+frame('rc8_receiver_process_origin'))
 if json.loads((Path(out)/'receipt.json').read_text()).get('version') in ('0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14'):
  paths['timing_reply']=(result['configured_bytes'],4096,root+frame('void {anonymous}::respond_status(uint64_t, uint64_t)')+frame('void {anonymous}::lora_status_frame(uint64_t, uint64_t)')+frame('void n30_timing_frame(v12::ConsoleFrame&, uint32_t, uint64_t)'))
  paths['timing_start']=(result['configured_bytes'],4096,root+frame('bool n30_timing_start(uint64_t, uint64_t, uint32_t, uint32_t)')+frame('bool n30::timing::Recorder::start(uint64_t, uint64_t, uint32_t, uint32_t, uint64_t)'))
  config=(Path(out)/'target/sdkconfig').read_text();host=re.findall(r'^CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=(\d+)$',config,re.M)
  if len(host)!=1:raise ValueError('One N30 BLE host stack configuration required')
  paths['timing_pair_worker']=(int(host[0]),4096,frame('void v12::r2_host_poll()')+frame('void v12::{anonymous}::active_poll(uint64_t)')+frame('unsigned int v12::{anonymous}::periodic_poll(uint64_t, unsigned int)')+frame('bool v12::G1CorrectedClock::receive(const r2_relay_packet&, const v12::periodic_rx::Stamp&, uint64_t, uint64_t, const v12::G1Anchor&, rc5_origin*)')+frame('void v12::G1CorrectedClock::observe_clock(const Original&, uint64_t, uint64_t)')+frame('void {anonymous}::pair(const v12::n30_probe::Pair&)')+frame('void n30::timing::Recorder::pair(const v12::n30_probe::Pair&, uint64_t)'))
 if json.loads((Path(out)/'receipt.json').read_text()).get('version') in ('0.2.12','0.2.13','0.2.14'):
  paths['fine_reply']=(result['configured_bytes'],4096,root+frame('void {anonymous}::respond_status(uint64_t, uint64_t)')+frame('void {anonymous}::lora_status_frame(uint64_t, uint64_t)')+frame('void n30_fine_frame(v12::ConsoleFrame&, uint32_t, uint64_t)'))
  paths['fine_start']=(result['configured_bytes'],4096,root+frame('bool n30_fine_start(uint64_t, uint64_t, uint64_t, uint32_t, uint32_t)')+frame('bool n30::fine::History::start(uint64_t, uint64_t, uint32_t, uint32_t)'))
  if json.loads((Path(out)/'receipt.json').read_text()).get('version') in ('0.2.13','0.2.14'):
   def fine_function(name):
    unit=list((Path(out)/'target').rglob('fine_capture.cpp.su'))
    if len(unit)!=1:raise ValueError('One fine capture stack unit required')
    rows=[r.split('\t') for r in unit[0].read_text().splitlines() if re.search(r':bool (?:\{anonymous\}::)?'+re.escape(name)+r'\(',r.split('\t')[0])]
    if len(rows)!=1 or rows[0][2]!='static':raise ValueError('Missing/ambiguous fine stack function: '+name)
    return int(rows[0][1])
   startup=fine_function('fine_start')+frame('bool n30::fine::History::start(uint64_t, uint64_t, uint32_t, uint32_t)')
   paths['fine_start']=(result['configured_bytes'],4096,root+frame('bool n30_fine_start(uint64_t, uint64_t, uint64_t, uint32_t, uint32_t)')+startup)
   paths['fine_probe']=(result['configured_bytes'],4096,root+frame('bool n30_fine_probe(uint64_t, uint64_t, uint64_t, uint32_t, uint32_t)')+startup)
   paths['fine_poll']=(result['configured_bytes'],4096,f['main']+frame('void n30_fine_poll(uint64_t)')+fine_function('current')+fine_function('latch')+frame('bool n30::fine::Counter::current(uint32_t, uint32_t, uint64_t, n30::fine::Time&)')+fine_function('probe_tick')+fine_function('level_matches'))
  else:
   paths['fine_poll']=(result['configured_bytes'],4096,f['main']+frame('void n30_fine_poll(uint64_t)')+in_file('fine_capture.cpp','bool {anonymous}::current(uint64_t, n30::fine::Time&)')+in_file('fine_capture.cpp','bool {anonymous}::latch(uint32_t&)')+frame('bool n30::fine::Counter::current(uint32_t, uint32_t, uint64_t, n30::fine::Time&)'))
 result['n30_selected_paths']={}
 for name,(configured,reserve,selected) in paths.items():
  if selected+reserve>configured:raise ValueError('N30 stack budget exceeded: '+name)
  result['n30_selected_paths'][name]={'configured_bytes':configured,'selected_static_bytes':selected,'uncounted_callee_reserve_bytes':reserve,'headroom_after_reserve_bytes':configured-reserve-selected}
 return result

def verify_sdk(sdk,lock):
 pin=lock['esp_idf']
 if git(sdk,'rev-parse','HEAD')!=pin['commit'] or git(sdk,'status','--porcelain','--untracked-files=all'):raise ValueError('Pinned clean public ESP-IDF required')
 status=git(sdk,'submodule','status','--recursive')
 if any(line[:1] in '-+U' and not line.endswith('components/openthread/openthread/third_party/mbedtls/repo') for line in status.splitlines()):raise ValueError('SDK submodule missing or wrong revision')
 subprocess.run(['git','-C',str(sdk),'submodule','foreach','--recursive','--quiet','test -z "$(git status --porcelain --untracked-files=all)"'],check=True,stdout=subprocess.DEVNULL)
 for name,hashkey in [('components/bt/controller/lib_esp32c3_family/esp32s3/libbtdm_app.a','controller_sha256'),('components/esp_driver_gptimer/src/gptimer_priv.h','gptimer_private_header_sha256')]:
  if sha(sdk/name)!=pin[hashkey]:raise ValueError('SDK controller/private ABI mismatch')
 return status

def validate_configuration(config,compiled,lora_rx=True):
 for flag in ('CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y','CONFIG_ESP_CONSOLE_UART_DEFAULT=y','CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200'):
  if config.splitlines().count(flag)!=1:raise ValueError('Wrong N30 flash/console configuration: '+flag)
 if 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y' in config:raise ValueError('N30 USB bridge must use UART console')
 for row in compiled:
  if '/firmware/' not in row['file']:continue
  for flag in ('-DPTOB_N30=1','-DPTOB_GNSS_DISABLED=1','-DPTOB_N30_LORA_RX_ONLY=1' if lora_rx else '-DV12_LORA_FORCED_OFF=1','-DW12_GNSS_VERIFIED_INPUTS=0'):
   if flag not in row['command'].split():raise ValueError('N30 capability compile flag missing: '+flag)
  if row['file'].endswith('/gnss_pps.cpp'):raise ValueError('Physical GNSS driver forbidden')

def main():
 match=re.search(r'^set\(PROJECT_VER "([^\"]+)"\)$',(APP/'CMakeLists.txt').read_text(),re.MULTILINE)
 if not match or match.group(1)!=VERSION:raise ValueError('Python/CMake firmware versions must agree before building')
 p=argparse.ArgumentParser(description=__doc__)
 for name in ['idf','out','enrollment','device-config']:p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--profile',choices=['RC'],required=True);p.add_argument('--listening-percent',type=int,choices=[50,100],default=100)
 p.add_argument('--lora-transmitter-factory',default='0000000000000000',help='Exact configured T096 RC8 transmitter factory; zero disables hardware. RX only, fixed915MHz wire3')
 p.add_argument('--fine-shadow-profile',type=Path,help='Ignored exact-factory GPIO4/5 physical/schematic review profile; absent means CURRENT-only and no driven pads')
 p.add_argument('--commissioning',action='store_true',help='Clean-source candidate for separately authorized exact-device first installation; no hardware access');p.add_argument('--estimated-relay',action='store_true');p.add_argument('--registry',type=Path,default=REPO/'registry/example.json');p.add_argument('--development',action='store_true');a=p.parse_args()
 os.umask(0o077);sdk=a.idf.resolve();out=a.out.resolve()
 if out.exists() or REPO/'artifacts' not in out.parents:p.error('Fresh output directory below ignored artifacts/ required')
 if a.estimated_relay and a.listening_percent!=100:p.error('Estimated relay requires BLE100')
 for path in [a.enrollment,a.device_config]:
  if subprocess.run(['git','-C',str(REPO),'check-ignore','--quiet',str(path.resolve())]).returncode:p.error('Enrollment and device inputs must be Git-ignored')
 dirty=bool(git(REPO,'status','--porcelain','--untracked-files=all'))
 if dirty and a.commissioning:p.error('Commissioning requires a clean committed source checkout')
 if dirty and not a.development:p.error('Commit reviewed public inputs before a candidate build')
 lock=json.loads((APP/'dependencies.lock.json').read_text());modules=verify_sdk(sdk,lock)
 e=json.loads(a.enrollment.read_text());d=json.loads(a.device_config.read_text());key=validate_inputs(e,d);registry=load_registry(a.registry)
 validate_profile_mode(d,a.estimated_relay,a.listening_percent)
 if registry['devices'].get(d['device_id'])!={'alias':d['label'],'target':'n30'} and not (registry['devices'].get(d['device_id'],{}).get('alias')==d['label'] and registry['devices'][d['device_id']].get('target')=='n30'):p.error('Onboard the exact N30 factory identity in the shared registry first')
 tx_factory=a.lora_transmitter_factory
 validate_transmitter(tx_factory,d,registry)
 fp=fine_profile(d,a.fine_shadow_profile)
 if a.fine_shadow_profile and subprocess.run(['git','-C',str(REPO),'check-ignore','--quiet',str(a.fine_shadow_profile.resolve())]).returncode:p.error('Fine profile must be ignored local input')
 out.mkdir(parents=True);target=out/'target';target.mkdir();inputs=out/'local';inputs.mkdir()
 sources={str(f.relative_to(REPO)):sha(f) for f in source_files()};source_hash=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
 for n in sources:
  q=out/'source'/n;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(REPO/n,q)
 for path,name in [(a.enrollment,'enrollment.json'),(a.device_config,'device.json'),(a.registry,'registry.json'),(REPO/'registry/registry.py','registry_generator.py')]:shutil.copyfile(path,inputs/name)
 (inputs/'ptob_registry.h').write_text(registry_header(registry))
 (inputs/'ptob_device.h').write_text(device_header(d))
 fine_config=inputs/'fine_capture_config.h';fine_config.write_text(fine_header(fp));dump(inputs/'fine-profile.json',fp)
 lora_header=inputs/'n30_lora_config.h';lora_header.write_text(lora_config_header(d,e,tx_factory))
 # Registry/generator/device hashes bind display metadata and local input changes into runtime enrollment identity.
 enrollment_header=inputs/'r2-enrollment.h';enrollment_header.write_text('/* source:'+source_hash+' registry:'+sha(a.registry)+' generator:'+sha(REPO/'registry/registry.py')+' device:'+sha(a.device_config)+' fine:'+sha(fine_config)+' */\nstatic const r2_group_enrollment provision={{{'+','.join(map(str,key))+'},UINT64_C(0x'+e['group_id']+'),'+str(e['key_epoch'])+'},UINT64_C(0x'+d['device_id']+'),'+json.dumps(d['label'])+',40000};\n')
 r={'schema':1,'artifact_class':'PTOB_N30_RC','version':VERSION,'status':'INCOMPLETE','public_commit':git(REPO,'rev-parse','HEAD'),'source_dirty':dirty,'source_sha256':source_hash,'sources':sources,'dependencies':lock,'sdk_path':str(sdk),'app_path':str(APP),'sdk_submodules':modules.splitlines(),'profile':'RC','listening_percent':a.listening_percent,'estimated_relay':a.estimated_relay,'tx_actual_capture':False,'precision':'UNQUALIFIED','installed':False,'no_time_sync_watchdog_us':300000000,'source_recovery_policy':'ACCUMULATED_LOCK_LOSS_V1','physical_fit_policy':'CAPTURE_FROZEN_DENSE64_1S_ACCEL_V1','timing_diagnostics':'RUNTIME_RING16_CURSOR_V1','fine_capture_profile':fp,'fine_capture':'RAW_MCPWM1_CURRENT_SW128_RING32_V1','commissioning_candidate':a.commissioning,'hardware_validation':'NOT_PERFORMED','deployment_authorized':False,'registry_sha256':sha(a.registry),'registry_generator_sha256':sha(REPO/'registry/registry.py'),'lora_transmitter_factory':tx_factory,'lora_contract':'RC8_WIRE3_PASSIVE_RX_SX1262_915_V3','enrollment_mode':'LOCAL_BUILD_TIME_NO_NVS_WRITE','device_config_sha256':sha(a.device_config),'provisional_path_profile':provisional_profile(d)}
 dump(out/'receipt.json',r)
 env=os.environ.copy()
 for k in ['CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','EXTRA_COMPONENT_DIRS','SDKCONFIG_DEFAULTS']:env.pop(k,None)
 # This target has no managed components; use only the pinned SDK and local source.
 env.update(IDF_COMPONENT_MANAGER='0',IDF_PATH=str(sdk),PYTHONPYCACHEPREFIX=str(out/'pycache'),IDF_CCACHE_ENABLE='0')
 command=[sys.executable,str(sdk/'tools/idf.py'),'-C',str(APP),'-B',str(target),'-G','Unix Makefiles','-D','SDKCONFIG='+str(target/'sdkconfig'),'-D','V12_SOURCE_REVISION='+r['public_commit'],'-D','V12_R2_ENROLLMENT_HEADER='+str(enrollment_header),'-D','PTOB_DEVICE_HEADER='+str(inputs/'ptob_device.h'),'-D','PTOB_REGISTRY_HEADER='+str(inputs/'ptob_registry.h'),'-D','PTOB_LORA_HEADER='+str(lora_header),'-D','PTOB_FINE_HEADER='+str(fine_config),'-D','V12_R2_G1_LISTENING_PERCENT='+str(a.listening_percent),'-D','V12_R2_RC7_TX_ESTIMATE_LAB='+('ON' if a.estimated_relay else 'OFF'),'-D','CCACHE_ENABLE=OFF','build']
 dump(out/'command.json',command)
 try:
  with (out/'build.log').open('w') as log:subprocess.run(command,env=env,cwd=REPO,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=900)
  verify_sdk(sdk,lock)
  if sources!={str(f.relative_to(REPO)):sha(f) for f in source_files()}:raise ValueError('Source changed during build')
  image=target/'ptob_n30.bin'
  if not 0<image.stat().st_size<=lock['application_max_bytes']:raise ValueError('Application exceeds preserved slot ceiling')
  config=(target/'sdkconfig').read_text()
  for k in ['CONFIG_BT_NIMBLE_NVS_PERSIST=y','CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=y']:
   if k in config:raise ValueError('Forbidden storage/radio configuration')
  validate_configuration(config,json.loads((target/'compile_commands.json').read_text()))
  compiler=next(x for x in json.loads((target/'compile_commands.json').read_text()) if x['file'].endswith('/native_main.cpp'))
  toolchain_version=subprocess.check_output([compiler['command'].split()[0],'-dumpfullversion'],text=True).strip()
  if toolchain_version!='14.2.0':raise ValueError('Expected Xtensa GCC14.2.0')
  compiler_path=Path(compiler['command'].split()[0])
  compiler_banner=subprocess.check_output([str(compiler_path),'--version'],text=True).splitlines()[0]
  if 'esp-14.2.0_20260121' not in compiler_banner:raise ValueError('Compiler release mismatch')
  r['compiler_version']=compiler_banner
  nm=str(compiler_path.with_name(re.sub(r'(g\+\+|gcc)$','nm',compiler_path.name)))
  symbols=subprocess.check_output([nm,str(target/'ptob_n30.elf')],text=True)
  if re.search(r'\b(esp_wifi_init|esp_wifi_start|nvs_flash_init|nvs_set_blob|nvs_set_u32)\b',symbols):raise ValueError('Unexpected WiFi activation or NVS write dependency')
  if '-DPTOB_N30_LORA_RX_ONLY=1' not in compiler['command']:raise ValueError('LoRa receiver-only boundary missing')
  identity=json.loads((target/'v12_build_identity.json').read_text());r.update(runtime_identity=identity,application='target/ptob_n30.bin',application_sha256=sha(image),application_bytes=image.stat().st_size,artifacts={str(q.relative_to(out)):sha(q) for q in out.rglob('*') if q.is_file() and (q.suffix in ['.bin','.elf','.map','.su'] or q.name in ['sdkconfig','compile_commands.json','v12_build_identity.json','r2-enrollment.h','ptob_device.h','ptob_registry.h','enrollment.json','device.json','registry.json','registry_generator.py','n30_lora_config.h','fine_capture_config.h','fine-profile.json'])},status='COMMISSIONING_CANDIDATE_PHYSICAL_UNKNOWN' if a.commissioning else 'OFFLINE_NOT_FLASHABLE')
  r['main_stack_budget']=check_main_stack(out)
  r['compiled_sources']={}
  for row in json.loads((target/'compile_commands.json').read_text()):
   q=Path(row['file']).resolve()
   for prefix,base in [('sdk',sdk),('generated',out),('repo',REPO)]:
    if q.is_relative_to(base):r['compiled_sources'][prefix+'/'+str(q.relative_to(base))]=sha(q);break
   else:raise ValueError('Unexpected compiled source')
  dump(out/'receipt.json',r)
  from inspect_build import inspect
  dump(out/'inspection.json',inspect(out))
  print('Offline build and identity/layout checks passed (hardware unvalidated):',out)
 except BaseException:r['status']='FAILED';dump(out/'receipt.json',r);raise
if __name__=='__main__':main()
