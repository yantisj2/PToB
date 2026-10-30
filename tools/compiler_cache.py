"""Bounded, local-only ccache for offline host and native builds.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys


def environment(repo, lane, executable=None):
    repo = Path(repo).resolve()
    ccache = shutil.which(str(executable or 'ccache'))
    if not ccache:
        raise ValueError('ccache is required for --compiler-cache; ordinary uncached checks remain available')
    ccache = str(Path(ccache).resolve())
    root = repo / 'artifacts' / '.compiler-cache'
    cache = root / lane
    if lane not in ('host', 'native-w12'):
        raise ValueError('Unknown compiler cache lane')
    for path in (cache, *cache.parents):
        if path.is_symlink():
            raise ValueError('Compiler cache must be a private real directory')
    cache.mkdir(parents=True, exist_ok=True, mode=0o700)
    for path in (root, cache):
        if path.is_symlink():
            raise ValueError('Compiler cache must be a private real directory')
        path.chmod(0o700)
    config = cache / 'ccache.conf'
    if config.is_symlink():
        raise ValueError('Compiler cache configuration must not be a symlink')
    config.write_text('max_size = 512M\ncompiler_check = content\numask = 0077\n')
    config.chmod(0o600)
    env = {k: v for k, v in os.environ.items() if not k.startswith('CCACHE_')}
    env.update(CCACHE_DIR=str(cache), CCACHE_CONFIGPATH=str(config),
               CCACHE_BASEDIR=str(repo), CCACHE_NOHASHDIR='true')
    return ccache, env


def launcher(repo, ccache, compiler):
    compiler = shutil.which(compiler)
    if not compiler:
        raise ValueError('Host compiler unavailable')
    root = Path(repo) / 'artifacts' / '.compiler-cache' / 'launchers'
    root.mkdir(parents=True, exist_ok=True, mode=0o700)
    name = hashlib.sha256((ccache + compiler).encode()).hexdigest()[:16]
    path = root / name
    path.write_text('#!' + sys.executable + '\nimport sys\nsys.path.insert(0,' +
                    repr(str(Path(__file__).resolve().parent)) + ')\nfrom compiler_cache import execute\nraise SystemExit(execute(' +
                    repr(ccache) + ',' + repr(compiler) + ',sys.argv[1:]))\n')
    path.chmod(0o700)
    return str(path)


def finish(ccache, env):
    subprocess.run([ccache, '--cleanup'], env=env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)


def execute(ccache, compiler, args):
    # Existing host drivers compile and link in one command. Split a single
    # translation unit so ccache can reuse compilation; always link afresh.
    sources = [x for x in args if not x.startswith('-') and Path(x).suffix in ('.c', '.cc', '.cpp', '.cxx')]
    if len(sources) == 1 and '-o' in args and not any(x in args for x in ('-c', '-E', '-M', '-MM', '-fsyntax-only')):
        output = args[args.index('-o') + 1]
        obj = output + '.host.o'
        compile_args = [x for x in args if Path(x).suffix not in ('.o', '.obj', '.a')]
        compile_args[compile_args.index('-o') + 1] = obj
        result = subprocess.run([ccache, compiler, '-c', *compile_args])
        if result.returncode:
            return result.returncode
        link_args = [x for x in args if x != sources[0]]
        result = subprocess.run([compiler, *link_args, obj, '-Wno-unused-command-line-argument'])
        return result.returncode
    return subprocess.run([ccache, compiler, *args]).returncode


if __name__ == '__main__':
    raise SystemExit(execute(sys.argv[1], sys.argv[2], sys.argv[3:]))
