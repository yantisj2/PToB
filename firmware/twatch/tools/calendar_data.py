#!/usr/bin/env python3
"""Build compact NOAA high/low data. Network only with explicit --fetch.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,datetime as dt,hashlib,json
from decimal import Decimal
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import urlopen
APP=Path(__file__).resolve().parents[1]
STATION='8729511'
def compile_data(years,cache,out,fetch=False):
    if not years or years!=list(range(years[0],years[-1]+1)):raise ValueError('Contiguous years required')
    events=[];sources=[]
    for year in years:
        url='https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?'+urlencode(dict(product='predictions',application='PToB_Watch',begin_date=f'{year}0101',end_date=f'{year}1231',datum='MLLW',station=STATION,time_zone='gmt',units='metric',interval='hilo',format='json'))
        p=cache/f'{year}.json'
        if fetch:
            cache.mkdir(parents=True,exist_ok=True)
            raw=urlopen(url,timeout=45).read();body=json.loads(raw)
            if 'predictions' not in body:raise ValueError(body)
            p.write_bytes(raw)
        raw=p.read_bytes();rows=json.loads(raw)['predictions'];annual=[]
        for row in rows:
            when=dt.datetime.strptime(row['t'],'%Y-%m-%d %H:%M').replace(tzinfo=dt.timezone.utc)
            if when.year!=year or row['type'] not in ('H','L'):raise ValueError('Invalid event')
            minute=int(when.timestamp())//60;mm=int(Decimal(row['v'])*1000)
            if not -32768<=mm<=32767 or not 0<minute<2**31:raise ValueError('Encoding overflow')
            annual.append((minute,mm,row['type']))
        if not 500<=len(annual)<=1600:raise ValueError('Unexpected annual event count')
        if annual[0][0]>int(dt.datetime(year,1,3,tzinfo=dt.timezone.utc).timestamp())//60 or annual[-1][0]<int(dt.datetime(year,12,29,tzinfo=dt.timezone.utc).timestamp())//60:raise ValueError('Incomplete annual coverage')
        events+=annual;sources.append(dict(year=year,url=url,sha256=hashlib.sha256(raw).hexdigest(),events=len(rows)))
    if any(b[0]<=a[0] or b[0]-a[0]>48*60 for a,b in zip(events,events[1:])):raise ValueError('Unordered/duplicate/missing events')
    start=int(dt.datetime(years[0],1,1,tzinfo=dt.timezone.utc).timestamp());end=int(dt.datetime(years[-1]+1,1,1,tzinfo=dt.timezone.utc).timestamp())
    encoded=[(t<<1)|(kind=='H') for t,mm,kind in events]
    text='// NOAA CO-OPS public-domain predictions; see ../data/calendar_manifest.json.\n// Original encoding Copyright (c) 2026 Jonathan Yantis, CC BY 4.0.\n'
    text+=f'static constexpr uint64_t TideStart={start}ULL,TideEnd={end}ULL;\n'
    text+='static constexpr uint32_t TideTimes[]={\n'+''.join(' '+','.join(str(v)+'u' for v in encoded[i:i+8])+',\n' for i in range(0,len(events),8))+'};\n'
    text+='static constexpr int16_t TideHeights[]={\n'+''.join(' '+','.join(str(e[1]) for e in events[i:i+16])+',\n' for i in range(0,len(events),16))+'};\n'
    out.mkdir(parents=True,exist_ok=True);header=out/'tides.inc';header.write_text(text)
    manifest=dict(schema=1,station=STATION,station_name='Destin, East Pass, FL',reference_station='8729840',scope='Nearby Gulf reference; not a Santa Rosa Beach gauge or weather forecast',datum='MLLW',time_zone='UTC',height_unit='mm',event_resolution_seconds=60,coverage_start_utc=start,coverage_end_exclusive_utc=end,events=len(events),runtime_table_bytes=len(events)*6,source_terms='NOAA public domain; https://oceanservice.noaa.gov/about/faq.html',encoding_license='CC-BY-4.0; original encoding Jonathan Yantis',sources=sources,generated_sha256=hashlib.sha256(header.read_bytes()).hexdigest())
    (out/'calendar_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return manifest
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cache',type=Path,required=True);p.add_argument('--out',type=Path,default=APP/'data');p.add_argument('--years',type=int,nargs='+',default=[2026,2027]);p.add_argument('--fetch',action='store_true');a=p.parse_args()
    m=compile_data(a.years,a.cache,a.out,a.fetch);print(f"{m['events']} events, {m['runtime_table_bytes']} bytes; {m['station_name']}")
