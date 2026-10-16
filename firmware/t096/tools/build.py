#!/usr/bin/env python3
"""Build the common RC7 application (GM or RC), without hardware access.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

APP = Path(__file__).resolve().parents[1]
REPO = APP.parents[1]
sys.path.insert(0, str(REPO/'registry'))
from registry import load as load_registry, header as registry_header

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()

def write(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')

def source_files():
    return sorted(p for p in APP.rglob('*') if p.is_file() and
                  '__pycache__' not in p.parts and p.suffix != '.pyc')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deps', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--enrollment', type=Path, required=True)
    parser.add_argument('--device-config', type=Path, required=True)
    parser.add_argument('--profile', choices=['GM', 'RC'], required=True)
    parser.add_argument('--registry', type=Path, default=REPO/'registry/example.json')
    parser.add_argument('--development', action='store_true')
    args = parser.parse_args()
    os.umask(0o077)
    deps, out = args.deps.resolve(), args.out.resolve()
    if out.exists() or REPO / 'artifacts' not in out.parents:
        parser.error('Use a fresh output directory below repository artifacts/')
    dirty = bool(git(REPO, 'status', '--porcelain', '--untracked-files=all'))
    if dirty and not args.development:
        parser.error('Commit reviewed public inputs before a release-candidate build')
    for path in [args.enrollment, args.device_config]:
        if subprocess.run(['git','-C',str(REPO),'check-ignore','--quiet',str(path.resolve())]).returncode:
            parser.error('Local enrollment/device inputs must be ignored by Git')
    lock = json.loads((APP/'dependencies.lock.json').read_text())
    def check_deps():
        for dependency in [lock['zephyr'], *lock['modules']]:
            p = deps/dependency['path']
            if git(p,'rev-parse','HEAD') != dependency['commit'] or git(p,'status','--porcelain'):
                raise ValueError('Dependency pin/cleanliness mismatch: '+dependency['path'])
    check_deps()
    sdk = deps/lock['sdk']['path']
    for name, key in [('sdk-minimal.tar.xz','minimal_sha256'),('sdk-arm.tar.xz','arm_sha256')]:
        if sha(deps/name) != lock['sdk'][key]:
            raise ValueError('SDK archive hash mismatch')
    if (sdk/'sdk_version').read_text().strip() != lock['sdk']['release']:
        raise ValueError('SDK version mismatch')
    enrollment = json.loads(args.enrollment.read_text())
    device = json.loads(args.device_config.read_text())
    registry = load_registry(args.registry)
    key = bytes.fromhex(enrollment['psk'])
    group = enrollment['group_id']; epoch = enrollment['key_epoch']
    if not (len(key)==32 and any(key) and re.fullmatch('[0-9a-f]{16}',group)
            and int(group,16) and type(epoch) is int and 0 < epoch < 2**32):
        raise ValueError('Invalid local group enrollment')
    identity,label,battery = device['device_id'],device['label'],device['battery']
    if not (re.fullmatch('[0-9a-f]{16}',identity) and int(identity,16) and
            re.fullmatch('[A-Za-z0-9_-]{5}',label) and type(battery) is int and battery in (-1,0,1)
            and device['board']=='heltec_t096_ht_n5262g'):
        raise ValueError('Invalid explicit supported board/device configuration')
    out.mkdir(parents=True); (out/'target').mkdir()
    source = {str(p.relative_to(APP)):sha(p) for p in source_files()}
    source_hash = hashlib.sha256(json.dumps(source,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    version=re.search(r'^project\(ptob_t096 VERSION ([0-9]+\.[0-9]+\.[0-9]+)\)',
                      (APP/'CMakeLists.txt').read_text(),re.M)
    if not version:raise ValueError('Missing canonical CMake application version')
    receipt = {'schema':1,'artifact_class':'PTOB_T096_RC7','version':version[1],
               'status':'INCOMPLETE','public_commit':git(REPO,'rev-parse','HEAD'),
               'child_commit':git(REPO,'rev-parse','HEAD'),'source_dirty':dirty,'sources':source,'source_sha256':source_hash,
               'profile':args.profile,'usb_source_stream':True,'dependencies':lock,
               'installed':False,'precision':'UNQUALIFIED','hardware_tests':[],
               'enrollment_mode':'LOCAL_BUILD_TIME_P1_NOT_IMPLEMENTED',
               'registry_sha256':sha(args.registry),
               'registry_generator_sha256':sha(REPO/'registry/registry.py'),
               'device_config_sha256':sha(args.device_config)}
    write(out/'receipt.json',receipt)
    for name in source:
        dst=out/'source'/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(APP/name,dst)
    (out/'target/ptob_registry.h').write_text(registry_header(registry))
    shutil.copyfile(args.registry,out/'registry.json')
    shutil.copyfile(REPO/'registry/registry.py',out/'registry_generator.py')
    secret=out/'r2_enrollment.h'
    secret.write_text('/* device-config-sha256: '+sha(args.device_config)+'; registry-sha256: '+sha(args.registry)+' */\n#include "ble_service.h"\nstatic const struct r2_enrollment r2_enrollment={'
        '.group={.psk={'+','.join(map(str,key))+'},.id=UINT64_C(0x'+group+'),.epoch='+str(epoch)+'},'
        '.rc_dynamic=true,.rc7_boot_rc='+('true' if args.profile=='RC' else 'false')+','
        '.local_capabilities=7,.map_bound_us=40000};\n')
    (out/'target/ptob_device.h').write_text('#define PTOB_DEVICE_ID "'+identity+'"\n'
        '#define PTOB_DEVICE_LABEL "'+label+'"\n#define PTOB_DEVICE_BATTERY '+str(battery)+'\n')
    env=os.environ.copy()
    for name in ['CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','ZEPHYR_EXTRA_MODULES','EXTRA_CONF_FILE','DTC_OVERLAY_FILE']:
        env.pop(name,None)
    env.update(ZEPHYR_BASE=str(deps/'zephyr'),ZEPHYR_SDK_INSTALL_DIR=str(sdk),ZEPHYR_TOOLCHAIN_VARIANT='zephyr',
               PYTHONPYCACHEPREFIX=str(out/'pycache'),XDG_CACHE_HOME=str(out/'cache'),
               PATH=str(deps/'venv/bin')+os.pathsep+env['PATH'])
    def run(command, name):
        with (out/name).open('w') as f:
            subprocess.run(command,env=env,cwd=REPO,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=600)
    command=['cmake','-GNinja','-S',str(APP),'-B',str(out/'target'),'-DBOARD=t096_r2',
             '-DUSER_CACHE_DIR='+str(out/'cache'),'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DR2_ENROLLMENT_HEADER='+str(secret),
             '-DR2_SOURCE_SHA='+source_hash,'-DR2_CHILD_COMMIT='+receipt['public_commit'],
             '-DR2_SOURCE_DIRTY='+str(int(dirty)),
             '-DZEPHYR_MODULES='+';'.join(str(deps/p['path']) for p in lock['modules']),
             '-DEXTRA_CONF_FILE='+';'.join(str(APP/p) for p in ['rc_dynamic.conf','rc_unicast.conf','watchdog.conf','rc_hf_synth.conf'])]
    try:
        run(command,'configure.log');run(['ninja','-C',str(out/'target')],'build.log')
        check_deps()
        if source != {str(p.relative_to(APP)):sha(p) for p in source_files()}:
            raise ValueError('Source changed during build')
        target=out/'target/zephyr'
        binding=hashlib.sha256((source_hash+':'+sha(secret)+':'+sha(target/'.config')).encode()).hexdigest()
        if '#define R2_WATCHDOG_BINDING "'+binding+'"' not in (out/'target/build_identity.h').read_text():
            raise ValueError('Watchdog binding mismatch')
        run([sys.executable,str(APP/'tools/uf2.py'),'pack',str(target/'zephyr.bin'),str(out/'t096.uf2'),'--address','0x26000'],'uf2.log')
        receipt['watchdog_binding_sha256']=binding
        receipt['compiled_sources']={}
        for c in json.loads((out/'target/compile_commands.json').read_text()):
            p=Path(c['file']).resolve()
            for prefix,base in [('deps',deps),('app',APP),('generated',out)]:
                if p.is_relative_to(base):
                    receipt['compiled_sources'][prefix+'/'+str(p.relative_to(base))]=sha(p);break
            else:raise ValueError('Unexpected compiled source')
        receipt['artifacts']={str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file()
                              and (p.suffix in ['.elf','.bin','.hex','.map','.uf2','.su'] or p.name in ['.config','zephyr.dts','build_identity.h','ptob_device.h','ptob_registry.h','registry.json','registry_generator.py','r2_enrollment.h','compile_commands.json'])}
        receipt['status']='DEVELOPMENT_NOT_FLASHABLE' if dirty else 'BUILD_COMPLETE_PHYSICAL_UNKNOWN'
        write(out/'receipt.json',receipt)
        from inspect_build import inspect
        write(out/'inspection.json',inspect(out,allow_development=args.development))
        print('Build and portable layout checks passed:',out)
    except BaseException:
        receipt['status']='FAILED';write(out/'receipt.json',receipt);raise

if __name__=='__main__':
    main()
