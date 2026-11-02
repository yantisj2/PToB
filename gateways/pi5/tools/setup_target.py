#!/usr/bin/env python3
"""Remote package preparation and native read-only smoke test.

Copyright (c) 2026 Jonathan Yantis. MIT.
"""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

PACKAGES = ['openssh-server', 'avahi-daemon', 'bluez', 'linuxptp', 'ethtool',
            'python3', 'python3-venv', 'python3-dev', 'python3-serial', 'python3-usb',
            'git', 'rsync', 'build-essential', 'cmake', 'ninja-build', 'pkg-config',
            'libbluetooth-dev', 'libsystemd-dev', 'libusb-1.0-0-dev', 'libudev-dev',
            'libelf-dev', 'libcap2-bin', 'strace', 'trace-cmd', 'tcpdump',
            'device-tree-compiler', 'gdb', 'gpiod', 'usbutils', 'curl',
            'ca-certificates', 'xz-utils', 'bc', 'bison', 'flex', 'tzdata-legacy']
PTP_UNITS = ['ptp4l', 'phc2sys', 'timemaster', 'ptp4l@', 'phc2sys@']


def run(command, timeout=300, check=True):
    result = subprocess.run(command, capture_output=True, text=True, timeout=timeout,
                            env=dict(os.environ, DEBIAN_FRONTEND='noninteractive'))
    if check and result.returncode:
        raise RuntimeError(shlex.join(command) + '\n' + result.stderr[-8000:] + result.stdout[-8000:])
    return {'returncode': result.returncode, 'stdout': result.stdout[-24000:], 'stderr': result.stderr[-4000:]}


def atomic_file(path, content, mode=0o600):
    if path.is_symlink():
        raise ValueError('refusing linked output: ' + str(path))
    with tempfile.NamedTemporaryFile('w', dir=path.parent, delete=False) as f:
        f.write(content)
        temp = f.name
    os.chmod(temp, mode)
    os.replace(temp, path)


def provision():
    if os.geteuid() != 0:
        raise ValueError('provision requires sudo through remote.py')
    for unit in PTP_UNITS + ['ptob-ptp4l', 'ptob-phc2sys', 'ptob-ptp-wifi']:
        status = run(['systemctl', 'is-active', unit], check=False)
        if status['stdout'].strip() in ('active', 'activating', 'reloading'):
            raise ValueError('PTP experiment is active; refusing baseline provisioning')
    missing = []
    for package in PACKAGES:
        status = run(['dpkg-query', '-W', '-f=${Status}', package], check=False)
        if status['stdout'] != 'install ok installed':
            missing.append(package)
    operations = []
    if missing:
        operations.append(run(['apt-get', 'update'], timeout=300))
        operations.append(run(['apt-get', 'install', '-y', '--no-install-recommends', *missing], timeout=600))
    operations.append(run(['systemctl', 'mask', *[u + '.service' for u in PTP_UNITS]]))
    packages = run(['dpkg-query', '-W', '-f=${binary:Package} ${Version} ${db:Status-Status}\n', *PACKAGES])
    audit = run(['dpkg', '--audit'])
    if audit['stdout'].strip():
        raise ValueError('package audit is not clean: ' + audit['stdout'])
    result = {'installed_missing': missing, 'packages': packages, 'package_audit': audit,
              'operations': operations, 'precision_input': 'unimplemented', 'ptp_services': 'masked'}
    folder = Path('/var/lib/ptob/pi5')
    folder.mkdir(mode=0o700, parents=True, exist_ok=True)
    atomic_file(folder / 'provision.json', json.dumps(result, indent=2) + '\n')
    return result


def smoke(params):
    if os.geteuid() == 0:
        raise ValueError('build the smoke test as the development user')
    parent = Path.home() / 'ptob'
    folder = parent / 'pi5'
    if parent.is_symlink() or folder.is_symlink():
        raise ValueError('refusing linked workspace')
    folder.mkdir(mode=0o700, parents=True, exist_ok=True)
    source = folder / 'clock_smoke.c'
    binary = folder / 'clock_smoke'
    if binary.is_symlink():
        raise ValueError('refusing linked smoke executable')
    atomic_file(source, params['c_source'])
    flags = shlex.split(run(['pkg-config', '--cflags', '--libs', 'bluez', 'libsystemd'])['stdout'])
    compile_result = run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                          str(source), '-o', str(binary), *flags])
    result = run(['sudo', '-n', str(binary)], timeout=20)
    return {'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
            'compiler': run(['cc', '--version']), 'compile': compile_result,
            'observations': json.loads(result['stdout']),
            'precision_qualification': 'not performed'}


if __name__ == '__main__':
    if 'PARAMS' not in globals():
        raise SystemExit('run through tools/remote.py for identity checks')
    print(json.dumps(provision() if PARAMS['action'] == 'provision' else smoke(PARAMS), indent=2))
