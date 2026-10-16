#!/usr/bin/env python3
"""Shared registry rejection cases and production T096 display lookup."""
import json, os, subprocess, tempfile
from pathlib import Path
from registry import load, header
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as folder:
    out=Path(folder); path=out/'input.json'
    data=load(ROOT/'registry/example.json')
    for text in ['{"schema":1,"schema":1,"devices":{}}',
                 '{"schema":1,"devices":{"0000000000000000":{"alias":"Bad","target":"t096"}}}']:
        path.write_text(text)
        try: load(path)
        except ValueError: pass
        else: raise AssertionError('Invalid map accepted')
    for alias in ['bad\nname','x'*25,'"inject','   ']:
        d={'schema':1,'devices':{'0000000000000001':{'alias':alias,'target':'t096'}}}
        path.write_text(json.dumps(d))
        try: load(path)
        except ValueError: pass
        else: raise AssertionError('Invalid alias accepted')
    (out/'ptob_registry.h').write_text(header(data))
    (out/'test.c').write_text('''#include "ui.h"
#include <assert.h>
#include <string.h>
int main(void){char name[25];
r2_ui_identity_name(name,1,0);assert(!strcmp(name,"TC-01"));
r2_ui_identity_name(name,8,0);assert(!strcmp(name,"SS3"));
r2_ui_identity_name(name,9,0);assert(!strcmp(name,"Mac client"));
r2_ui_identity_name(name,1,"Renamed");assert(!strcmp(name,"Renamed"));
r2_ui_identity_name(name,1,"Clock-default");assert(!strcmp(name,"TC-01"));
r2_ui_identity_name(name,0,0);assert(!strcmp(name,"--"));
r2_ui_identity_name(name,0xabcdef,0);assert(!strcmp(name,"cdef"));}
''')
    app=ROOT/'firmware/t096'
    subprocess.run([os.environ.get('CC','cc'),'-DPTOB_NAME_REGISTRY=1','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),str(out/'test.c'),str(app/'src/ui_render.c'),str(app/'src/precision.c'),'-o',str(out/'test')],check=True)
    subprocess.run([str(out/'test')],check=True)
print('PASS: registry validation and production T096 name lookup/precedence/fallback')
