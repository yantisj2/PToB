#!/usr/bin/env python3
"""Offline T-Watch Ultra SX1262 build. Never opens hardware; outputs are not install authorization.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,importlib.util,json,os,re,shutil,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1]
sys.path.insert(0,str(REPO/'registry'))
from registry import load as load_registry,header as registry_header
spec=importlib.util.spec_from_file_location('n30_builder',REPO/'firmware/n30/tools/build.py');backend=importlib.util.module_from_spec(spec);spec.loader.exec_module(backend)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,v):p.write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('idf','out','enrollment','device-config','registry'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--lora-transmitter-factory',default='0000000000000000');a=p.parse_args();os.umask(0o077)
 out=a.out.resolve();sdk=a.idf.resolve()
 if out.exists() or REPO/'artifacts' not in out.parents:p.error('Fresh ignored artifacts output required')
 for q in (a.enrollment,a.device_config,a.registry):
  if subprocess.run(['git','-C',str(REPO),'check-ignore','--quiet',str(q.resolve())]).returncode:p.error('Private input must be ignored')
 e=json.loads(a.enrollment.read_text());d=json.loads(a.device_config.read_text());registry=load_registry(a.registry)
 if set(d)!={'board','factory_mac','device_id','label','legacy_wire_id','radio'} or d['board']!='lilygo_twatch_ultra_v1' or d['radio']!='sx1262':p.error('Exact Ultra V1 SX1262 device schema required')
 # Validate shared identity/enrollment schema without adding a board alias to W12.
 check={k:v for k,v in d.items() if k!='radio'};check['board']='meshnology_w12_esp32s3'
 backend.shared_path_builder().validate_inputs(e,check)
 if registry['devices'].get(d['device_id'],{}).get('target')!='twatch' or registry['devices'][d['device_id']].get('alias')!=d['label']:p.error('Exact device must be in shared registry')
 backend.validate_transmitter(a.lora_transmitter_factory,d,registry)
 lock=json.loads((REPO/'firmware/n30/dependencies.lock.json').read_text());backend.verify_sdk(sdk,lock)
 paths={q for q in backend.source_files() if q.suffix in ('.c','.cpp','.h','.hpp','.inc','.lf','.cmake') or q.name in ('CMakeLists.txt','sdkconfig.defaults','sdkconfig.g1.defaults')};paths|={q for q in APP.rglob('*') if q.is_file() and '__pycache__' not in q.parts and (q.suffix in ('.c','.cpp','.h','.hpp','.inc','.lf','.cmake') or q.name in ('CMakeLists.txt','sdkconfig.defaults','sdkconfig.g1.defaults','build.py'))};paths.add(REPO/'tools/firmware_artifacts.py')
 sources={str(q.relative_to(REPO)):sha(q) for q in sorted(paths)};source_hash=hashlib.sha256(json.dumps(sources,sort_keys=True).encode()).hexdigest()
 out.mkdir(parents=True);local=out/'local';local.mkdir();target=out/'target'
 for name in sources:
  q=out/'source'/name;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(REPO/name,q)
 for q,name in ((a.enrollment,'enrollment.json'),(a.device_config,'device.json'),(a.registry,'registry.json')):shutil.copyfile(q,local/name)
 (local/'ptob_registry.h').write_text(registry_header(registry));(local/'ptob_device.h').write_text(backend.shared_path_builder().device_header(check))
 (local/'n30_lora_config.h').write_text(backend.lora_config_header(d,e,a.lora_transmitter_factory))
 (local/'fine_capture_config.h').write_text(backend.fine_header(backend.fine_profile(d,None)))
 key=bytes.fromhex(e['psk']);(local/'r2-enrollment.h').write_text('/* source:'+source_hash+' */\nstatic const r2_group_enrollment provision={{{'+','.join(map(str,key))+'},UINT64_C(0x'+e['group_id']+'),'+str(e['key_epoch'])+'},UINT64_C(0x'+d['device_id']+'),'+json.dumps(d['label'])+',40000};\n')
 commit=backend.git(REPO,'rev-parse','HEAD');r={'schema':1,'artifact_class':'PTOB_TWATCH_RC','version':'0.1.3','status':'INCOMPLETE','profile':'RC','device_id':d['device_id'],'device_config_sha256':sha(a.device_config),'source_dirty':True,'public_commit':commit,'source_sha256':source_hash,'sources':sources,'sdk_path':str(sdk),'precision':'UNQUALIFIED','installed':False,'hardware_validation':'NOT_PERFORMED','listening_percent':100,'estimated_relay':False,'clock_backend':{'mode':'LEGACY_GPTIMER20','counter_hz':20000000},'lora_transmitter_factory':a.lora_transmitter_factory,'GNSS':'POWER_OFF','radio':'SX1262_RX_ONLY','application_max_bytes':1536000}
 dump(out/'receipt.json',r)
 env=os.environ.copy();env.update(IDF_COMPONENT_MANAGER='0',IDF_PATH=str(sdk),IDF_CCACHE_ENABLE='0',PYTHONPYCACHEPREFIX=str(out/'pycache'))
 for k in ('CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','EXTRA_COMPONENT_DIRS','SDKCONFIG_DEFAULTS'):env.pop(k,None)
 cmd=[sys.executable,str(sdk/'tools/idf.py'),'-C',str(APP),'-B',str(target),'-G',('Ninja' if shutil.which('ninja') else 'Unix Makefiles'),'-D','SDKCONFIG='+str(target/'sdkconfig'),'-D','V12_SOURCE_REVISION='+commit]
 for name,file in [('V12_R2_ENROLLMENT_HEADER','r2-enrollment.h'),('PTOB_DEVICE_HEADER','ptob_device.h'),('PTOB_REGISTRY_HEADER','ptob_registry.h'),('PTOB_LORA_HEADER','n30_lora_config.h'),('PTOB_FINE_HEADER','fine_capture_config.h')]:cmd+=['-D',name+'='+str(local/file)]
 cmd+=['build'];dump(out/'command.json',cmd)
 try:
  with (out/'build.log').open('w') as f:subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=900)
  image=target/'ptob_twatch.bin';assert 0<image.stat().st_size<=1536000
  if sources!={str(q.relative_to(REPO)):sha(q) for q in sorted(paths)}:raise ValueError('Source changed during build')
  config=(target/'sdkconfig').read_text()
  for flag in ('CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y','CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y','CONFIG_SPIRAM_MODE_QUAD=y'):
   if flag not in config.splitlines():raise ValueError('Missing board configuration: '+flag)
  commands=json.loads((target/'compile_commands.json').read_text());r['compiled_sources']={}
  for entry in commands:
   q=Path(entry['file']).resolve()
   if q.is_relative_to(out):
    r['compiled_sources']['generated/'+str(q.relative_to(out))]=sha(q)
   elif q.is_relative_to(REPO) and not q.is_relative_to(sdk):
    name=str(q.relative_to(REPO))
    if name not in sources:raise ValueError('Uncaptured source: '+name)
    r['compiled_sources']['repo/'+name]=sha(q)
  r.update(status='OFFLINE_NOT_FLASHABLE',application='target/ptob_twatch.bin',application_sha256=sha(image),application_bytes=image.stat().st_size,runtime_identity=json.loads((target/'v12_build_identity.json').read_text()))
  r['artifacts']={str(q.relative_to(out)):sha(q) for base in (local,target) for q in base.rglob('*') if q.is_file() and (base==local or q.suffix in ('.bin','.elf','.map','.su') or str(q.relative_to(out)) in {key.removeprefix('generated/') for key in r['compiled_sources'] if key.startswith('generated/')} or q.name in ('sdkconfig','compile_commands.json','v12_build_identity.json'))}
  dump(out/'receipt.json',r);print('PASS: offline native watch build; hardware and calibration unqualified')
 except BaseException:r['status']='FAILED';dump(out/'receipt.json',r);raise
if __name__=='__main__':
 sys.path.insert(0,str(REPO/'tools'));from firmware_artifacts import run_builder
 run_builder(main,REPO)
