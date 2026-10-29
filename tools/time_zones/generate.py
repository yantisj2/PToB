#!/usr/bin/env python3
"""Generate the bounded read-only T096 TZ image from hash-pinned public inputs.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
from bisect import bisect_right
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import io
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import urllib.request
import zipfile
from zoneinfo import ZoneInfo

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
GRIDS = [(5, -180, 80, 130), (26, -32, 46, 64)]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True) + '\n').encode()


def tzif_transitions(data):
    if data[:4] != b'TZif' or data[4:5] not in (b'2', b'3', b'4'):
        raise ValueError('Expected modern POSIX TZif')
    gmt, standard, leap, times, types, chars = struct.unpack_from('>6I', data, 20)
    at = 44 + times * 5 + types * 6 + chars + leap * 8 + standard + gmt
    if data[at:at + 4] != b'TZif':
        raise ValueError('Missing 64-bit TZif block')
    _, _, leap, times, types, _ = struct.unpack_from('>6I', data, at + 20)
    if leap:
        raise ValueError('Leap-coordinate TZif is not a POSIX calendar')
    at += 44
    seconds = struct.unpack_from('>' + 'q' * times, data, at)
    indices = data[at + times * 8:at + times * 9]
    infos = at + times * 9
    offsets = [struct.unpack_from('>i', data, infos + i * 6)[0] for i in range(types)]
    return [(t, offsets[index]) for t, index in zip(seconds, indices)]


def utc_offset(zone, t):
    seconds = int(datetime.fromtimestamp(t, timezone.utc).astimezone(zone).utcoffset().total_seconds())
    if seconds % 60 or not -840 * 60 <= seconds <= 840 * 60:
        raise ValueError('Unsupported modern offset')
    return seconds // 60


def calendars(cache, work, lock, zones):
    source = work / 'tzdb'
    source.mkdir()
    for name in ('tzcode' + lock['tzdb'] + '.tar.gz', 'tzdata' + lock['tzdb'] + '.tar.gz'):
        with tarfile.open(cache / name) as archive:
            for member in archive:
                if not member.isfile():
                    continue
                path = Path(member.name)
                if path.is_absolute() or len(path.parts) != 1:
                    raise ValueError('Unexpected source path')
                (source / path).write_bytes(archive.extractfile(member).read())
    if (source / 'version').read_text().strip() != lock['tzdb']:
        raise ValueError('Incorrect embedded tzdb version')
    env = dict(__import__('os').environ)
    for key in ('CFLAGS', 'CPPFLAGS', 'LDFLAGS', 'MAKEFLAGS'):
        env.pop(key, None)
    with (work / 'zic-build.log').open('w') as log:
        subprocess.run(['make', '-j2', 'zic'], cwd=source, env=env, stdout=log,
                       stderr=subprocess.STDOUT, check=True, timeout=60)
    start, end = lock['valid_from'], lock['valid_until']
    with (work / 'zic-data.log').open('w') as log:
        subprocess.run([str(source / 'zic'), '-b', 'fat', '-r', f'@{start}/@{end}',
                        '-d', str(source / 'zoneinfo'), 'africa', 'antarctica', 'asia',
                        'australasia', 'europe', 'northamerica', 'southamerica', 'etcetera', 'backward'],
                       cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)
    unique, result = {}, []
    daily = boundaries = 0
    for row in zones:
        with (source / 'zoneinfo' / row['name']).open('rb') as stream:
            zone = ZoneInfo.from_file(stream, key=row['name'])
        raw = tzif_transitions((source / 'zoneinfo' / row['name']).read_bytes())
        initial = current = utc_offset(zone, start)
        changes = []
        for second, seconds_offset in raw:
            if start < second < end:
                if seconds_offset % 60:
                    raise ValueError('Non-minute transition')
                new = seconds_offset // 60
                if new != current:
                    changes.append((second, new))
                    current = new
        key = (initial, tuple(changes))
        index = unique.setdefault(key, len(unique))
        instants = [t for t, _ in changes]

        def packed_offset(t):
            i = bisect_right(instants, t)
            return initial if i == 0 else changes[i - 1][1]

        for t in range(start, end, 86400):
            if packed_offset(t) != utc_offset(zone, t):
                raise ValueError(('Daily offset mismatch', row['name'], t))
            daily += 1
        for t, _ in raw:
            if start <= t < end:
                for sample in (t - 1, t, t + 1):
                    if start <= sample < end:
                        if packed_offset(sample) != utc_offset(zone, sample):
                            raise ValueError(('Boundary mismatch', row['name'], sample))
                        boundaries += 1
        result.append(dict(row, calendar=index, initial_offset_min=initial, transitions=changes))
    return list(unique), result, dict(daily=daily, transition_boundaries=boundaries)


def shapes(path, stem, version='5.1.1'):
    import shapefile
    from shapely.geometry import shape
    from shapely import make_valid
    with zipfile.ZipFile(path) as archive:
        if archive.read(stem + '.VERSION.txt').decode().strip() != version:
            raise ValueError('Incorrect Natural Earth version')
        with shapefile.Reader(**{ext: io.BytesIO(archive.read(stem + '.' + ext))
                                 for ext in ('shp', 'shx', 'dbf')}, encoding='utf-8') as reader:
            for row in reader.iterShapeRecords():
                geom = shape(row.shape.__geo_interface__)
                if not geom.is_valid:
                    geom = make_valid(geom)
                yield row.record.as_dict(), geom


def geographic(cache, mapping, indices):
    from shapely.geometry import box
    from shapely.ops import unary_union
    from shapely.strtree import STRtree
    regions, codes, supported = [], [], []

    def add(geom, code, selected):
        if not geom.is_empty:
            regions.append(geom)
            codes.append(code)
            supported.append(selected)

    def parts(geom, rules):
        remaining = geom
        for rule in rules:
            part = geom
            if 'include_boxes' in rule:
                part = part.intersection(unary_union([box(*b) for b in rule['include_boxes']]))
            if 'exclude_boxes' in rule:
                part = part.difference(unary_union([box(*b) for b in rule['exclude_boxes']]))
            if not part.intersection(geom.difference(remaining)).is_empty and part.intersection(geom.difference(remaining)).area > 1e-10:
                raise ValueError('Overlapping authored cuts')
            add(part, indices[rule['zone']], True)
            remaining = remaining.difference(part)
        add(remaining, 255, True)

    countries = list(shapes(cache / 'ne_10m_admin_0_countries.zip', 'ne_10m_admin_0_countries'))
    subdivisions = list(shapes(cache / 'ne_10m_admin_1_states_provinces.zip', 'ne_10m_admin_1_states_provinces'))
    # Administrative polygons include water jurisdictions (e.g. Michigan's
    # portion of Lake Michigan in Chicago's cell). Classify supported land.
    lakes = unary_union([geom for _, geom in shapes(cache / 'ne_10m_lakes.zip', 'ne_10m_lakes', '5.0.0')])
    countries = [(props, geom.difference(lakes)) for props, geom in countries]
    subdivisions = [(props, geom.difference(lakes)) for props, geom in subdivisions]
    country_ids = {props['ADM0_A3'] for props, _ in countries}
    if not set(mapping['country_zones']).issubset(country_ids):
        raise ValueError('Missing selected country')
    state_ids = {props['iso_3166_2'] for props, _ in subdivisions}
    if not set(mapping['states']).issubset(state_ids):
        raise ValueError('Missing selected subdivision')
    used_states = []
    europe = box(*mapping['country_clip'])
    for props, geom in countries:
        code = props['ADM0_A3']
        if code in mapping['subdivision_countries']:
            pieces = []
            for state, boundary in subdivisions:
                if state['adm0_a3'] != code:
                    continue
                boundary = boundary.intersection(geom)
                pieces.append(boundary)
                key = state['iso_3166_2']
                rule = mapping['states'].get(key)
                if rule:
                    parts(boundary, rule.get('parts', [rule]))
                    used_states.append(dict(iso_3166_2=key, natural_earth_id=state['adm1_code']))
                else:
                    add(boundary, 255, True)
            add(geom.difference(unary_union(pieces)), 255, True)
        elif code in mapping['country_zones']:
            selected = geom if code in ('BLZ', 'GTM', 'SLV', 'HND', 'NIC', 'CRI', 'PAN') else geom.intersection(europe)
            rules = mapping['country_parts'].get(code, [{'zone': mapping['country_zones'][code]}])
            parts(selected, rules)
            add(geom.difference(selected), 0, False)
        else:
            add(geom, 0, False)
    tree = STRtree(regions)
    grids, populations = [], []
    for lat, lon, rows, columns in GRIDS:
        cells = []
        for y in range(lat, lat + rows):
            row = []
            for x in range(lon, lon + columns):
                cell = box(x, y, x + 1, y + 1)
                matches = [int(i) for i in tree.query(cell)
                           if regions[int(i)].intersection(cell).area > 1e-10]
                values = {codes[i] for i in matches}
                selected = any(supported[i] for i in matches)
                value = next(iter(values)) if selected and len(values) == 1 else 255 if selected else 0
                row.append(value)
            cells.append(row)
        runs, offsets = bytearray(), []
        for row in cells:
            offsets.append(8 + (rows + 1) * 2 + len(runs))
            begin = 0
            while begin < columns:
                end = begin + 1
                while end < columns and row[end] == row[begin]:
                    end += 1
                runs.extend((end, row[begin]))
                begin = end
        offsets.append(8 + (rows + 1) * 2 + len(runs))
        grids.append(struct.pack('<hhBBH', lat, lon, rows, columns, 0) +
                     struct.pack('<' + 'H' * len(offsets), *offsets) + runs)
        populations.append(dict(latitude_start=lat, longitude_start=lon, rows=rows,
                                columns=columns, bytes=len(grids[-1]),
                                cells=dict(sorted(Counter(v for r in cells for v in r).items())), grid=cells))
    return grids, populations, used_states


def principal_locations(work, populations, records):
    import re
    rows = {}
    for line in (work / 'tzdb/zone.tab').read_text().splitlines():
        if line and not line.startswith('#'):
            fields = line.split('\t')
            rows[fields[2]] = fields[1]
    results = []
    for row in records:
        if row['name'] == 'Etc/UTC':
            continue
        text = rows[row['name']]
        match = re.fullmatch(r'([+-])(\d{4}|\d{6})([+-])(\d{5}|\d{7})', text)
        if not match:
            raise ValueError('Invalid IANA principal coordinate')

        def degrees(sign, value, width):
            degree = int(value[:width])
            minute = int(value[width:width + 2])
            second = int(value[width + 2:] or '0')
            scaled = degree * 10000000 + (minute * 60 + second) * 10000000 // 3600
            return scaled if sign == '+' else -scaled

        lat = degrees(match[1], match[2], 2)
        lon = degrees(match[3], match[4], 3)
        index = 0
        for grid in populations:
            y = lat // 10000000 - grid['latitude_start']
            x = lon // 10000000 - grid['longitude_start']
            if 0 <= y < grid['rows'] and 0 <= x < grid['columns']:
                index = grid['grid'][y][x]
                break
        actual = 65535 if index == 255 else records[index - 1]['id'] if index else 0
        outcome = 'ambiguous' if actual == 65535 else 'unsupported' if not actual else 'correct' if actual == row['id'] else 'wrong'
        results.append(dict(name=row['name'], expected_id=row['id'], actual_id=actual,
                            latitude_e7=lat, longitude_e7=lon, outcome=outcome))
    return dict(schema=1, license='CC-BY-4.0', copyright='2026 Jonathan Yantis',
                population='All 97 selected IANA zone.tab principal locations; not a representative accuracy sample',
                counts=dict(Counter(row['outcome'] for row in results)), locations=results)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True, help='Fresh ignored scratch directory')
    p.add_argument('--out', type=Path, default=REPO / 'firmware/t096/data')
    p.add_argument('--download', action='store_true', help='Acquire only missing hash-pinned public archives')
    args = p.parse_args()
    for package, expected in [('numpy', '2.0.2'), ('shapely', '2.0.7'), ('pyshp', '2.3.1')]:
        if importlib.metadata.version(package) != expected:
            raise ValueError('Use pinned requirements: ' + package)
    lock = json.loads((HERE / 'sources.lock.json').read_text())
    zones = json.loads((HERE / 'zones.json').read_text())['zones']
    if len(zones) > 254 or len({z['id'] for z in zones}) != len(zones) or len({z['name'] for z in zones}) != len(zones):
        raise ValueError('Invalid public zone registry')
    if zones[:2] != [{'id': 1, 'name': 'Etc/UTC'}, {'id': 2, 'name': 'America/Chicago'}]:
        raise ValueError('Existing wire IDs must remain stable')
    args.cache.mkdir(parents=True, exist_ok=True)
    for item in lock['inputs']:
        path = args.cache / item['name']
        if not path.exists() and args.download:
            with urllib.request.urlopen(item['url'], timeout=30) as response:
                data = response.read(item['bytes'] + 1)
            if len(data) != item['bytes'] or sha(data) != item['sha256']:
                raise ValueError('Downloaded archive mismatch: ' + item['name'])
            path.write_bytes(data)
        if not path.exists() or path.stat().st_size != item['bytes'] or sha(path.read_bytes()) != item['sha256']:
            raise ValueError('Missing or changed pinned archive: ' + item['name'])
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=False)
    args.out.mkdir(parents=True, exist_ok=True)
    keys, records, comparisons = calendars(args.cache.resolve(), args.work, lock, zones)
    names, zone_data, calendar_data, transitions = bytearray(), bytearray(), bytearray(), bytearray()
    for row in records:
        zone_data.extend(struct.pack('<HHH', row['id'], row['calendar'], len(names)))
        names.extend(row['name'].encode() + b'\0')
    for initial, changes in keys:
        calendar_data.extend(struct.pack('<hHH', initial, len(transitions) // 6, len(changes)))
        for second, value in changes:
            transitions.extend(struct.pack('<Ih', second, value))
    mapping = json.loads((HERE / 'regions.json').read_text())
    grids, populations, regions = geographic(args.cache.resolve(), mapping,
                                            {z['name']: i + 1 for i, z in enumerate(zones)})
    coverage = principal_locations(args.work, populations, records)
    (args.work / 'coverage.json').write_bytes(json_bytes(coverage))
    if coverage['counts'].get('wrong', 0):
        raise ValueError('Wrong region IDs at IANA principal locations; see work/coverage.json')
    image, offsets = bytearray(32), []
    for section in (zone_data, calendar_data, transitions, names, *grids):
        offsets.append(len(image))
        image.extend(section)
    if len(image) > 16384:
        raise ValueError('Generated flash data exceeds 16 KiB')
    struct.pack_into('<4sIIHH8H', image, 0, b'TZ01', lock['valid_from'], lock['valid_until'],
                     len(records), len(keys), *offsets, len(image), 1)
    text = ('/* Generated dataset: CC-BY-4.0. Copyright (c) 2026 Jonathan Yantis.\n'
            ' * IANA tzdb and Natural Earth inputs are public domain; see README.md.\n'
            ' * Regenerate with tools/time_zones/generate.py. Do not edit. */\n'
            '#pragma once\n#include <stdint.h>\n#define R2_TZ_DB_VERSION "2026f/NE5.1.1"\n'
            'static const uint8_t r2_tz_data[] = {\n')
    text += ''.join(' ' + ','.join('0x%02x' % v for v in image[i:i + 16]) + ',\n'
                    for i in range(0, len(image), 16)) + '};\n'
    (args.out / 'time_zone_data.h').write_text(text)
    manifest = dict(schema=1, license='CC-BY-4.0', copyright='2026 Jonathan Yantis',
                    sources=lock, image_sha256=sha(image), image_bytes=len(image),
                    header_sha256=sha(text.encode()), sections=dict(zip(
                        ['zones', 'calendars', 'transitions', 'names', 'americas', 'europe'], offsets)),
                    calendar_comparisons=comparisons, zones=records, calendars=len(keys),
                    transitions=len(transitions) // 6, geographic_regions=regions,
                    geographic_populations=[{k: v for k, v in pop.items() if k != 'grid'} for pop in populations],
                    generator_sha256=sha(Path(__file__).read_bytes()),
                    inputs_sha256={name: sha((HERE / name).read_bytes()) for name in
                                   ('sources.lock.json', 'zones.json', 'regions.json', 'requirements.txt')})
    (args.out / 'manifest.json').write_bytes(json_bytes(manifest))
    (args.out / 'coverage.json').write_bytes(json_bytes(coverage))
    (args.work / 'grid.json').write_bytes(json_bytes(populations))
    print(json.dumps(dict(status='PASS', image_bytes=len(image), zones=len(records),
                         calendars=len(keys), transitions=len(transitions) // 6,
                         comparisons=comparisons, image_sha256=sha(image)), indent=2))


if __name__ == '__main__':
    main()
