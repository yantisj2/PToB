#!/usr/bin/env python3
"""Build an exact-device Particle-bootloader-compatible RC leaf; no hardware.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zlib

APP = Path(__file__).resolve().parents[1]
ROOT = APP.parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from firmware_artifacts import run_builder


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


def pack_image(raw):
    data = bytearray(raw)
    if len(data) < 0x218 or any(data[0x200:0x218]):
        raise ValueError('module-info reservation is absent or occupied')
    sp, pc = struct.unpack_from('<II', data)
    if sp & 0x2ffc0000 != 0x20000000 or not pc & 1 or not 0x30000 <= (pc & ~1) < 0xb4000:
        raise ValueError('invalid boot vector')
    # Particle bootloader validates start/end/platform and IEEE CRC32 (BE).
    # Monolithic function 3 uses its existing system start, preserving MBR,
    # S140, user partition, bootloader, UICR and external flash.
    struct.pack_into('<IIBBHHBB8s', data, 0x200, 0x30000, 0x30000 + len(data),
                     0, 0, 1, 25, 3, 0, bytes(8))
    data += struct.pack('>I', zlib.crc32(data) & 0xffffffff)
    if 0x30000 + len(data) > 0xb4000:
        raise ValueError('image exceeds system partition')
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deps', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--module', type=Path, required=True, help='verified private module profile')
    parser.add_argument('--enrollment', type=Path, required=True)
    args = parser.parse_args()
    os.umask(0o077)
    out, deps = args.out.resolve(), args.deps.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'artifacts'):
        raise ValueError('use a new ignored artifacts/ output')
    profile = json.loads(args.module.read_text())
    particle_id = profile['device_id']
    if not re.fullmatch(r'e00fce68[0-9a-f]{16}', particle_id) or profile['platform'] != 'b5som':
        raise ValueError('verified B5 SoM factory binding required')
    ficr0, ficr1 = struct.unpack('<II', bytes.fromhex(particle_id)[4:])
    device_id = f'{ficr1:08x}{ficr0:08x}'
    enrollment = json.loads(args.enrollment.read_text())
    key = bytes.fromhex(enrollment['psk'])
    if len(key) != 32 or not any(key) or not re.fullmatch('[0-9a-f]{16}', enrollment['group_id']) or not int(enrollment['group_id'], 16) or not 0 < enrollment['key_epoch'] < 2**32:
        raise ValueError('invalid private trust enrollment')
    lock = json.loads((ROOT / 'firmware/t096/dependencies.lock.json').read_text())
    for item in [lock['zephyr'], *lock['modules']]:
        repo = deps / item['path']
        if subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip() != item['commit'] or subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']):
            raise ValueError('dependency pin/cleanliness mismatch: ' + item['path'])
    for name, field in [('sdk-minimal.tar.xz', 'minimal_sha256'), ('sdk-arm.tar.xz', 'arm_sha256')]:
        if digest(deps / name) != lock['sdk'][field]:
            raise ValueError('SDK archive mismatch')
    out.mkdir(parents=True)
    # Selected public receiver sources and their include/controller closure.
    selected = [p for p in APP.rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    t096 = ROOT / 'firmware/t096'
    for folder in ('include', 'protocol', 'cmake', 'tools'):
        selected += [p for p in (t096 / folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix in ('.h', '.c', '.cmake', '.py', '.patch', '.inc', '.json')]
    for name in ('core.c', 'rc_dynamic_service.c', 'rc_dynamic_transport.inc', 'rc_boundary.c', 'rc_slots.c', 'rc_unicast.c',
                 'radio_capture.c', 'radio_audit.c', 'initiator_diag.c', 'timing_events.c'):
        selected.append(t096 / 'src' / name)
    sources = {str(p.relative_to(ROOT)): digest(p) for p in sorted(set(selected))}
    for name in sources:
        dest = out / 'source' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, dest)
    source_hash = hashlib.sha256(json.dumps(sources, sort_keys=True).encode()).hexdigest()
    header = out / 'mhat_private.h'
    header.write_text('#include <stdint.h>\n' + f'#define MHAT_FICR0 0x{ficr0:08x}u\n#define MHAT_FICR1 0x{ficr1:08x}u\n'
                      + f'#define MHAT_PARTICLE_ID "{particle_id}"\n#define MHAT_SOURCE_SHA "{source_hash}"\n'
                      + f'#define MHAT_KEY_EPOCH {enrollment["key_epoch"]}u\n'
                      + '#define MHAT_PSK {' + ','.join(f'0x{b:02x}' for b in key) + '}\n')
    write(out / 'device.json', {'device_id': device_id, 'particle_id': particle_id, 'board': 'mhat_b504e'})
    version = re.search(r'project\(ptob_mhat VERSION ([0-9.]+)\)', (APP/'CMakeLists.txt').read_text()).group(1)
    receipt = {'schema': 1, 'artifact_class': 'PTOB_MHAT_RC', 'version': version, 'profile': 'RC',
               'status': 'INCOMPLETE', 'source_dirty': True, 'sources': sources, 'source_sha256': source_hash,
               'dependencies': lock, 'device_id': device_id, 'device_config_sha256': digest(out / 'device.json'),
               'enrollment_sha256': digest(args.enrollment), 'installed': False, 'precision': 'UNQUALIFIED',
               'application': 'mhat.bin', 'artifacts': {'device.json': digest(out / 'device.json'), 'mhat_private.h': digest(header)}}
    write(out / 'receipt.json', receipt)
    env = os.environ.copy()
    for name in ('CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'ZEPHYR_EXTRA_MODULES', 'EXTRA_CONF_FILE', 'DTC_OVERLAY_FILE'):
        env.pop(name, None)
    env.update(ZEPHYR_BASE=str(deps / 'zephyr'), ZEPHYR_SDK_INSTALL_DIR=str(deps / lock['sdk']['path']),
               ZEPHYR_TOOLCHAIN_VARIANT='zephyr', PYTHONPYCACHEPREFIX=str(out / 'pycache'),
               XDG_CACHE_HOME=str(out / 'cache'), PATH=str(deps / 'venv/bin') + os.pathsep + env['PATH'])
    try:
        commands = [('configure.log', ['cmake', '-GNinja', '-S', str(out / 'source/firmware/mhat'), '-B', str(out / 'target'),
                    '-DBOARD=mhat_b504e', '-DUSER_CACHE_DIR=' + str(out / 'cache'), '-DMHAT_PRIVATE_HEADER=' + str(header),
                    '-DZEPHYR_MODULES=' + ';'.join(str(deps / p['path']) for p in lock['modules']),
                    '-DCONFIG_USB_DEVICE_SN="' + particle_id + '"']),
                    ('build.log', ['cmake', '--build', str(out / 'target'), '-j', '6'])]
        for name, command in commands:
            with (out / name).open('w') as log:
                subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
        stack_path = out / 'target/CMakeFiles/app.dir/src/main.c.su'
        rows = [line.split('\t') for line in stack_path.read_text().splitlines()]
        frames = [int(row[1]) for row in rows if row[0].endswith(':main')]
        if len(frames) != 1 or frames[0] > 2048:
            raise ValueError('main frame must leave at least 6 KiB for startup call depth')
        receipt['main_static_frame_bytes'] = frames[0]
        receipt['artifacts'][str(stack_path.relative_to(out))] = digest(stack_path)
        (out / 'mhat.bin').write_bytes(pack_image((out / 'target/zephyr/zephyr.bin').read_bytes()))
        for rel in ('mhat.bin', 'target/zephyr/zephyr.elf', 'target/zephyr/zephyr.bin', 'target/zephyr/zephyr.map',
                    'target/zephyr/.config', 'target/zephyr/zephyr.dts'):
            receipt['artifacts'][rel] = digest(out / rel)
        receipt.update(status='BUILD_COMPLETE_PHYSICAL_UNKNOWN', application_sha256=digest(out / 'mhat.bin'),
                       application_bytes=(out / 'mhat.bin').stat().st_size)
    except BaseException:
        receipt['status'] = 'FAILED'
        raise
    finally:
        write(out / 'receipt.json', receipt)
    print(json.dumps({'output': str(out), 'bytes': receipt['application_bytes'], 'status': receipt['status']}))


if __name__ == '__main__':
    run_builder(main, ROOT)
