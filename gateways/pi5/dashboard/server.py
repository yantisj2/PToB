#!/usr/bin/env python3
"""Loopback-only dashboard. Copyright 2026 Jonathan Yantis. MIT."""
import argparse
import csv
import io
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
from pathlib import Path
import time
import sqlite3
import threading
from urllib.parse import urlsplit, parse_qs
from history import read as read_history, METRICS

STATIC = Path(__file__).resolve().parent / 'static'
STATE = Path('/run/ptob-dashboard/status.json')
HISTORY = Path('/var/lib/ptob-dashboard/history.sqlite3')
CLOCK_STATE = Path('/run/ptob-clocks')
FILES = {'/': ('index.html', 'text/html; charset=utf-8'),
         '/app.js': ('app.js', 'text/javascript; charset=utf-8'),
         '/charts.js': ('charts.js', 'text/javascript; charset=utf-8'),
         '/style.css': ('style.css', 'text/css; charset=utf-8')}
HISTORY_CACHE = {}
HISTORY_LOCK = threading.Lock()


def clock_chain_status():
    """Read the owned guards, not just their supervisor process liveness."""
    result = {}
    for name in ('transfer', 'server'):
        try:
            row = json.loads((CLOCK_STATE / (name + '.json')).read_text())
            stamp = row.get('observed_boottime_s')
            valid = type(stamp) in (int, float) and math.isfinite(stamp)
            age = time.clock_gettime(time.CLOCK_BOOTTIME) - stamp if valid else None
            result[name] = {key: row.get(key) for key in ('running', 'ready', 'reason', 'tai_offset_s')}
            result[name].update(age_s=age, fresh=age is not None and 0 <= age <= 2)
        except (OSError, ValueError, TypeError, AttributeError, OverflowError):
            result[name] = dict(fresh=False, age_s=None, reason='Guard observation unavailable')
    return result


def history(seconds):
    with HISTORY_LOCK:
        old = HISTORY_CACHE.get(seconds)
        if old and time.monotonic()-old[0] < 5:
            return old[1]
        value = read_history(HISTORY, seconds)
        mono = value.get('newest_mono_ms')
        value['sample_age_s'] = None if mono is None else (time.clock_gettime(time.CLOCK_BOOTTIME)*1000-mono)/1000
        HISTORY_CACHE[seconds] = (time.monotonic(), value)
        return value


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        route = urlsplit(self.path).path
        code = 200
        if route in ('/api/history', '/api/history/export'):
            mime = 'application/json'
            try:
                seconds = int(parse_qs(urlsplit(self.path).query).get('seconds', ['300'])[0])
                if route.endswith('/export'):
                    rows = read_history(HISTORY, seconds, export=True)
                    out = io.StringIO(newline='')
                    fields = ['at_ms', 'mono_ms', 'selected', 'receiver_boot', 'receiver_uptime_ms',
                              'lineage_schema', 'parent', 'gm_admitted', 'gm_root', 'gm_boot', 'gm_epoch', 'discovery_context',
                              'pps_sequence', 'pps_interval_ns', 'pps_gap', 'metric_schema', 'complete',
                              'source_sample_age_s', 'source_state', 'source_poll_s', 'source_fit_samples',
                              *METRICS, 'context']
                    writer = csv.DictWriter(out, fieldnames=fields, extrasaction='ignore')
                    writer.writeheader(); writer.writerows(rows)
                    body, mime = out.getvalue().encode(), 'text/csv; charset=utf-8'
                else:
                    body = json.dumps(history(seconds), allow_nan=False).encode()
            except ValueError:
                code, body = 400, b'{"error":"Choose a supported history window"}'
            except (OSError, sqlite3.Error):
                code, body = 503, b'{"error":"History is warming up; retry in 10 seconds"}'
        elif route in ('/api/status', '/api/export'):
            try:
                data = json.loads(STATE.read_text())
                age = max(0, time.clock_gettime(time.CLOCK_BOOTTIME)-data['observed_boottime_s'])
                data.update(sample_age_s=age, stale=age > 8)
                data['clock_chain'] = clock_chain_status()
                if data['stale']:
                    data['chain']['complete'] = False
                    data['chain']['summary'] = 'Device observations are stale'
                body = json.dumps(data, allow_nan=False).encode()
            except (OSError, ValueError, KeyError):
                code, body = 503, b'{"error":"Device observations unavailable; retry shortly"}'
            mime = 'application/json'
        elif route in FILES:
            name, mime = FILES[route]
            body = (STATIC / name).read_bytes()
        else:
            code, body, mime = 404, b'Not found', 'text/plain'
        self.send_response(code)
        for k, v in {'Content-Type': mime, 'Content-Length': str(len(body)), 'Cache-Control': 'no-store',
                     'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer',
                     'Content-Security-Policy': "default-src 'self'; frame-ancestors 'none'; base-uri 'none'; object-src 'none'"}.items():
            self.send_header(k, v)
        if route == '/api/export':
            self.send_header('Content-Disposition', 'attachment; filename="ptob-gateway-status.json"')
        if route == '/api/history/export' and code == 200:
            self.send_header('Content-Disposition', 'attachment; filename="ptob-timing-history.csv"')
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass  # Do not retain visitor metadata.


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--port', type=int, default=8080)
    p.add_argument('--state', type=Path, default=STATE)
    args = p.parse_args()
    STATE = args.state
    ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()
