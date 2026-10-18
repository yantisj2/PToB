#!/usr/bin/env python3
"""Build W12 RC with public ESP-IDF and ignored local inputs; never opens USB.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,shutil,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1]
sys.path.insert(0,str(REPO/'registry'))
from registry import load as load_registry,header as registry_header
VERSION='0.1.15'
GNSS_PROFILE='meshnology_w12_l76k_rx39_pps41'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def git(p,*args):return subprocess.check_output(['git','-C',str(p),*args],text=True).strip()
def dump(p,v):p.write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
def source_files():return sorted(p for p in APP.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc')
def validate_inputs(enrollment,device):
 if set(enrollment)!={'group_id','key_epoch','psk'}:raise ValueError('Expected shared group enrollment schema')
 group=enrollment['group_id'];epoch=enrollment['key_epoch'];key=bytes.fromhex(enrollment['psk'])
 if not(re.fullmatch('[0-9a-f]{16}',group) and int(group,16) and type(epoch)is int and 0<epoch<2**32 and len(key)==32 and any(key)):raise ValueError('Invalid group enrollment')
 required={'board','factory_mac','device_id','label','legacy_wire_id'}
 if set(device) not in (required,required|{'gnss_profile'}):raise ValueError('Unexpected device fields')
 if 'gnss_profile' in device and device['gnss_profile']!=GNSS_PROFILE:raise ValueError('Unsupported GNSS route; explicit reviewed W12 L76K RX39/PPS41 profile required')
 mac=device['factory_mac'];identity=device['device_id'];label=device['label'];wire=device['legacy_wire_id']
 if not(device['board']=='meshnology_w12_esp32s3' and re.fullmatch('[0-9a-f]{2}(:[0-9a-f]{2}){5}',mac) and int(mac.replace(':',''),16) and not int(mac[:2],16)&1 and identity=='0000'+mac.replace(':','') and re.fullmatch('[A-Za-z0-9_-]{1,24}',label) and type(wire)is int and 1<=wire<=254):raise ValueError('Explicit W12 factory binding/label required')
 return key

def device_header(device):
 return ('#pragma once\n#define PTOB_FACTORY_MAC {'+','.join('0x'+x for x in device['factory_mac'].split(':'))+'}\n#define PTOB_DEVICE_LABEL '+json.dumps(device['label'])+'\n#define PTOB_LEGACY_WIRE_ID '+str(device['legacy_wire_id'])+'\n#define PTOB_GNSS_INDICATORS '+str(int('gnss_profile' in device))+'\n')

def verify_sdk(sdk,lock):
 pin=lock['esp_idf']
 if git(sdk,'rev-parse','HEAD')!=pin['commit'] or git(sdk,'status','--porcelain','--untracked-files=all'):raise ValueError('Pinned clean public ESP-IDF required')
 status=git(sdk,'submodule','status','--recursive')
 if any(line[:1] in '-+U' and not line.endswith('components/openthread/openthread/third_party/mbedtls/repo') for line in status.splitlines()):raise ValueError('SDK submodule missing or wrong revision')
 subprocess.run(['git','-C',str(sdk),'submodule','foreach','--recursive','--quiet','test -z "$(git status --porcelain --untracked-files=all)"'],check=True,stdout=subprocess.DEVNULL)
 for name,hashkey in [('components/bt/controller/lib_esp32c3_family/esp32s3/libbtdm_app.a','controller_sha256'),('components/esp_driver_gptimer/src/gptimer_priv.h','gptimer_private_header_sha256')]:
  if sha(sdk/name)!=pin[hashkey]:raise ValueError('SDK controller/private ABI mismatch')
 return status

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ['idf','out','enrollment','device-config']:p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--profile',choices=['RC'],required=True);p.add_argument('--listening-percent',type=int,choices=[50,100],default=100)
 p.add_argument('--estimated-relay',action='store_true');p.add_argument('--registry',type=Path,default=REPO/'registry/example.json');p.add_argument('--development',action='store_true');a=p.parse_args()
 os.umask(0o077);sdk=a.idf.resolve();out=a.out.resolve()
 if out.exists() or REPO/'artifacts' not in out.parents:p.error('Fresh output directory below ignored artifacts/ required')
 if a.estimated_relay and a.listening_percent!=100:p.error('Estimated relay requires BLE100')
 for path in [a.enrollment,a.device_config]:
  if subprocess.run(['git','-C',str(REPO),'check-ignore','--quiet',str(path.resolve())]).returncode:p.error('Enrollment and device inputs must be Git-ignored')
 dirty=bool(git(REPO,'status','--porcelain','--untracked-files=all'))
 if dirty and not a.development:p.error('Commit reviewed public inputs before a candidate build')
 lock=json.loads((APP/'dependencies.lock.json').read_text());modules=verify_sdk(sdk,lock)
 e=json.loads(a.enrollment.read_text());d=json.loads(a.device_config.read_text());key=validate_inputs(e,d);registry=load_registry(a.registry)
 out.mkdir(parents=True);target=out/'target';target.mkdir();inputs=out/'local';inputs.mkdir()
 sources={str(f.relative_to(APP)):sha(f) for f in source_files()};source_hash=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
 for n in sources:
  q=out/'source'/n;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(APP/n,q)
 for path,name in [(a.enrollment,'enrollment.json'),(a.device_config,'device.json'),(a.registry,'registry.json'),(REPO/'registry/registry.py','registry_generator.py')]:shutil.copyfile(path,inputs/name)
 (inputs/'ptob_registry.h').write_text(registry_header(registry))
 (inputs/'ptob_device.h').write_text(device_header(d))
 # Registry/generator/device hashes bind display metadata and local input changes into runtime enrollment identity.
 enrollment_header=inputs/'r2-enrollment.h';enrollment_header.write_text('/* source:'+source_hash+' registry:'+sha(a.registry)+' generator:'+sha(REPO/'registry/registry.py')+' device:'+sha(a.device_config)+' */\nstatic const r2_group_enrollment provision={{{'+','.join(map(str,key))+'},UINT64_C(0x'+e['group_id']+'),'+str(e['key_epoch'])+'},UINT64_C(0x'+d['device_id']+'),'+json.dumps(d['label'])+',40000};\n')
 r={'schema':1,'artifact_class':'PTOB_W12_RC','version':VERSION,'status':'INCOMPLETE','public_commit':git(REPO,'rev-parse','HEAD'),'source_dirty':dirty,'source_sha256':source_hash,'sources':sources,'dependencies':lock,'sdk_path':str(sdk),'app_path':str(APP),'sdk_submodules':modules.splitlines(),'profile':'RC','listening_percent':a.listening_percent,'estimated_relay':a.estimated_relay,'tx_actual_capture':False,'precision':'UNQUALIFIED','installed':False,'registry_sha256':sha(a.registry),'registry_generator_sha256':sha(REPO/'registry/registry.py'),'enrollment_mode':'LOCAL_BUILD_TIME_NO_NVS_WRITE','device_config_sha256':sha(a.device_config)}
 dump(out/'receipt.json',r)
 env=os.environ.copy()
 for k in ['CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','EXTRA_COMPONENT_DIRS','SDKCONFIG_DEFAULTS']:env.pop(k,None)
 # This target has no managed components; use only the pinned SDK and local source.
 env.update(IDF_COMPONENT_MANAGER='0',IDF_PATH=str(sdk),PYTHONPYCACHEPREFIX=str(out/'pycache'),IDF_CCACHE_ENABLE='0')
 command=[sys.executable,str(sdk/'tools/idf.py'),'-C',str(APP),'-B',str(target),'-G','Unix Makefiles','-D','SDKCONFIG='+str(target/'sdkconfig'),'-D','V12_SOURCE_REVISION='+r['public_commit'],'-D','V12_R2_ENROLLMENT_HEADER='+str(enrollment_header),'-D','PTOB_DEVICE_HEADER='+str(inputs/'ptob_device.h'),'-D','PTOB_REGISTRY_HEADER='+str(inputs/'ptob_registry.h'),'-D','PTOB_W12_GNSS_INDICATORS='+('ON' if 'gnss_profile' in d else 'OFF'),'-D','V12_R2_G1_LISTENING_PERCENT='+str(a.listening_percent),'-D','V12_R2_RC7_TX_ESTIMATE_LAB='+('ON' if a.estimated_relay else 'OFF'),'-D','CCACHE_ENABLE=OFF','build']
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
  if '-DV12_LORA_FORCED_OFF=1' not in compiler['command']:raise ValueError('LoRa disabled boundary missing')
  identity=json.loads((target/'v12_build_identity.json').read_text());r.update(runtime_identity=identity,application='target/ptob_w12.bin',application_sha256=sha(image),application_bytes=image.stat().st_size,artifacts={str(q.relative_to(out)):sha(q) for q in out.rglob('*') if q.is_file() and (q.suffix in ['.bin','.elf','.map','.su'] or q.name in ['sdkconfig','compile_commands.json','v12_build_identity.json','r2-enrollment.h','ptob_device.h','ptob_registry.h','enrollment.json','device.json','registry.json','registry_generator.py'])},status='DEVELOPMENT_NOT_FLASHABLE' if dirty else 'BUILD_COMPLETE_PHYSICAL_UNKNOWN')
  from stack_budget import check as check_main_stack
  r['main_stack_budget']=check_main_stack(out)
  r['compiled_sources']={}
  for row in json.loads((target/'compile_commands.json').read_text()):
   q=Path(row['file']).resolve()
   for prefix,base in [('sdk',sdk),('app',APP),('generated',out)]:
    if q.is_relative_to(base):r['compiled_sources'][prefix+'/'+str(q.relative_to(base))]=sha(q);break
   else:raise ValueError('Unexpected compiled source')
  dump(out/'receipt.json',r)
  from inspect_build import inspect
  dump(out/'inspection.json',inspect(out,allow_development=a.development))
  print('Build and standalone identity/layout checks passed:',out)
 except BaseException:r['status']='FAILED';dump(out/'receipt.json',r);raise
if __name__=='__main__':main()
