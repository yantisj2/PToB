#!/usr/bin/env python3
"""Bind a reserved N30 name to an operator-verified MAC in the ignored shared map.
No serial, USB, discovery, enrollment key generation, or hardware access.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,fcntl,hashlib,json,os,re,subprocess,sys,tempfile
from pathlib import Path
REPO=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(REPO/'registry'))
from registry import load
ALIASES=('N3-1','N3-2')
def prepare(data,alias,mac,rotation='cw'):
 if alias not in ALIASES or rotation not in ('cw','ccw'):raise ValueError('Reserved N30 alias and explicit orientation required')
 if not isinstance(mac,str) or not re.fullmatch('[0-9a-f]{2}(:[0-9a-f]{2}){5}',mac) or not int(mac.replace(':',''),16) or int(mac[:2],16)&1:raise ValueError('Canonical nonzero unicast factory MAC required')
 identity='0000'+mac.replace(':','');devices=data['devices']
 for key,value in devices.items():
  if value['alias']==alias and key!=identity:raise ValueError('Alias already belongs to a different factory identity')
 prior=devices.get(identity)
 if prior is not None and (prior['target']!='n30' or prior['alias']!=alias):raise ValueError('Factory identity already belongs to another target/name')
 # Preserve all unrelated entries, UUIDs and existing matching N30 metadata.
 updated=json.loads(json.dumps(data));updated['devices'][identity]=prior or {'alias':alias,'target':'n30'}
 device={'board':'meshnology_n30_heltec_v3','factory_mac':mac,'device_id':identity,'label':alias,'legacy_wire_id':3+ALIASES.index(alias),'display_rotation':rotation}
 return updated,device

def _transaction(registry,out,alias,mac,rotation='cw'):
 registry=Path(registry).resolve();out=Path(out).resolve()
 for path in (registry,out):
  if subprocess.run(['git','-C',str(REPO),'check-ignore','--quiet',str(path)]).returncode:raise ValueError('Registry and output must be ignored local paths')
 if out.exists():raise ValueError('Use a fresh onboarding output directory')
 before=registry.read_bytes();data=load(registry);updated,device=prepare(data,alias,mac,rotation)
 encoded=(json.dumps(updated,indent=2,sort_keys=True)+'\n').encode()
 os.umask(0o077);out.mkdir(parents=True)
 # Validate with the same shared parser before replacing the operator's map.
 candidate=out/'registry-candidate.json';candidate.write_bytes(encoded);load(candidate)
 if registry.read_bytes()!=before:raise ValueError('Shared map changed during onboarding; no replacement made')
 (out/'device.json').write_text(json.dumps(device,indent=2)+'\n')
 with tempfile.NamedTemporaryFile(dir=registry.parent,prefix='.n30-map-',delete=False) as f:
  temp=Path(f.name);f.write(encoded);f.flush();os.fsync(f.fileno())
 try:os.replace(temp,registry)
 finally:temp.unlink(missing_ok=True)
 candidate.unlink()
 receipt={'schema':1,'alias':alias,'device_id':device['device_id'],'target':'n30','registry':str(registry),'registry_before_sha256':hashlib.sha256(before).hexdigest(),'registry_after_sha256':hashlib.sha256(encoded).hexdigest(),'factory_identity_evidence':'OPERATOR_SUPPLIED_VERIFY_ON_ARRIVAL','hardware_access':False,'hardware_validation':'NOT_PERFORMED','flashing_authorized':False}
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');return receipt

def onboard(registry,out,alias,mac,rotation='cw'):
 registry=Path(registry).resolve()
 if subprocess.run(['git','-C',str(REPO),'check-ignore','--quiet',str(registry)]).returncode:raise ValueError('Registry must be an ignored local path')
 os.umask(0o077)
 with registry.with_name('.'+registry.name+'.n30.lock').open('a') as lock:
  try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
  except BlockingIOError:raise ValueError('Another N30 onboarding transaction owns this shared map') from None
  return _transaction(registry,out,alias,mac,rotation)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--alias',choices=ALIASES,required=True);p.add_argument('--factory-mac',required=True);p.add_argument('--rotation',choices=['cw','ccw'],default='cw');p.add_argument('--registry',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 print(json.dumps(onboard(a.registry,a.out,a.alias,a.factory_mac,a.rotation),indent=2))
if __name__=='__main__':main()
