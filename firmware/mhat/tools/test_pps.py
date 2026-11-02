#!/usr/bin/env python3
"""Exercise production projection and PPS planning offline. Jonathan Yantis. MIT."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = (root/'firmware/t096/src/rc_boundary.c').read_text()
arithmetic = source[source.index('static bool map_value('):source.index('bool rc5_clock_discipline(')]
arithmetic += source[source.index('bool rc5_map_project('):source.index('bool rc5_clock_capture(')]
with tempfile.TemporaryDirectory(prefix='ptob-mhat-pps-') as name:
    out=Path(name)
    (out/'projection.c').write_text('#include "rc_boundary.h"\n#include <limits.h>\n'+arithmetic)
    command=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
             '-fsanitize=undefined','-fno-sanitize-recover=all',
             '-I'+str(root/'firmware/mhat/include'),'-I'+str(root/'firmware/t096/include'),
             '-I'+str(root/'firmware/t096/protocol'),str(out/'projection.c'),
             str(root/'firmware/mhat/tests/pps_plan_test.c'),'-o',str(out/'test')]
    subprocess.run(command,check=True,timeout=30)
    subprocess.run([str(out/'test')],check=True,timeout=30)
