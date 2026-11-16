#!/usr/bin/env python3
"""Offline solar/NOAA data and runtime contract checks; no hardware or network.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,datetime as dt,hashlib,json,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
exe=a.out/'calendar-test'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(APP/'main'),str(APP/'main/calendar.cpp'),str(APP/'tests/calendar_test.cpp'),'-o',str(exe)],check=True)
r=subprocess.run([exe],check=True,capture_output=True,text=True);(a.out/'runtime.log').write_text(r.stdout)
actual={line.split()[0]:list(map(int,line.split()[1:])) for line in r.stdout.splitlines()}
references=json.loads((APP/'data/solar_reference.json').read_text());errors=[]
for row in references['rows']:
    events={e['phen']:e['time'] for e in row['events']}
    for kind,got in zip(['Begin Civil Twilight','Rise','Set','End Civil Twilight'],actual[row['date']]):
        when=dt.datetime.fromisoformat(row['date']+'T'+events[kind]).replace(tzinfo=dt.timezone(dt.timedelta(hours=row['utc_offset_hours'])))
        error=abs(got-int(when.timestamp()));assert error<=180,(row['date'],kind,error)
        errors.append(error)
astronomy=a.out/'astronomy-test'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(APP/'main'),str(APP/'main/calendar.cpp'),str(APP/'tests/astronomy_test.cpp'),'-o',str(astronomy)],check=True)
subprocess.run([astronomy],check=True)
motion=a.out/'solar-motion-test'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(APP/'main'),str(APP/'tests/solar_motion_test.cpp'),'-o',str(motion)],check=True)
subprocess.run([motion],check=True)
phase_errors=[]
for ref in json.loads((APP/'data/astronomy_reference.json').read_text())['rows']:
    values=list(map(float,subprocess.check_output([astronomy,str(ref['utc'])],text=True).split()))
    for actual_value,key in zip(values,['sun_altitude','sun_azimuth','moon_phase','moon_illumination']):
        assert abs(actual_value-ref[key])<1e-8,(ref['utc'],key,actual_value,ref[key])
    if 'usno_phase' in ref:
        error=abs(values[2]-ref['usno_phase']);error=min(error,1-error);assert error<.015
        phase_errors.append(error)
manifest=json.loads((APP/'data/calendar_manifest.json').read_text());data=APP/'data/tides.inc'
assert hashlib.sha256(data.read_bytes()).hexdigest()==manifest['generated_sha256']
assert manifest['events']==1548 and manifest['runtime_table_bytes']==9288
sources=[APP/'main/mobile_time.hpp',APP/'main/watch.hpp',APP/'tests/solar_motion_test.cpp',APP/'vendor/suncalc/astronomy.hpp',APP/'vendor/suncalc/LICENSE',APP/'vendor/suncalc/provenance.json',APP/'tests/astronomy_test.cpp',APP/'data/astronomy_reference.json',APP/'main/calendar.hpp',APP/'main/calendar.cpp',APP/'tests/calendar_test.cpp',APP/'data/tides.inc',APP/'data/solar_reference.json',APP/'data/calendar_manifest.json',Path(__file__),APP/'tools/calendar_data.py']
receipt=dict(status='PASS',max_usno_moon_phase_difference=max(phase_errors),upstream_astronomy_parity=True,hardware_access=False,solar_reference='USNO four dates; sunrise/set and civil twilight',max_solar_difference_seconds=max(errors),scope='MODELED_SOLAR_AND_OFFICIAL_STATION_PREDICTIONS_NOT_FIELD_OBSERVATION',sources={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in sources})
(a.out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PASS calendar; max USNO difference',max(errors),'seconds; 9288-byte tide table')
