#!/usr/bin/env python3
"""Host-only Solar golden/moving-frame equivalence and renderer work comparison.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path);p.add_argument('--goldens',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
app=Path(__file__).resolve().parents[1];current=app/'main/watch.cpp'
base=['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(app/'main')]
results={}
for name,path in ([('before',a.baseline)] if a.baseline else [])+[('after',current)]:
    body=path.read_text()
    for fn in ['sin','cos','sqrt','pow']:body=body.replace('std::'+fn+'(', 'solar_count_'+fn+'(')
    preamble='#include <cmath>\n#include <cstdint>\nuint64_t solar_math_counts[4]{};\n'
    for i,fn in enumerate(['sin','cos','sqrt','pow']):
        args='double x,double y' if fn=='pow' else 'double x';call='x,y' if fn=='pow' else 'x'
        preamble+=f'double solar_count_{fn}({args}){{++solar_math_counts[{i}];return std::{fn}({call});}}\n'
    preamble+='float solar_count_sqrt(float x){return std::sqrt(x);}'
    source=a.out/(name+'.cpp');source.write_text(preamble+body)
    exe=a.out/name
    subprocess.run(base+[str(source),str(app/'main/calendar.cpp'),str(app/'tests/solar_render_perf.cpp'),'-o',str(exe)],check=True,timeout=90)
    results[name]=json.loads(subprocess.check_output([str(exe)],text=True,timeout=60))
if a.baseline:
    assert results['before']['pixel_sequence_hash']==results['after']['pixel_sequence_hash'],'Moving-frame pixels changed'
    assert results['after']['sin_calls']+results['after']['cos_calls']<=results['before']['sin_calls']+results['before']['cos_calls'], 'Warm trigonometry increased'
assert results['after']['sin_calls']+results['after']['cos_calls']<=16*results['after']['frames'],'Warm path unexpectedly rebuilds geometry'
# Check the actual overloads as well as the counted build: instrumentation must
# never silently promote hardware-float calculations to double.
counts=a.out/'counts.cpp';counts.write_text('#include <cstdint>\nuint64_t solar_math_counts[4]{};\n')
actual=a.out/'actual'
subprocess.run(base+[str(current),str(app/'main/calendar.cpp'),str(app/'tests/solar_render_perf.cpp'),str(counts),'-o',str(actual)],check=True,timeout=90)
results['actual']=json.loads(subprocess.check_output([str(actual)],text=True,timeout=60))
assert results['actual']['pixel_sequence_hash']==results['after']['pixel_sequence_hash'],'Instrumentation changed renderer pixels'
exe=a.out/'preview'
subprocess.run(base+[str(current),str(app/'main/calendar.cpp'),str(app/'tools/preview.cpp'),'-o',str(exe)],check=True,timeout=90)
goldens=sorted(a.goldens.glob('*.ppm')) if a.goldens else []
if a.goldens:assert len(goldens)>=55
for golden in goldens:
    output=a.out/golden.name;subprocess.run([str(exe),golden.stem,str(output)],check=True,timeout=15)
    assert output.read_bytes()==golden.read_bytes(),f'Golden changed: {golden.name}'
results.update(status='PASS',sqrt_counter='double_precision_calls_only_float_roots_excluded',scope='HOST_RENDER_WORK_AND_PIXEL_EQUIVALENCE_NOT_DEVICE_LATENCY',goldens_equal=len(goldens),sources={str(q.relative_to(app)):hashlib.sha256(q.read_bytes()).hexdigest() for q in [current,*[app/'main'/name for name in ['watch.hpp','font.hpp','calendar.hpp','calendar.cpp','solar_geometry.hpp']],app/'tools/preview.cpp',app/'tests/solar_render_perf.cpp',Path(__file__).resolve()]})
(a.out/'receipt.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))
