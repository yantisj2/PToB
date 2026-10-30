#!/usr/bin/env python3
"""Exercise actual cached compilation, generated headers and relinking.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from compiler_cache import environment, launcher, finish


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--ccache', required=True)
    a = p.parse_args()
    with tempfile.TemporaryDirectory(prefix='ptob-cache-') as temp:
        root = Path(temp).resolve()
        ccache, env = environment(root, 'host', a.ccache)
        cc = launcher(root, ccache, shutil.which('clang'))
        source = root / 'source with spaces.c'
        header = root / 'value.h'
        source.write_text('#include "value.h"\n#ifndef EXTRA\n#define EXTRA 0\n#endif\nint main(void){return VALUE + EXTRA;}\n')
        header.write_text('#define VALUE 7\n')
        def build(name, extra=()):
            out = root / name
            subprocess.run([cc, '-Wall', '-Wextra', '-Werror', *extra, str(source), '-o', str(out)], env=env, check=True)
            return subprocess.run([str(out)], env=env).returncode
        assert build('first') == 7
        assert build('relinked') == 7
        header.write_text('#define VALUE 11\n')
        assert build('changed header') == 11
        assert build('changed define', ['-DEXTRA=3']) == 14
        # Failed compilation must neither replace an output nor enter the cache.
        header.write_text('#error intentional invalid generated configuration\n')
        bad = subprocess.run([cc, str(source), '-o', str(root / 'refused')], env=env, capture_output=True)
        assert bad.returncode and not (root / 'refused').exists()
        header.write_text('#define VALUE 19\n')
        assert build('recovered') == 19
        finish(ccache, env)
        assert not (os.stat(root / 'artifacts/.compiler-cache/host').st_mode & 0o077)
    print('PASS actual cached compile/link, paths with spaces, header/flag invalidation and failed-output refusal')


if __name__ == '__main__':
    main()
