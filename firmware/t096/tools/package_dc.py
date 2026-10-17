#!/usr/bin/env python3
"""Package a clean, inspected exact-device DC candidate; never opens hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,shutil,zipfile
from pathlib import Path
from inspect_build import inspect
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def package(build,out):
    build=build.resolve();out=out.resolve()
    if out.exists() or REPO/'artifacts' not in out.parents:
        raise ValueError('Use a fresh output directory below repository artifacts/')
    result=inspect(build)
    receipt=json.loads((build/'receipt.json').read_text())
    if receipt['profile']!='DC' or receipt['artifact_class']!='PTOB_T096_DC1':
        raise ValueError('Only a separately built DC candidate can be packaged')
    header=(build/'target/ptob_device.h').read_text()
    identity=re.search(r'^#define PTOB_DEVICE_ID "([0-9a-f]{16})"$',header,re.M)
    label=re.search(r'^#define PTOB_DEVICE_LABEL "([A-Za-z0-9_-]{5})"$',header,re.M)
    if not identity or not label:raise ValueError('Missing exact device binding')
    name='t096-dc-'+receipt['version']+'-'+label[1]
    device=json.loads((build/'device.json').read_text())
    if sha(build/'device.json')!=receipt['device_config_sha256'] or device['device_id']!=identity[1] or device['label']!=label[1]:
        raise ValueError('Device input/header binding mismatch')
    archive=out.parent/(name+'.zip')
    if archive.exists():raise ValueError('Refusing to replace an existing archive')
    files={name+'.uf2':build/'t096.uf2',name+'.bin':build/'target/zephyr/zephyr.bin',
           'build-receipt.json':build/'receipt.json','target-device.json':build/'device.json'}
    for relative in receipt['sources']:
        files['source/firmware/t096/'+relative]=build/'source'/relative
    # Freeze documentation/registry/terms from the same committed checkout.
    # A newer working tree must not silently supply an older image's package.
    import subprocess
    commit=subprocess.check_output(['git','-C',str(REPO),'rev-parse','HEAD'],text=True).strip()
    dirty=subprocess.check_output(['git','-C',str(REPO),'status','--porcelain','--untracked-files=all'],text=True).strip()
    if commit!=receipt['public_commit'] or dirty:raise ValueError('Package from the same clean source commit as the build')
    out.mkdir(parents=True)
    for relative in ['LICENSE','LICENSING.md','LICENSES/Apache-2.0.txt','LICENSES/TinyCrypt-BSD-3-Clause.txt',
                     'registry/registry.py','registry/example.json','docs/t096/direct-connect.md','docs/t096/build.md']:
        files['source/'+relative]=REPO/relative
    for relative,path in files.items():
        target=out/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,target)
    (out/'inspection.json').write_text(json.dumps(result,indent=2)+'\n')
    (out/'README.txt').write_text(
        'T096 Direct Connect '+receipt['version']+' / '+label[1]+'\n'
        'LOCAL EXACT-DEVICE CANDIDATE. Hardware timing/durability NOT TESTED.\n'
        'Fixed DC image: GNSS/PPS -> USB -> macOS; Bluetooth disabled, zero BLE relay hops.\n'
        'No group enrollment or PSK. Device identity remains bound to target-device.json.\n'
        'App-only UF2 begins at 0x26000; no protected-region or bootloader payload.\n'
        'Use the reviewed identity/ownership/recovery/update guards; this is not a flasher.\n'
        'Preserve a current verified rollback and protected-region proof before installation.\n'
        'UTC association and precision bounds remain unknown. USB RMS is device SOF cadence.\n'
        'See source/docs/t096/direct-connect.md for scope, build and outstanding website gates.\n')
    hashes={str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file()}
    manifest={'schema':1,'profile':'DC','version':receipt['version'],'source_commit':receipt['public_commit'],
              'source_sha256':receipt['source_sha256'],'target':device,'ble_enabled':False,
              'profile_toggleable':False,'relay_hops':0,'group_enrollment':False,
              'hardware_validation':'NOT_TESTED','precision':'UNQUALIFIED','website_release':False,
              'installation_performed':False,'files':hashes}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
    (out/'SHA256SUMS').write_text(''.join(sha(p)+'  '+str(p.relative_to(out))+'\n' for p in sorted(out.rglob('*')) if p.is_file()))
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(out.rglob('*')):
            if p.is_file():z.write(p,name+'/'+str(p.relative_to(out)))
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:raise ValueError('Archive CRC failure')
    print(json.dumps({'archive':str(archive),'sha256':sha(archive),'manifest':str(out/'manifest.json')},indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();os.umask(0o077);package(a.build,a.out)
