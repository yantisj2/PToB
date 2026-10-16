#!/usr/bin/env python3
"""Acquire public pinned dependencies into a new directory (no hardware).
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Requires Python 3.12+, Git, CMake, and the Apple Silicon host toolchain.
"""
import argparse,hashlib,json,platform,subprocess,sys,tarfile,urllib.request,venv
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dest',type=Path,required=True);a=p.parse_args()
    if sys.version_info<(3,12):p.error('Python 3.12+ required for safe archive extraction')
    if (platform.system(),platform.machine())!=('Darwin','arm64'):p.error('This SDK lock currently supports Apple Silicon; other host locks pending')
    dest=a.dest.resolve()
    if dest.exists():p.error('Use a new dependency directory')
    dest.mkdir(parents=True)
    lock=json.loads((Path(__file__).resolve().parents[1]/'dependencies.lock.json').read_text())
    for repo in [lock['zephyr'],*lock['modules']]:
        path=dest/repo['path'];path.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run(['git','init',str(path)],check=True)
        subprocess.run(['git','-C',str(path),'remote','add','origin',repo['url']],check=True)
        subprocess.run(['git','-C',str(path),'fetch','--depth','1','origin',repo['commit']],check=True)
        subprocess.run(['git','-C',str(path),'checkout','--detach',repo['commit']],check=True)
    for name,prefix,target in [('sdk-minimal.tar.xz','minimal',dest),('sdk-arm.tar.xz','arm',dest/lock['sdk']['path'])]:
        archive=dest/name
        urllib.request.urlretrieve(lock['sdk'][prefix+'_url'],archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=lock['sdk'][prefix+'_sha256']:raise ValueError('Downloaded SDK checksum mismatch')
        with tarfile.open(archive) as t:t.extractall(target,filter='data')
    venv.create(dest/'venv',with_pip=True)
    subprocess.run([str(dest/'venv/bin/python'),'-m','pip','install',*[k+'=='+v for k,v in lock['python'].items()]],check=True)
    print('Public pinned dependency directory:',dest)
if __name__=='__main__':main()
