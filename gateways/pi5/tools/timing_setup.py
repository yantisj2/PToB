#!/usr/bin/env python3
"""Exact-device remote timing setup, invoked by remote.py.

Copyright (c) 2026 Jonathan Yantis. MIT.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess


def clock_install(params):
    """Update only the owned Ethernet clock chain; preserve chrony/UART/Wi-Fi."""
    if os.geteuid():
        raise ValueError('root required')
    owners = []
    allowed = {'chronyd': 'chrony.service', 'ptp4l': 'ptob-ptp4l.service',
               'phc2sys': 'ptob-phc2sys.service'}
    for proc in Path('/proc').glob('[0-9]*'):
        try:
            name = (proc/'comm').read_text().strip()
            if name not in ('chronyd', 'ptp4l', 'phc2sys', 'ts2phc', 'timemaster', 'ntpd', 'systemd-timesyn'):
                continue
            group = (proc/'cgroup').read_text()
            if name not in allowed or not any(line.endswith('/'+allowed[name]) for line in group.splitlines()):
                raise ValueError('competing clock owner: ' + name)
            owners.append(dict(process=name, unit=allowed[name]))
        except FileNotFoundError:
            continue
    saved = Path('/var/lib/ptob/pi5/clock-chain-before')
    saved.mkdir(parents=True, mode=0o700, exist_ok=True)
    mapping = {'ptob-phc2sys.service': Path('/etc/systemd/system/ptob-phc2sys.service'),
               'ptob-ptp4l.service': Path('/etc/systemd/system/ptob-ptp4l.service'),
               'ptp4l.conf': Path('/etc/ptob/pi5/ptp4l.conf')}
    files = dict(params['files'])
    files['clock_chain.py'] = params['clock_script']
    mapping['clock_chain.py'] = Path('/opt/ptob/pi5/clock_chain.py')
    # Preflight all destinations before stopping the old chain.
    for name, path in mapping.items():
        if path.is_symlink():
            raise ValueError('linked clock-chain output')
        backup = saved/name
        if path.exists() and not backup.exists():
            backup.write_bytes(path.read_bytes()); backup.chmod(0o600)
    operations = [run(['systemctl', 'stop', 'ptob-ptp4l', 'ptob-phc2sys'])]
    try:
        for name, path in mapping.items():
            path.write_text(files[name]); path.chmod(0o644)
        operations.append(run(['systemd-analyze', 'verify', str(mapping['ptob-phc2sys.service']),
                               str(mapping['ptob-ptp4l.service'])]))
        operations.append(run(['python3', str(mapping['clock_chain.py']), 'observe']))
        operations.append(run(['systemctl', 'daemon-reload']))
        operations.append(run(['systemctl', 'enable', '--now', 'ptob-phc2sys', 'ptob-ptp4l']))
    except Exception:
        # Keep Ethernet output withdrawn on a failed deployment; retain backup/error.
        run(['systemctl', 'stop', 'ptob-ptp4l', 'ptob-phc2sys'], check=False)
        raise
    return dict(operations=operations, owners=owners,
                files={k: hashlib.sha256(v.encode()).hexdigest() for k, v in files.items()},
                output_accuracy_measured=False)


def run(command, check=True, timeout=120):
    p = subprocess.run(command, capture_output=True, text=True, timeout=timeout,
                       env=dict(os.environ, DEBIAN_FRONTEND='noninteractive'))
    result = {'returncode': p.returncode, 'stdout': p.stdout[-24000:], 'stderr': p.stderr[-8000:]}
    if check and p.returncode:
        raise RuntimeError(str(command) + '\n' + json.dumps(result))
    return result


def install(files):
    if os.geteuid():
        raise ValueError('root required')
    saved = Path('/var/lib/ptob/pi5/timing-before')
    saved.mkdir(parents=True, mode=0o700, exist_ok=True)
    commands = [run(['systemctl', 'mask', 'ptpd.service']), run(['apt-get', 'update'], timeout=300),
                run(['apt-get', 'install', '-y', '--no-install-recommends',
                     'chrony', 'pps-tools', 'ptpd', 'tzdata-legacy'], timeout=600)]
    # Debian 13 moved leap-aware right/* zones out of the base tzdata package.
    # Do not install a chrony leapsectz reference to a nonexistent database.
    if not Path('/usr/share/zoneinfo/right/UTC').is_file():
        raise RuntimeError('tzdata-legacy did not provide the required right/UTC leap timezone')
    mapping = {'chrony.conf': '/etc/chrony/chrony.conf',
               'ptp4l.conf': '/etc/ptob/pi5/ptp4l.conf',
               'ptpd-wifi.conf': '/etc/ptob/pi5/ptpd-wifi.conf',
               'ptob-ptp-wifi.service': '/etc/systemd/system/ptob-ptp-wifi.service',
               'ptob-ptp4l.service': '/etc/systemd/system/ptob-ptp4l.service',
               'ptob-phc2sys.service': '/etc/systemd/system/ptob-phc2sys.service',
               'ptob-pps.dts': '/etc/ptob/pi5/ptob-pps.dts'}
    for name, destination in mapping.items():
        p = Path(destination)
        if p.is_symlink():
            raise ValueError('refusing linked config: ' + destination)
        backup = saved / name
        if p.exists() and not backup.exists():
            backup.write_bytes(p.read_bytes())
            backup.chmod(0o600)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(files[name])
        p.chmod(0o644)
    commands.append(run(['dtc', '-@', '-I', 'dts', '-O', 'dtb', '-o',
                         '/boot/firmware/overlays/ptob-pps.dtbo', '/etc/ptob/pi5/ptob-pps.dts']))
    boot = Path('/boot/firmware/config.txt')
    if not (saved / 'config.txt').exists():
        (saved / 'config.txt').write_bytes(boot.read_bytes())
    marker = '# PToB GPIO16 PPS input (M-HAT D2/RTS)'
    config = boot.read_text()
    if marker not in config:
        boot.write_text(config.rstrip() + '\n\n[all]\n' + marker + '\ndtoverlay=ptob-pps\n')
    rule = Path('/etc/udev/rules.d/70-ptob-pps.rules')
    rule.write_text('SUBSYSTEM=="pps", ATTR{name}=="ptob-pps*", SYMLINK+="pps-ptob", GROUP="_chrony", MODE="0660"\n')
    commands.append(run(['udevadm', 'control', '--reload-rules']))
    if not any(p.joinpath('name').read_text().strip().startswith('ptob-pps') for p in Path('/sys/class/pps').glob('pps*')):
        commands.append(run(['dtoverlay', 'ptob-pps']))
    commands.append(run(['udevadm', 'trigger', '--subsystem-match=pps']))
    commands.append(run(['udevadm', 'settle']))
    if not Path('/dev/pps-ptob').exists():
        raise ValueError('exact GPIO PPS source not available; refusing chrony configuration')
    commands.append(run(['chronyd', '-p', '-f', '/etc/chrony/chrony.conf']))
    commands.append(run(['ptpd', '-k', '-c', '/etc/ptob/pi5/ptpd-wifi.conf']))
    commands.append(run(['systemd-analyze', 'verify', '/etc/systemd/system/ptob-ptp4l.service',
                         '/etc/systemd/system/ptob-phc2sys.service']))
    commands.append(run(['systemctl', 'disable', '--now', 'systemd-timesyncd'], check=False))
    commands.append(run(['systemctl', 'daemon-reload']))
    commands.append(run(['systemctl', 'enable', '--now', 'chrony']))
    commands.append(run(['systemctl', 'restart', 'chrony']))
    commands.append(run(['systemctl', 'enable', '--now', 'ptob-ptp-wifi']))
    commands.append(run(['systemctl', 'restart', 'ptob-ptp-wifi']))
    clocks = clock_install(dict(files={k:files[k] for k in
                                     ('ptob-phc2sys.service', 'ptob-ptp4l.service', 'ptp4l.conf')},
                               clock_script=PARAMS['clock_script']))
    receipt = {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'operations': commands,
               'file_sha256': {k: hashlib.sha256(v.encode()).hexdigest() for k, v in files.items()},
               'ptp_domain': 24, 'ptp_class': 248, 'pps_pin': 16,
               'precision_qualification': 'not performed', 'clock_chain': clocks}
    (Path('/var/lib/ptob/pi5') / 'timing-install.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def status():
    cmds = {'services': ['systemctl', 'is-active', 'chrony', 'ptob-ptp4l', 'ptob-phc2sys', 'ptob-ptp-wifi', 'systemd-timesyncd'],
            'tracking': ['chronyc', '-n', 'tracking'], 'sources': ['chronyc', '-n', 'sources', '-v'],
            'source_stats': ['chronyc', '-n', 'sourcestats', '-v'],
            'ethernet': ['ethtool', 'eth0'], 'timestamping': ['ethtool', '-T', 'eth0'],
            'phc_transfer': ['journalctl', '-u', 'ptob-phc2sys', '-n', '15', '--no-pager'],
            'ptp_log': ['journalctl', '-u', 'ptob-ptp4l', '-n', '15', '--no-pager'],
            'wifi_ptp_log': ['journalctl', '-u', 'ptob-ptp-wifi', '-n', '15', '--no-pager'],
            'wifi_ptp_policy': ['systemctl', 'show', 'ptob-ptp-wifi', '-p', 'ExecStart', '-p', 'CapabilityBoundingSet'],
            'ptp_datasets': ['pmc', '-u', '-b', '0', '-d', '24', '-s', '/run/ptob-ptp4l',
                             'GET PORT_DATA_SET', 'GET TIME_PROPERTIES_DATA_SET', 'GET PARENT_DATA_SET']}
    out = {k: run(v, check=False, timeout=15) for k, v in cmds.items()}
    out['pps'] = {p.name: {k: (p / k).read_text().strip() for k in ('name', 'path', 'assert')}
                  for p in Path('/sys/class/pps').glob('pps*')}
    out['pps_link'] = str(Path('/dev/pps-ptob').resolve())
    out['output_accuracy_measured'] = False
    out['clock_chain'] = {role: json.loads(p.read_text()) if p.exists() else None
                          for role, p in ((r, Path('/run/ptob-clocks')/(r+'.json'))
                                          for r in ('transfer', 'server'))}
    return out


if __name__ == '__main__':
    if 'PARAMS' not in globals():
        raise SystemExit('invoke through remote.py for exact-device guards')
    action = PARAMS['action']
    print(json.dumps(install(PARAMS['files']) if action == 'timing-install' else
                     clock_install(PARAMS) if action == 'clock-install' else status()))
