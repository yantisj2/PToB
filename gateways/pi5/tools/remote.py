#!/usr/bin/env python3
"""Operate an explicitly enrolled Pi 5 over pinned-key SSH.

Copyright (c) 2026 Jonathan Yantis. MIT.
"""
import argparse
import datetime
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import shlex
import socket
import subprocess

TARGET = Path(__file__).resolve().parents[1]
ROOT = TARGET.parents[1]
GUARD = r"""import hashlib,json,os,pathlib,platform,sys
p=json.load(sys.stdin)
def read(f):return pathlib.Path(f).read_text().strip('\x00\n ')
if not read('/proc/device-tree/model').startswith('Raspberry Pi 5 Model B'):raise SystemExit('wrong Pi model')
if platform.machine()!='aarch64':raise SystemExit('wrong architecture')
if read('/proc/device-tree/serial-number')!=p['serial']:raise SystemExit('exact factory serial mismatch')
if read('/opt/ptob/pi5/IMAGE_ID')!=p['image_id']:raise SystemExit('exact boot-image mismatch')
os.umask(0o077)
exec(compile(p['source'],'<ptob-remote>','exec'),{'__name__':'__main__','__file__':'<ptob-remote>','PARAMS':p.get('params',{}),'PTOB_SOURCE_SHA256':hashlib.sha256(p['source'].encode()).hexdigest()})
"""


def validate_profile(profile):
    if not re.fullmatch(r'[a-z][a-z0-9_-]*@[A-Za-z0-9][A-Za-z0-9.:-]*', profile['host']):
        raise ValueError('connection requires explicit user@host')
    if not re.fullmatch(r'[0-9a-f]{16}', profile['serial']) or int(profile['serial'], 16) == 0:
        raise ValueError('connection requires a nonzero factory serial')
    if not re.fullmatch(r'ptob-pi5-[a-f0-9-]+', profile['image_id']):
        raise ValueError('connection requires an enrolled image instance ID')
    if not re.fullmatch(r'[a-zA-Z0-9.-]+', profile['host_key_alias']):
        raise ValueError('connection requires a pinned host-key alias')
    for name in ('identity_file', 'known_hosts'):
        path = Path(profile[name]).expanduser().resolve()
        if path.is_relative_to(ROOT):
            raise ValueError('SSH access files must be outside the repository')
        if not path.is_file() or path.stat().st_mode & 0o077:
            raise ValueError('SSH access files must exist and have owner-only permissions')


