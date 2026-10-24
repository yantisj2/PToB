#!/usr/bin/env python3
"""Build the CoreS3-SE client offline; secrets and images stay in ignored artifacts.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,importlib.util,io,json,os,re,shutil,subprocess,sys,tarfile
from pathlib import Path
from prepare_runtime import prepare
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1]
SHARED_REF='699f100c4e01db563f8e4b0a9f8d01d15e8600a4'
RECOVERY_REF='6ac2721becf37b3c5d22919eea9e25e24a4054cf'
RECOVERY_POLICY='AUTO_RC9_PASSIVE_NORMAL_RETRY_V1'
SHARED_PATHS=['firmware/n30/main','firmware/n30/CMakeLists.txt','firmware/n30/sdkconfig.defaults','firmware/n30/sdkconfig.g1.defaults','firmware/w12/main','firmware/w12/legacy','firmware/ss3/main/td_publication.hpp',*['firmware/t096/protocol/'+n for n in ('role_announcement.c','role_announcement.h','group_auth.c','group_auth.h')]]
sys.path.insert(0,str(REPO/'registry'))
from registry import load,header

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def dump(p,v):p.write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
def module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);sys.modules[name]=m;spec.loader.exec_module(m);return m

def sources():
 roots=[APP,REPO/'firmware/n30/main',REPO/'firmware/w12/main',REPO/'firmware/w12/legacy',REPO/'registry']
 files={p for base in roots for p in base.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc'}
 files|={REPO/'firmware/ss3/main/td_publication.hpp',REPO/'firmware/n30/CMakeLists.txt',REPO/'firmware/n30/sdkconfig.defaults',REPO/'firmware/n30/sdkconfig.g1.defaults'}
 files|={REPO/'firmware/n30/tools/build.py',REPO/'firmware/n30/dependencies.lock.json',REPO/'firmware/w12/tools/build.py'}
 files|={REPO/'firmware/t096/protocol'/n for n in ('role_announcement.c','role_announcement.h','group_auth.c','group_auth.h')}
 # GD2/legacy GD1 are current explicit dependencies; keep the tested receiver pin.
 files|={REPO/'firmware/t096/protocol'/n for n in ('gnss_diagnostics.c','gnss_diagnostics.h')}
 return {str(p.relative_to(REPO)):sha(p) for p in sorted(files)}

def freeze_shared(root):
 # Pin the receiver version tested on this target. Other repository targets
 # may advance independently while a CoreSE hardware comparison is running.
 for n in SHARED_PATHS:
  p=root/n
  if p.is_dir():shutil.rmtree(p)
  elif p.exists():p.unlink()
 data=subprocess.check_output(['git','-C',str(REPO),'archive',SHARED_REF,*SHARED_PATHS])
 with tarfile.open(fileobj=io.BytesIO(data)) as archive:
  for item in archive:
   name=Path(item.name)
   if name.is_absolute() or '..' in name.parts:raise ValueError('Unsafe source archive path')
   p=root/name
   if item.isdir():p.mkdir(parents=True,exist_ok=True)
   elif item.isfile():p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(archive.extractfile(item).read())
   else:raise ValueError('Unexpected source archive member')
 # Backport only the reviewed AUTO recovery change to this tested baseline.
 # No other target's calibration or clock-coordinate changes transfer here.
 patch=APP/'tools/auto_rc9_recovery.patch'
 expected={'firmware/w12/main/native_g1.cpp','firmware/w12/main/rc9_client_native.inc'}
 paths=set(re.findall(r'^diff --git a/(\S+) b/\1$',patch.read_text(),re.M))
 if paths!=expected:raise ValueError('Recovery patch target scope changed')
 args=['git','apply','--unsafe-paths','--directory='+str(root.resolve())]
 subprocess.run([*args,'--check',str(patch)],check=True,cwd=REPO)
 subprocess.run([*args,str(patch)],check=True,cwd=REPO)
 return {str(p.relative_to(root)):sha(p) for p in sorted(root.rglob('*')) if p.is_file()}

def inspect(out):
 out=Path(out);r=json.loads((out/'receipt.json').read_text());
 if r.get('kind')!='CORESE_LIVE_CLIENT' or r.get('version') not in ('0.1.1','0.1.2','0.1.3','0.1.4','0.1.5','0.1.6','0.1.7','0.1.8','0.1.9','0.1.10','0.1.11','0.1.12','0.1.13'):raise ValueError('Wrong client artifact class/version')
 target=out/'target';i=json.loads((target/'v12_build_identity.json').read_text())
 for n,h in r['sources'].items():
  if sha(out/'source'/n)!=h:raise ValueError('Retained source mismatch: '+n)
 for n,h in r['artifacts'].items():
  if sha(out/n)!=h:raise ValueError('Build artifact mismatch: '+n)
 if r['version'] in ('0.1.11','0.1.12','0.1.13'):
  recovery=r.get('shared_source_recovery',{})
  if r['shared_source_git_ref']!=SHARED_REF or recovery.get('commit')!=RECOVERY_REF or recovery.get('policy')!=RECOVERY_POLICY or recovery.get('patch_sha256')!=sha(out/'source/firmware/corese/tools/auto_rc9_recovery.patch'):raise ValueError('Recovery backport provenance mismatch')
 if r['version']=='0.1.12' and r.get('diagnostic_scan_policy')!='FRESH_ASSOCIATED_PA_12MS_MIN200MS_V1':raise ValueError('Diagnostic scan policy mismatch')
 if r['version']=='0.1.13' and (r.get('diagnostic_scan_policy')!='FRESH_ASSOCIATED_PA_12MS_MIN200MS_AGE10MS_V1' or r.get('retained_display_policy')!='LAST_PHYSICAL_DELIVERY_500MS_NO_PRECISION_V1'):raise ValueError('Scan/HOLD policy mismatch')
 config=(target/'sdkconfig').read_text().splitlines()
 for flag in ('CONFIG_SPIRAM=y','CONFIG_SPIRAM_MODE_QUAD=y','CONFIG_SPIRAM_SPEED_80M=y','CONFIG_SPIRAM_USE_MALLOC=y','CONFIG_BT_ENABLED=y','CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y','CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y','CONFIG_BT_CTRL_BLE_MAX_ACT_EFF=6'):
  if flag not in config:raise ValueError('Required setting missing: '+flag)
 for flag in ('CONFIG_BT_NIMBLE_NVS_PERSIST=y','CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=y','CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y','CONFIG_SPIRAM_MODE_OCT=y'):
  if flag in config:raise ValueError('Forbidden setting: '+flag)
 local=out/'local'
 fields=[r['public_commit'],r['source_sha256'],sha(target/'sdkconfig'),i['controller_sha256'],sha(local/'r2-enrollment.h'),sha(local/'ptob_device.h'),sha(local/'ptob_registry.h'),sha(local/'fine_capture_config.h'),'PTOB_CORESE_'+r['version']]
 if hashlib.sha256('|'.join(fields).encode()).hexdigest()!=i['build_id']:raise ValueError('Runtime binding mismatch')
 for row in json.loads((target/'compile_commands.json').read_text()):
  if not ('/firmware/' in row['file'] or '/runtime/' in row['file']):continue
  cmd=row['command'].split()
  for flag in ('-DPTOB_CORESE=1','-DPTOB_RC9=1','-DPTOB_GNSS_DISABLED=1','-DV12_LORA_FORCED_OFF=1'):
   if flag not in cmd:raise ValueError('Compile scope missing: '+flag)
  if '-DV12_R2_RC7_TX_ESTIMATE_LAB=1' in cmd or row['file'].endswith('/gnss_pps.cpp'):raise ValueError('Upstream serving/GNSS driver forbidden')
 if sha(out/r['application'])!=r['application_sha256']:raise ValueError('Image identity mismatch')
 # IDF's BT component requires esp_wifi, whose hidden Kconfig defaults to y.
 # Verify the linked executable has no Wi-Fi initialization/start entry points.
 symbols=subprocess.check_output(['xtensa-esp32s3-elf-nm','--defined-only',str(target/'ptob_corese.elf')],text=True)
 if re.search(r'\b(?:esp_wifi_init|esp_wifi_start|esp_wifi_connect)$',symbols,re.M):raise ValueError('Wi-Fi runtime owner linked')
 return {'status':'LIVE_CLIENT_BUILD_VERIFIED','build_id':i['build_id'],'application_bytes':r['application_bytes'],'application_sha256':r['application_sha256'],'precision':'UNQUALIFIED','relay':False,'hardware_access':False}

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('idf','dependencies','out','enrollment','device','registry'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--resume-failed',action='store_true',help='Retain a failed attempt and reuse its public SDK object build')
 a=p.parse_args();os.umask(0o077);out=a.out.resolve();sdk=a.idf.resolve();deps=a.dependencies.resolve()
 if REPO/'artifacts' not in out.parents:p.error('Ignored artifacts directory required')
 if out.exists():
  if not a.resume_failed or json.loads((out/'receipt.json').read_text()).get('status')!='FAILED':p.error('Only explicitly selected failed builds can resume')
  attempt=out/('failed-attempt-'+str(len(list(out.glob('failed-attempt-*')))+1));attempt.mkdir()
  for n in ('source','runtime','local','receipt.json','command.json','build.log'):
   if (out/n).exists():shutil.move(str(out/n),attempt/n)
 for path in (a.enrollment,a.device,a.registry):
  if subprocess.run(['git','-C',str(REPO),'check-ignore','--quiet',str(path.resolve())]).returncode:p.error('Local inputs must be ignored')
 n30=module('_corese_n30_build',REPO/'firmware/n30/tools/build.py');w12=module('_corese_w12_build',REPO/'firmware/w12/tools/build.py')
 n30.verify_sdk(sdk,json.loads((REPO/'firmware/n30/dependencies.lock.json').read_text()))
 pins=json.loads((APP/'dependencies.lock.json').read_text())
 for name,path in (('M5GFX',deps/'M5GFX'),('M5Unified',deps/'M5Unified')):
  if subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()!=pins[name]['revision'] or subprocess.check_output(['git','-C',str(path),'status','--porcelain','--untracked-files=no']):raise ValueError('Pinned clean dependency required: '+name)
 e=json.loads(a.enrollment.read_text());d=json.loads(a.device.read_text());registry=load(a.registry);key=bytes.fromhex(e['psk'])
 if set(e)!={'group_id','key_epoch','psk'} or not re.fullmatch('[0-9a-f]{16}',e['group_id']) or not int(e['group_id'],16) or type(e['key_epoch'])is not int or not 0<e['key_epoch']<2**32 or len(key)!=32 or not any(key):raise ValueError('Invalid enrollment')
 if set(d)!={'board','factory_mac','device_id','label','legacy_wire_id'} or d['board']!='m5stack_cores3_se' or not re.fullmatch('[0-9a-f]{2}(:[0-9a-f]{2}){5}',d['factory_mac']) or d['device_id']!='0000'+d['factory_mac'].replace(':','') or not 1<=d['legacy_wire_id']<=254:raise ValueError('Exact CoreS3-SE input required')
 if registry['devices'].get(d['device_id'],{}).get('target')!='corese' or registry['devices'][d['device_id']]['alias']!=d['label']:raise ValueError('Onboard exact CoreS3-SE identity first')
 out.mkdir(parents=True,exist_ok=True);local=out/'local';local.mkdir();current_sources=sources()
 for n in current_sources:
  q=out/'source'/n;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(REPO/n,q)
 snapshot=freeze_shared(out/'source');digest=hashlib.sha256(json.dumps(snapshot,sort_keys=True,separators=(',',':')).encode()).hexdigest()
 for value,n in ((e,'enrollment.json'),(d,'device.json'),(registry,'registry.json')):dump(local/n,value)
 (local/'ptob_device.h').write_text(w12.device_header(d));(local/'ptob_registry.h').write_text(header(registry));(local/'n30_lora_config.h').write_text('#pragma once\n// No LoRa hardware in this target.\n')
 (local/'fine_capture_config.h').write_text('#pragma once\n#include <cstdint>\n#define PTOB_N30_FINE_FACTORY UINT64_C(0x'+d['device_id']+')\n#define PTOB_N30_FINE_PINS 1\n#define PTOB_N30_FINE_RX_GPIO 6\n#define PTOB_N30_FINE_TX_GPIO 7\n#define PTOB_N30_FINE_RW 1\n#define PTOB_N30_FINE_BACKEND 1\n#define PTOB_N30_FINE_TRANSLATION80 UINT64_C(125440)\n')
 (local/'r2-enrollment.h').write_text('/* CoreSE source:'+digest+' */\nstatic const r2_group_enrollment provision={{{'+','.join(map(str,key))+'},UINT64_C(0x'+e['group_id']+'),'+str(e['key_epoch'])+'},UINT64_C(0x'+d['device_id']+'),'+json.dumps(d['label'])+',40000};\n')
 prepare(out/'runtime',out/'source');commit=subprocess.check_output(['git','-C',str(REPO),'rev-parse','HEAD'],text=True).strip()
 dirty=bool(subprocess.check_output(['git','-C',str(REPO),'status','--porcelain']))
 r={'schema':1,'kind':'CORESE_LIVE_CLIENT','version':'0.1.13','status':'INCOMPLETE','public_commit':commit,'shared_source_git_ref':SHARED_REF,'diagnostic_scan_policy':'FRESH_ASSOCIATED_PA_12MS_MIN200MS_AGE10MS_V1','retained_display_policy':'LAST_PHYSICAL_DELIVERY_500MS_NO_PRECISION_V1','shared_source_recovery':{'commit':RECOVERY_REF,'policy':RECOVERY_POLICY,'patch_sha256':sha(APP/'tools/auto_rc9_recovery.patch')},'source_dirty':dirty,'sources':snapshot,'source_sha256':digest,'dependencies':pins,'device_id':d['device_id'],'factory_mac':d['factory_mac'],'relay':False,'fine_gpio':[6,7],'calibration':'NONE','precision':'UNQUALIFIED','hardware_access':False,'installed':False}
 dump(out/'receipt.json',r);target=out/'target';env=os.environ.copy();env.update(IDF_PATH=str(sdk),IDF_COMPONENT_MANAGER='0',PYTHONPYCACHEPREFIX=str(out/'pycache'),IDF_CCACHE_ENABLE='0')
 for k in ('CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','EXTRA_COMPONENT_DIRS','SDKCONFIG_DEFAULTS'):env.pop(k,None)
 values={'SDKCONFIG':str(target/'sdkconfig'),'CORESE_LIVE':'ON','CORESE_DEPENDENCIES':str(deps),'CORESE_RUNTIME':str(out/'runtime'),'CORESE_SOURCE_SHA':digest,'V12_SOURCE_REVISION':commit,'V12_R2_ENROLLMENT_HEADER':str(local/'r2-enrollment.h'),'PTOB_DEVICE_HEADER':str(local/'ptob_device.h'),'PTOB_REGISTRY_HEADER':str(local/'ptob_registry.h'),'PTOB_FINE_HEADER':str(local/'fine_capture_config.h'),'PTOB_LORA_HEADER':str(local/'n30_lora_config.h'),'CCACHE_ENABLE':'OFF'}
 command=[sys.executable,str(sdk/'tools/idf.py'),'-C',str(out/'source/firmware/corese'),'-B',str(target),'-G','Unix Makefiles']
 for k,v in values.items():command+=['-D',k+'='+v]
 command+=['build'];dump(out/'command.json',command)
 try:
  with (out/'build.log').open('w') as log:subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=900)
  if any(sha(REPO/n)!=h for n,h in current_sources.items() if n.startswith(('firmware/corese/','registry/'))):raise ValueError('CoreSE source changed during build')
  image=target/'ptob_corese.bin'
  if not 0<image.stat().st_size<2*1024*1024:raise ValueError('App ceiling exceeded')
  r.update(application='target/ptob_corese.bin',application_bytes=image.stat().st_size,application_sha256=sha(image),runtime_identity=json.loads((target/'v12_build_identity.json').read_text()),artifacts={str(q.relative_to(out)):sha(q) for q in out.rglob('*') if q.is_file() and (q.suffix in ('.bin','.elf','.map','.su') or q.parent in (local,out/'runtime') or q.name in ('sdkconfig','compile_commands.json','v12_build_identity.json'))},status='BUILD_COMPLETE')
  dump(out/'receipt.json',r);print(json.dumps(inspect(out),indent=2))
 except Exception as error:r.update(status='FAILED',error=str(error));dump(out/'receipt.json',r);raise
if __name__=='__main__':main()
