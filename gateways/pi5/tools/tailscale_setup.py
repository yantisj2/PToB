#!/usr/bin/env python3
"""Official signed Tailscale packages and private Serve. Jonathan Yantis. MIT."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.request


def run(argv, timeout=30, check=True):
    p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout,
                       env=dict(os.environ, DEBIAN_FRONTEND='noninteractive'))
    if check and p.returncode:
        raise RuntimeError(p.stderr[-3000:] + p.stdout[-3000:])
    return dict(rc=p.returncode, out=p.stdout[-6000:], error=p.stderr[-3000:])


def state():
    r = subprocess.run(['tailscale', 'status', '--json'], capture_output=True, text=True, timeout=15, check=True)
    s = json.loads(r.stdout)
    return dict(state=s.get('BackendState'), ips=s.get('TailscaleIPs'),
                dns=s.get('Self', {}).get('DNSName'), auth_url=s.get('AuthURL'),
                health=s.get('Health'), enabled=run(['systemctl','is-enabled','tailscaled'])['out'].strip(),
                serve=run(['tailscale','serve','status','--json'])['out'])


def install():
    if os.geteuid():
        raise ValueError('root required')
    if 'VERSION_CODENAME=trixie' not in Path('/etc/os-release').read_text():
        raise ValueError('this provisioning source is for Debian trixie only')
    rows = {}
    for url, destination in [
        ('https://pkgs.tailscale.com/stable/debian/trixie.noarmor.gpg', '/usr/share/keyrings/tailscale-archive-keyring.gpg'),
        ('https://pkgs.tailscale.com/stable/debian/trixie.tailscale-keyring.list', '/etc/apt/sources.list.d/tailscale.list')]:
        with urllib.request.urlopen(url, timeout=30) as r:
            data = r.read(65536)
        p = Path(destination)
        if p.is_symlink():
            raise ValueError('refusing linked package config')
        p.write_bytes(data)
        p.chmod(0o644)
        rows[destination] = hashlib.sha256(data).hexdigest()
    run(['apt-get','update'], timeout=300)
    run(['apt-get','install','-y','--no-install-recommends','tailscale'], timeout=600)
    run(['systemctl','enable','--now','tailscaled'])
    s = state()
    if s['state'] != 'Running':
        run(['tailscale','up','--hostname=ptob-pi5','--accept-dns=false','--timeout=12s'], check=False)
        s = state()
    if s['state'] == 'Running':
        run(['tailscale','serve','--bg','--yes','http://127.0.0.1:8080'])
        s = state()
    return dict(repository_sha256=rows, package=run(['tailscale','version'])['out'], status=s,
                exposure='private Tailscale Serve only; no Funnel')


if __name__ == '__main__':
    if 'PARAMS' not in globals():
        raise SystemExit('invoke through remote.py for exact-device guards')
    print(json.dumps(install() if PARAMS['action']=='tailscale-install' else state()))
