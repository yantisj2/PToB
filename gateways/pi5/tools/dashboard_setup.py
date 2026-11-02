#!/usr/bin/env python3
"""Deploy dashboard source through the pinned SSH guard. Jonathan Yantis. MIT."""
import hashlib
import json
import os
from pathlib import Path
import subprocess


def run(argv):
    p = subprocess.run(argv, capture_output=True, text=True, timeout=30)
    if p.returncode:
        raise RuntimeError(p.stderr + p.stdout)
    return p.stdout.strip()


def install(params):
    if os.geteuid():
        raise ValueError('root required')
    # Only our installed header UART, never /dev/serial0 (the Pi debug UART).
    node = Path('/sys/class/tty/ttyAMA0/device/of_node').resolve()
    if not str(node).endswith('/rp1/serial@30000'):
        raise ValueError('Header UART identity mismatch; enable uart0-pi5 without ctsrts')
    files = params['files']
    expected = {'collector.py', 'history.py', 'server.py', 'static/index.html', 'static/app.js', 'static/charts.js', 'static/style.css'}
    if set(files) != expected:
        raise ValueError('unexpected dashboard file set')
    root = Path('/opt/ptob/pi5/dashboard')
    for name, content in files.items():
        dest = root / name
        if dest.is_symlink():
            raise ValueError('linked dashboard output')
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(content)
        dest.chmod(0o644)
    for parent in (Path('/opt/ptob'), Path('/opt/ptob/pi5')):
        parent.chmod(parent.stat().st_mode & 0o777 | 0o111)
    root.chmod(0o755)
    (root / 'static').chmod(0o755)
    state = Path('/var/lib/ptob-dashboard')
    state.mkdir(parents=True, exist_ok=True)
    state.chmod(0o755)
    conf = Path('/etc/ptob/pi5/receiver.json')
    binding = params['binding'].copy()
    if conf.exists():
        previous = json.loads(conf.read_text())
        if any(previous.get(k) != binding[k] for k in ('device_id','particle_id','uart')):
            raise ValueError('refusing to replace a different receiver binding')
        if 'aliases' in previous:
            binding['aliases'] = previous['aliases']
    conf.write_text(json.dumps(binding)+'\n')
    conf.chmod(0o600)
    exists = subprocess.run(['id', 'ptob-dashboard'], capture_output=True).returncode == 0
    if not exists:
        run(['useradd', '--system', '--user-group', '--no-create-home', '--shell', '/usr/sbin/nologin', 'ptob-dashboard'])
    for name, content in params['units'].items():
        if name not in ('ptob-dashboard.service', 'ptob-telemetry.service'):
            raise ValueError('unexpected service')
        (Path('/etc/systemd/system') / name).write_text(content)
    run(['systemd-analyze', 'verify', '/etc/systemd/system/ptob-dashboard.service', '/etc/systemd/system/ptob-telemetry.service'])
    run(['systemctl', 'daemon-reload'])
    run(['systemctl', 'reset-failed', 'ptob-telemetry', 'ptob-dashboard'])
    run(['systemctl', 'enable', '--now', 'ptob-telemetry', 'ptob-dashboard'])
    run(['systemctl', 'restart', 'ptob-telemetry', 'ptob-dashboard'])
    return dict(files={k: hashlib.sha256(v.encode()).hexdigest() for k,v in files.items()},
                services=run(['systemctl', 'is-active', 'ptob-telemetry', 'ptob-dashboard']),
                listener='127.0.0.1:8080', access='Tailscale Serve; no Funnel', uart='/dev/ttyAMA0')


if __name__ == '__main__':
    if 'PARAMS' not in globals():
        raise SystemExit('invoke through remote.py for exact-device guards')
    print(json.dumps(install(PARAMS)))
