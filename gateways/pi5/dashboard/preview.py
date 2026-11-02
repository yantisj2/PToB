#!/usr/bin/env python3
"""Loopback dashboard preview with synthetic data only. Jonathan Yantis. MIT.

Uses the shipped frontend without reading profiles, hardware or private logs.
Values demonstrate presentation, not measured timing performance.
"""
import argparse
import csv
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import math
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

STATIC = Path(__file__).resolve().parent / 'static'
ROOT = '1111111111111111'


def status(holdover=False):
    selected = dict(kind='#', state='*', name='PTOB', reach='377', offset_s=72e-9, error_s=18e-9)
    return dict(stale=False, sample_age_s=.2, observed_utc='2026-01-01T12:00:00Z', ethernet_link=False,
        aliases={ROOT: dict(alias='Example GM', target='t096')},
        chain=dict(summary='Illustrative PToB timing path', complete=not holdover, admitted=not holdover,
            pps_disciplined=not holdover, pps_selected=True, chrony_holdover=holdover, selected=selected),
        receiver=dict(fresh=True, age_s=.4, transport='HAT UART', error=None, events=[], data=dict(
            version='Example', authenticated=2400, rx=3200, pps_emitted=600, pps_rejected=0,
            clock_valid=not holdover, offer_valid=not holdover, uptime_ms=600000,
            acquisition=[0, 0x11111111, 0x11111111]+[0]*17,
            fit=dict(locked=not holdover, samples=64, rms_valid=True, rms_ns=58, age_valid=True, age_ms=25))),
        pps=dict(fresh=not holdover, age_s=27.7 if holdover else .3, sequence=600),
        source_status=dict(current=not holdover, stale=False, root=ROOT, root_boot='2222222222222222',
            epoch=1, parent=ROOT, context='3333333333333333', last_valid_age_s=27.7 if holdover else 0,
            reports=[], changes=[], discovery_state=2, control_admitted=True, periodic_sync=True,
            selections=1, restarts=0, recovery_remaining_s=120),
        sources=[selected], source_stats=[dict(name='PTOB', stddev_ns=240)],
        tracking=dict(reference='PTOB', leap='Normal', tracking_rms_ns=96, tracking_offset_ns=42),
        observations=dict(services=dict(text='\n\n'.join('Id='+name+'\nActiveState=active' for name in
            ['chrony.service', 'ptob-ptp-wifi.service', 'ptob-phc2sys.service', 'ptob-ptp4l.service'])),
            tracking=dict(text='Synthetic preview'), ptp=dict(text='Illustrative preview; no packets measured')),
        clock_chain=dict(transfer=dict(fresh=True, running=not holdover),
            server=dict(fresh=True, ready=not holdover, running=not holdover,
                reason='Admitted PPS unavailable' if holdover else 'ready')))


def history(seconds=300):
    points=[]
    for i in range(seconds):
        phase=round(260*math.sin(i*.17)+105*math.cos(i*.41))
        if i%113==50:phase+=2400
        points.append(dict(at_ms=1767268800000-(seconds-1-i)*1000, segment=0, selected='PTOB',
            source_offset_ns=round(phase*.7), tracking_offset_ns=round(phase*.25),
            tracking_rms_ns=round(90+45*math.sin(i*.025)), source_stddev_ns=round(230+60*math.cos(i*.04)),
            fit_rms_ns=round(55+12*math.sin(i*.12)), fit_drift_ppb=round(1450+25*math.sin(i*.015)),
            pps_phase_ns=phase, pps_period_ns=round(180*math.sin(i*.3)), query_ms=5,
            uart_age_ms=250, source_sample_age_s=0, source_poll_s=1))
    statistics={}
    for key in points[0]:
        if not key.endswith(('_ns','_ms','_ppb')) or key=='at_ms':continue
        values=[p[key] for p in points];n=len(values);mean=math.fsum(values)/n;ordered=sorted(map(abs,values))
        statistics[key]=dict(n=n,mean=mean,rms=math.sqrt(math.fsum(v*v for v in values)/n),
            stddev=math.sqrt(math.fsum((v-mean)**2 for v in values)/n),min=min(values),max=max(values),
            p95_abs=ordered[math.ceil(.95*n)-1],p99_abs=ordered[math.ceil(.99*n)-1])
    return dict(points=points,statistics=statistics,population=len(points),window_s=seconds,sample_age_s=0,gaps=0)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        url=urlsplit(self.path)
        if url.path in ('/api/status','/api/export'):
            body=json.dumps(status(self.server.holdover)).encode();mime='application/json'
        elif url.path in ('/api/history','/api/history/export'):
            try:seconds=int(parse_qs(url.query).get('seconds',['300'])[0])
            except ValueError:self.send_error(400);return
            if seconds not in (60,300,1800,7200,21600):self.send_error(400);return
            data=history(seconds)
            if url.path.endswith('/export'):
                out=io.StringIO(newline='');writer=csv.DictWriter(out,fieldnames=list(data['points'][0]))
                writer.writeheader();writer.writerows(data['points']);body=out.getvalue().encode();mime='text/csv'
            else:body=json.dumps(data).encode();mime='application/json'
        else:
            name={'/':'index.html','/app.js':'app.js','/charts.js':'charts.js','/style.css':'style.css'}.get(url.path)
            if not name:self.send_error(404);return
            body=(STATIC/name).read_bytes()
            if name=='index.html':
                body=body.replace(b'Private \xc2\xb7 Tailscale',b'Illustrative demo')
                body=body.replace(b'PToB gateway \xc2\xb7 pi.ptob.dev DNS pending',b'Generic preview - no device connected')
            mime='text/css' if name.endswith('.css') else 'text/javascript' if name.endswith('.js') else 'text/html'
        self.send_response(200)
        self.send_header('Content-Type',mime+'; charset=utf-8');self.send_header('Content-Length',str(len(body)))
        self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(body)

    def log_message(self,*_):pass


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=8090)
    parser.add_argument('--holdover',action='store_true')
    args=parser.parse_args()
    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler);server.holdover=args.holdover
    print(f'Illustrative dashboard only: http://127.0.0.1:{args.port}/',flush=True)
    server.serve_forever()
