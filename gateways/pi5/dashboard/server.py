#!/usr/bin/env python3
"""Loopback-only dashboard. Copyright 2026 Jonathan Yantis. MIT."""
import argparse
import csv
import gzip
import io
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
from pathlib import Path
import time
import sqlite3
import threading
from urllib.parse import urlsplit, parse_qs
from history import read as read_history, METRICS, REPORTED_METRICS, LEGACY_REPORT_FIELDS

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


def status_freshness(data):
    """UI current-source claims use the same three-second fence as serving."""
    stamp = data.get('observed_boottime_s')
    try:
        valid = type(stamp) in (int, float) and math.isfinite(stamp)
    except OverflowError:
        valid = False
    age = time.clock_gettime(time.CLOCK_BOOTTIME)-stamp if valid else None
    stale = age is None or not 0 <= age <= 3
    data.update(sample_age_s=age, stale=stale)
    if stale:
        data['chain']['complete'] = False
        data['chain']['summary'] = 'Device observations are stale'
    return data


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
            value = old[1].copy()
        else:
            value = read_history(HISTORY, seconds)
            HISTORY_CACHE[seconds] = (time.monotonic(), value.copy())
        mono = value.get('newest_mono_ms')
        value['sample_age_s'] = None if mono is None else (time.clock_gettime(time.CLOCK_BOOTTIME)*1000-mono)/1000
        return value


def accepts_gzip(header):
    """Honor an explicit gzip refusal even when a wildcard is accepted."""
    choices = {}
    for item in header.lower().split(','):
        parts = [p.strip() for p in item.split(';')]
        quality = 1.0
        for parameter in parts[1:]:
            name, _, value = parameter.partition('=')
            if name.strip() == 'q':
                try:
                    quality = float(value)
                except ValueError:
                    quality = 0
        choices[parts[0]] = quality if 0 < quality <= 1 else 0
    return choices.get('gzip', choices.get('*', 0)) > 0


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
                              *LEGACY_REPORT_FIELDS,
                              'source_sample_age_s', 'source_state', 'source_poll_s', 'source_fit_samples',
                              'tracking_reference', 'tracking_mode', 'source_mode', 'timing_reason',
                              'pps_fresh', 'pps_age_s', 'pps_observed_interval_s', 'pps_skipped_edges',
                              'receiver_following', 'receiver_clock_valid', 'receiver_offer_valid',
                              *METRICS, *('reported_'+k for k in REPORTED_METRICS), 'context']
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
                status_freshness(data)
                data['clock_chain'] = clock_chain_status()
                body = json.dumps(data, allow_nan=False).encode()
            except (OSError, ValueError, KeyError):
                code, body = 503, b'{"error":"Device observations unavailable; retry shortly"}'
            mime = 'application/json'
        elif route in FILES:
            name, mime = FILES[route]
            body = (STATIC / name).read_bytes()
        else:
            code, body, mime = 404, b'Not found', 'text/plain'
        compressed = len(body) >= 1024 and accepts_gzip(self.headers.get('Accept-Encoding', ''))
        if compressed:
            # Low compression cost on CPUs0-2; much less 2.4GHz Wi-Fi airtime
            # beside the HAT's BLE receiver. No history points are discarded.
            body = gzip.compress(body, compresslevel=1, mtime=0)
        self.send_response(code)
        for k, v in {'Content-Type': mime, 'Content-Length': str(len(body)), 'Cache-Control': 'no-store',
                     'Vary': 'Accept-Encoding',
                     'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer',
                     'Content-Security-Policy': "default-src 'self'; frame-ancestors 'none'; base-uri 'none'; object-src 'none'"}.items():
            self.send_header(k, v)
        if compressed:
            self.send_header('Content-Encoding', 'gzip')
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
