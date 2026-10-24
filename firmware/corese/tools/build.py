#!/usr/bin/env python3
"""Build CoreS3-SE UI preparation offline. Never opens USB or flashes.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def git(p,*args):return subprocess.check_output(['git','-C',str(p),*args],text=True).strip()
def sources():
 paths=[p for p in APP.rglob('*') if p.is_file() and p.suffix in ('.hpp','.cpp','.py','.json','.txt','.defaults') and '__pycache__' not in p.parts]
 paths.extend(APP.parent/q for q in ['n30/main/font.inc','t096/protocol/role_announcement.c','t096/protocol/role_announcement.h','t096/protocol/gnss_diagnostics.c','t096/protocol/gnss_diagnostics.h','t096/protocol/group_auth.c','t096/protocol/group_auth.h','w12/main/g1_hmac.c'])
 return {str(p.relative_to(REPO)):sha(p) for p in sorted(paths)}
def dump(p,data):p.write_text(json.dumps(data,indent=2,sort_keys=True)+'\n')
def inspect(out):
 r=json.loads((out/'receipt.json').read_text());config=(out/'target/sdkconfig').read_text()
 required=['CONFIG_SPIRAM=y','CONFIG_SPIRAM_MODE_QUAD=y','CONFIG_SPIRAM_SPEED_80M=y','CONFIG_SPIRAM_USE_MALLOC=y','CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y']
 for k in required:
  if k not in config.splitlines():raise ValueError('Missing effective setting: '+k)
 for k in ['CONFIG_BT_ENABLED=y','CONFIG_ESP_WIFI_ENABLED=y','CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y','CONFIG_SPIRAM_MODE_OCT=y']:
  if k in config.splitlines():raise ValueError('Forbidden UI-preparation setting: '+k)
 if r['kind']!='UI_PREPARATION_ONLY' or r['flash_authorized'] or r['timing_receiver']:raise ValueError('Preparation scope changed')
 image=out/'target/ptob_corese_ui.bin'
 if not image.is_file() or not 0<image.stat().st_size<1024*1024:raise ValueError('Application exceeds UI preparation size ceiling')
 if r.get('application_sha256') and r['application_sha256']!=sha(image):raise ValueError('Application hash mismatch')
 return {'status':'OFFLINE_UI_BUILD_VERIFIED_NOT_INSTALLED','application_bytes':image.stat().st_size,'application_sha256':sha(image),'psram':'QUAD_80MHZ_8MIB_REQUIRED_AT_BOOT','timing_receiver':False,'hardware_access':False}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--idf',type=Path,required=True);p.add_argument('--dependencies',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 os.umask(0o077);out=a.out.resolve();sdk=a.idf.resolve();deps=a.dependencies.resolve()
 if out.exists() or REPO/'artifacts' not in out.parents:p.error('Fresh ignored artifacts/ output directory required')
 pins=json.loads((APP/'dependencies.lock.json').read_text())
 for name,path in [('esp-idf',sdk),('M5GFX',deps/'M5GFX'),('M5Unified',deps/'M5Unified')]:
  if git(path,'rev-parse','HEAD')!=pins[name]['revision']:p.error(name+' revision mismatch')
  if git(path,'status','--porcelain','--untracked-files=no'):p.error(name+' has modified tracked files')
 out.mkdir(parents=True);target=out/'target';snapshot=sources()
 r={'schema':1,'kind':'UI_PREPARATION_ONLY','version':'0.1.0','status':'INCOMPLETE','source_commit':git(REPO,'rev-parse','HEAD'),'source_dirty':bool(git(REPO,'status','--porcelain')),'sources':snapshot,'dependencies':pins,'flash_authorized':False,'timing_receiver':False,'hardware_access':False,'installed':False,'enrollment':'NONE','precision':'UNQUALIFIED','psram_mode':'QUAD','scope':'FIRST_UI_REVIEW_BEFORE_CLIENT_INTEGRATION'}
 dump(out/'receipt.json',r)
 env=os.environ.copy()
 for k in ['CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','EXTRA_COMPONENT_DIRS','SDKCONFIG_DEFAULTS']:env.pop(k,None)
 env.update(IDF_PATH=str(sdk),IDF_COMPONENT_MANAGER='0',PYTHONPYCACHEPREFIX=str(out/'pycache'),IDF_CCACHE_ENABLE='0')
 command=[sys.executable,str(sdk/'tools/idf.py'),'-C',str(APP),'-B',str(target),'-G','Unix Makefiles','-D','SDKCONFIG='+str(target/'sdkconfig'),'-D','CORESE_DEPENDENCIES='+str(deps),'-D','CCACHE_ENABLE=OFF','build']
 dump(out/'command.json',command)
 try:
  with (out/'build.log').open('w') as log:subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
  if snapshot!=sources():raise ValueError('Source changed during build')
  r.update(inspect(out));dump(out/'receipt.json',r);print(json.dumps(r|{'sources':'See receipt.json'},indent=2))
 except Exception as e:r['status']='FAILED';r['error']=str(e);dump(out/'receipt.json',r);raise
if __name__=='__main__':main()