def execute(profile, source, params=None, root=False, timeout=90):
    validate_profile(profile)
    ssh = ['ssh', '-i', str(Path(profile['identity_file']).expanduser()),
           '-o', 'IdentitiesOnly=yes', '-o', 'IdentityAgent=none', '-o', 'BatchMode=yes',
           '-o', 'ConnectTimeout=10', '-o', 'StrictHostKeyChecking=yes',
           '-o', 'UserKnownHostsFile=' + profile['known_hosts'],
           '-o', 'HostKeyAlias=' + profile['host_key_alias'],
           '-o', 'ForwardAgent=no', '-o', 'ServerAliveInterval=15',
           '-o', 'ServerAliveCountMax=2', profile['host']]
    command = (['sudo', '-n'] if root else []) + ['python3', '-c', GUARD]
    payload = dict(serial=profile['serial'], image_id=profile['image_id'], source=source, params=params or {})
    result = subprocess.run(ssh + [shlex.join(command)], input=json.dumps(payload).encode(),
                            capture_output=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors='replace')[-12000:] or 'remote operation failed')
    return json.loads(result.stdout)


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('inventory', 'provision', 'smoke', 'timing-install', 'timing-status',
                                          'dashboard-install', 'tailscale-install', 'tailscale-status', 'jitter-install',
                                          'clock-install'))
    parser.add_argument('--connection', type=Path, default=Path.home() / '.config/ptob/pi5.json')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--wifi-peer', help='PTP Wi-Fi unicast test peer, or auto for this host')
    parser.add_argument('--mhat-build', type=Path, help='retained, actually installed HAT build for telemetry binding')
    parser.add_argument('--module', type=Path, default=Path.home()/'.config/ptob/mhat.json')
    args = parser.parse_args()
    profile = json.loads(args.connection.expanduser().read_text())
    validate_profile(profile)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    out = (args.out or ROOT / 'artifacts/pi5/reports' / (args.action + '-' + stamp + '.json')).resolve()
    if not out.is_relative_to((ROOT / 'artifacts').resolve()) or out.exists():
        raise ValueError('choose a new report path inside ignored artifacts/')
    if args.action == 'inventory':
        source = (TARGET / 'tools/probe.py').read_text()
        report = execute(profile, source, root=True, timeout=220)
    elif args.action.startswith('tailscale-'):
        source = (TARGET / 'tools/tailscale_setup.py').read_text()
        report = execute(profile, source, {'action':args.action}, root=True, timeout=900)
    elif args.action == 'jitter-install':
        source = (TARGET/'tools/pps_tune.py').read_text()
        params = dict(binding={k:profile[k] for k in ('serial','image_id')},
                      files=dict(script=source, unit=(TARGET/'config/ptob-pps-tuning.service').read_text()))
        report = execute(profile, source, params, root=True)
    elif args.action == 'dashboard-install':
        if not args.mhat_build:
            raise ValueError('--mhat-build must identify the actually installed receiver image')
        build = args.mhat_build.resolve()
        if not build.is_relative_to(ROOT/'artifacts'):
            raise ValueError('use a private retained build')
        receipt = json.loads((build/'receipt.json').read_text())
        module = json.loads(args.module.expanduser().read_text())
        device = json.loads((build/'device.json').read_text())
        if (device['particle_id'] != module['device_id'] or module['platform'] != 'b5som' or
                receipt['artifact_class'] != 'PTOB_MHAT_RC' or
                receipt['application_sha256'] != hashlib.sha256((build/'mhat.bin').read_bytes()).hexdigest()):
            raise ValueError('HAT build/factory binding mismatch')
        source = (TARGET/'tools/dashboard_setup.py').read_text()
        binding = dict(device_id=device['device_id'], particle_id=device['particle_id'],
                       allowed_sources=[receipt['source_sha256']], uart='/dev/ttyAMA0')
        params = dict(binding=binding, files={k:(TARGET/'dashboard'/k).read_text() for k in
                      ('collector.py','history.py','server.py','static/index.html','static/app.js','static/charts.js','static/style.css')},
                      units={k:(TARGET/'config'/k).read_text() for k in
                      ('ptob-dashboard.service','ptob-telemetry.service')})
        report = execute(profile, source, params, root=True, timeout=120)
    elif args.action.startswith('timing-') or args.action == 'clock-install':
        source = (TARGET / 'tools/timing_setup.py').read_text()
        params = {'action': args.action}
        if args.action in ('timing-install', 'clock-install'):
            params['clock_script'] = (TARGET/'tools/clock_chain.py').read_text()
        if args.action == 'clock-install':
            params['files'] = {name: (TARGET/'config'/name).read_text() for name in
                              ('ptob-phc2sys.service', 'ptob-ptp4l.service', 'ptp4l.conf')}
        if args.action == 'timing-install':
            params['files'] = {p.name: p.read_text() for p in (TARGET / 'config').iterdir() if p.is_file()}
            if args.wifi_peer:
                peer = args.wifi_peer
                if peer == 'auto':
                    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
                        route.connect((profile['host'].split('@')[1], 123))
                        peer = route.getsockname()[0]
                peer = str(ipaddress.IPv4Address(peer))
                params['files']['ptpd-wifi.conf'] = params['files']['ptpd-wifi.conf'].replace(
                    'ip_mode = multicast', 'ip_mode = unicast\nunicast_destinations = ' + peer)
        report = execute(profile, source, params, root=True, timeout=900)
    else:
        source = (TARGET / 'tools/setup_target.py').read_text()
        params = {'action': args.action}
        if args.action == 'smoke':
            params['c_source'] = (TARGET / 'tools/clock_smoke.c').read_text()
        report = execute(profile, source, params, root=args.action == 'provision', timeout=900)
    receipt = {'schema': 1, 'action': args.action, 'utc': stamp,
               'device_serial': profile['serial'], 'image_id': profile['image_id'],
               'executed_source_sha256': hashlib.sha256(source.encode()).hexdigest(), 'result': report}
    out.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    with out.open('x') as f:
        json.dump(receipt, f, indent=2)
        f.write('\n')
    summary = {'action': args.action, 'report': str(out)}
    if args.action == 'provision':
        summary['installed_missing'] = report['installed_missing']
        summary['ptp_services'] = report['ptp_services']
    elif args.action == 'smoke':
        summary['observations'] = report['observations']
    elif args.action == 'inventory':
        summary.update(model=report['model'], kernel=report['kernel'])
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
