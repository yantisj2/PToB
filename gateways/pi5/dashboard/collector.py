#!/usr/bin/env python3
"""Read-only, bounded device observations. Copyright 2026 Jonathan Yantis. MIT."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
import datetime
import io
import json
import os
from pathlib import Path
import select
import re
import subprocess
import termios
import time
from history import History, ns, tracking, source_stats


def command(argv, timeout=3):
    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        return {'ok': p.returncode == 0, 'text': p.stdout[-12000:].strip(),
                'error': p.stderr[-1000:].strip() if p.returncode else None}
    except (OSError, subprocess.TimeoutExpired) as e:
        return {'ok': False, 'text': '', 'error': str(e)}


def sources(text):
    result = []
    for row in csv.reader(io.StringIO(text)):
        try:
            result.append(dict(kind=row[0], state=row[1], name=row[2], stratum=int(row[3]),
                               poll=int(row[4]), reach=row[5], age_s=int(row[6]),
                               offset_s=float(row[7]), measured_offset_ns=ns(row[8]),
                               offset_ns=ns(row[7]), error_s=float(row[9])))
        except (IndexError, ValueError):
            continue
    return result


class PPS:
    """Observe every edge; never subtract floating point Unix timestamps."""
    def __init__(self):
        self.seq = self.stamp = self.at = None
        self.latest = dict(present=False, sequence=None, fresh=False, age_s=None)

    def poll(self, now=None):
        now = time.monotonic() if now is None else now
        try:
            dev = Path('/dev/pps-ptob').resolve(strict=True).name
            base = Path('/sys/class/pps') / dev
            if not (base / 'name').read_text().strip().startswith('ptob-pps'):
                raise ValueError('Wrong PPS identity')
            stamp, seq = (base / 'assert').read_text().strip().split('#')
            seq, tick = int(seq), ns(stamp)
            if seq != self.seq and seq:
                consecutive = self.seq is not None and seq == self.seq + 1
                interval = tick-self.stamp if consecutive else None
                regular = consecutive and 500_000_000 < interval < 1_500_000_000
                if self.seq is not None or abs(time.time_ns()-tick) < 5_000_000_000:
                    self.at = now
                self.latest = dict(present=True, sequence=seq, assert_utc=stamp,
                                   phase_ns=(tick+500_000_000)%1_000_000_000-500_000_000,
                                   period_error_ns=interval-1_000_000_000 if regular else None,
                                   interval_ns=interval, gap_interval=consecutive and not regular,
                                   skipped_edges=max(0, seq-self.seq-1) if self.seq is not None else None,
                                   pin=16)
                self.seq, self.stamp = seq, tick
            self.latest['present'] = True
            self.latest.pop('error', None)
        except (OSError, ValueError) as e:
            self.latest.update(present=False, error=str(e))
        age = None if self.at is None else now-self.at
        # A 1 Hz source must not appear continuously live through 2–3 s gaps.
        return dict(self.latest, age_s=age, fresh=self.latest['present'] and age is not None and age < 1.5)


def observe(previous, health_due):
    """Queries run in one worker, never in the UART/edge polling loop."""
    at = time.monotonic()
    observations = previous.copy()
    queries = {'sources': ['chronyc', '-c', '-n', 'sources'],
               'tracking': ['chronyc', '-c', '-n', 'tracking'],
               'sourcestats': ['chronyc', '-c', '-n', 'sourcestats']}
    if health_due:
        queries.update(services=['systemctl', 'show', '--property=Id,ActiveState,SubState',
                                 'chrony', 'ptob-ptp4l', 'ptob-phc2sys', 'ptob-ptp-wifi', 'tailscaled'],
                       ptp=['pmc', '-u', '-i', '/run/ptob-dashboard/pmc', '-b', '0', '-d', '24', '-s', '/run/ptob-ptp4l',
                            'GET PORT_DATA_SET', 'GET TIME_PROPERTIES_DATA_SET'])
    for key, argv in queries.items():
        observations[key] = command(argv)
    return observations, (time.monotonic()-at)*1000


def receiver_view(record, binding):
    """Bind telemetry to the enrolled factory identity and installed source."""
    if (record.get('target') != 'mhat-b504e' or
            record.get('device_id') != binding['device_id'] or
            record.get('particle_id') != binding['particle_id'] or
            record.get('source') not in binding['allowed_sources']):
        raise ValueError('receiver identity/source mismatch')
    if not re.fullmatch('[0-9a-f]{16}', record.get('boot', '')):
        raise ValueError('invalid receiver boot')
    for field in ('following', 'clock_valid'):
        if not isinstance(record.get(field), bool):
            raise ValueError('invalid receiver boolean')
    for field in ('rx', 'authenticated', 'rejected', 'pps_emitted', 'pps_rejected', 'pps_armed', 'offer_valid'):
        if type(record.get(field)) is not int or record[field] < 0:
            raise ValueError('invalid receiver counter')
    allowed = ('version', 'source', 'boot', 'uptime_ms', 'error', 'receiver_error', 'rx',
               'authenticated', 'rejected', 'following', 'clock_valid', 'pps_emitted',
               'pps_rejected', 'pps_armed', 'offer_valid', 'cycles', 'active',
               'transport', 'acquisition', 'admission', 'connection', 'radio', 'capture_error', 'uart_error',
               'mesh', 'path', 'fit', 'pps_plan', 'timing_event', 'clock_source', 'discovery')
    return {k: record[k] for k in allowed if k in record}


class Receiver:
    def __init__(self, binding):
        self.binding, self.fd, self.buffer = binding, None, b''
        self.last, self.at, self.error = None, None, 'Waiting for HAT UART telemetry'
        self.events, self.boot = [], None

    def poll(self):
        try:
            if self.fd is None:
                self.fd = os.open(self.binding['uart'], os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
                a = termios.tcgetattr(self.fd)
                a[0] = a[1] = a[3] = 0
                a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
                a[4] = a[5] = termios.B115200
                a[6][termios.VMIN] = a[6][termios.VTIME] = 0
                termios.tcsetattr(self.fd, termios.TCSANOW, a)
            while select.select([self.fd], [], [], 0)[0]:
                data = os.read(self.fd, 4096)
                if not data:
                    break
                self.buffer = (self.buffer + data)[-16384:]
                while b'\n' in self.buffer:
                    raw, self.buffer = self.buffer.split(b'\n', 1)
                    try:
                        self.last = receiver_view(json.loads(raw), self.binding)
                        self.at, self.error = time.monotonic(), None
                        if self.boot != self.last.get('boot'):
                            self.boot, self.events = self.last.get('boot'), []
                        event = self.last.get('timing_event', {})
                        if (event.get('found') and event.get('boot') == self.boot and
                                event.get('source') == self.last['source'] and
                                not any(e['sequence'] == event['sequence'] for e in self.events)):
                            self.events = (self.events + [event])[-128:]
                    except (ValueError, TypeError, KeyError):
                        self.error = 'Invalid or unbound HAT telemetry discarded'
        except OSError as e:
            self.error = 'HAT UART unavailable: ' + str(e)
            if self.fd is not None:
                os.close(self.fd)
                self.fd = None
        age = None if self.at is None else time.monotonic() - self.at
        return dict(data=self.last, age_s=age, fresh=age is not None and age < 5,
                    transport='HAT UART', error=self.error, events=self.events)


def chain_state(receiver, pps, rows):
    selected = next((s for s in rows if s['state'] == '*'), None)
    pps_selected = selected is not None and selected['name'] == 'PTOB' and selected['kind'] == '#'
    r = receiver.get('data') or {}
    admitted = bool(receiver.get('fresh') and r.get('following') and r.get('clock_valid') and
                    not r.get('error') and not r.get('receiver_error') and
                    not r.get('capture_error') and r.get('offer_valid'))
    phase = bool(pps_selected and pps.get('fresh'))
    if not admitted:
        state = 'Receiver timing unavailable'
    elif not pps.get('fresh'):
        state = 'Waiting for PPS edges'
    elif not pps_selected:
        state = 'PPS present; awaiting chrony selection'
    else:
        state = 'PToB phase disciplines the system clock'
    return dict(summary=state, admitted=admitted, pps_selected=pps_selected,
                pps_disciplined=phase, chrony_holdover=bool(pps_selected and not pps.get('fresh')),
                complete=bool(admitted and phase), selected=selected,
                output_accuracy_measured=False, sub_microsecond_qualified=False)


def silicon(low, high):
    return f'{high:08x}{low:08x}' if type(low) is int and type(high) is int and (low or high) else None


class SourceTracker:
    """Descriptive source history. It never grants clock or PPS permission."""
    def __init__(self):
        self.previous, self.events = None, []

    def update(self, receiver, utc):
        rx = receiver.get('data') or {}
        a, mesh = rx.get('acquisition') or [], rx.get('mesh') or []
        source, discovery = rx.get('clock_source') or {}, rx.get('discovery') or {}
        parent = silicon(a[1], a[2]) if len(a) == 20 else None
        admitted = bool(receiver.get('fresh') and rx.get('following') and rx.get('clock_valid') and rx.get('offer_valid')
                        and not rx.get('error') and not rx.get('receiver_error') and not rx.get('capture_error'))
        root = source.get('root')
        if root == '0000000000000000': root = None
        state = dict(boot=rx.get('boot'), root=root, root_boot=source.get('root_boot') if root else None,
                     epoch=source.get('epoch') if root else None, parent=parent, admitted=admitted,
                     selections=mesh[2] if len(mesh) == 6 else None,
                     restarts=mesh[3] if len(mesh) == 6 else None)
        previous = self.previous
        changes = []
        if state['boot'] and (previous is None or not previous['boot']): changes.append('Receiver first observed')
        elif previous and state['boot'] and state['boot'] != previous['boot']: changes.append('Receiver restarted')
        if previous and previous['boot'] and previous['boot'] == state['boot']:
            if state['parent'] != previous['parent']: changes.append('Selected parent changed')
            if (state['root'], state['root_boot'], state['epoch']) != (previous['root'], previous['root_boot'], previous['epoch']):
                changes.append('Admitted GM identity or incarnation changed' if root else 'GM context cleared')
            if admitted != previous['admitted']: changes.append('Timing acquired' if admitted else 'Timing withdrawn')
            if state['restarts'] != previous['restarts']: changes.append('Mesh rediscovery count changed')
        for change in changes:
            self.events.append(dict(utc=utc, event=change, **state))
        self.events = self.events[-32:]
        self.previous = state
        last = source.get('last_valid_ms')
        age = max(0, (rx.get('uptime_ms', 0)-last)/1000) if last else None
        reports = []
        for report in discovery.get('reports', []):
            reports.append(dict(report, fresh=bool(receiver.get('fresh') and report.get('fresh'))))
        pending = next((e for e in reversed(receiver.get('events', [])) if e.get('kind') in (11, 13, 14, 27)), None)
        return dict(**state, current=admitted and bool(source.get('valid')) and bool(root),
                    last_valid_age_s=age, periodic_sync=bool(receiver.get('fresh') and len(a) == 20 and a[7]),
                    control_admitted=bool(receiver.get('fresh') and len(a) == 20 and a[8]),
                    context=discovery.get('context'), discovery_state=discovery.get('state'),
                    recovery_remaining_s=discovery.get('restart_after_ms', 0)/1000 if discovery else None,
                    reports=reports, last_control=pending, changes=self.events,
                    reporting_available=bool(source), stale=not receiver.get('fresh'))


def atomic_json(path, value):
    tmp = path.with_suffix('.tmp')
    tmp.write_text(json.dumps(value, allow_nan=False) + '\n')
    tmp.chmod(0o644)
    tmp.replace(path)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--config', type=Path, default=Path('/etc/ptob/pi5/receiver.json'))
    p.add_argument('--state', type=Path, default=Path('/run/ptob-dashboard/status.json'))
    p.add_argument('--history', type=Path, default=Path('/var/lib/ptob-dashboard/history.sqlite3'))
    args = p.parse_args()
    args.state.parent.mkdir(parents=True, exist_ok=True)
    binding = json.loads(args.config.read_text())
    receiver = Receiver(binding)
    from history import sample
    pps_reader = PPS()
    history = History(args.history)
    source_tracker = SourceTracker()
    worker = ThreadPoolExecutor(max_workers=1, thread_name_prefix='observations')
    pending, observations, next_sample, health_at = None, {}, 0, 0
    while True:
        r = receiver.poll()
        now = time.monotonic()
        pps = pps_reader.poll(now)
        if pending is not None and pending.done():
            observations, elapsed = pending.result()
            pending = None
            rows = sources(observations['sources']['text']) if observations['sources']['ok'] else []
            try:
                ethernet = Path('/sys/class/net/eth0/carrier').read_text().strip() == '1'
            except OSError:
                ethernet = None
            cached = dict(schema=2, observed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                          observed_boottime_s=time.clock_gettime(time.CLOCK_BOOTTIME), receiver=r, pps=pps,
                          sources=rows, source_stats=source_stats(observations['sourcestats']['text']),
                          tracking=tracking(observations['tracking']['text']),
                          chain=chain_state(r, pps, rows), observations=observations,
                          ethernet_link=ethernet, ptp_domain=24, wifi_ptp='software / ARB timescale',
                          accuracy='Not independently measured', aliases=binding.get('aliases', {}),
                          collector=dict(query_ms=elapsed, history_commit_s=10, health_poll_s=15))
            cached['source_status'] = source_tracker.update(r, cached['observed_utc'])
            checks = Path('/etc/ptob/pi5/wire-checks.json')
            if checks.exists():
                try:
                    cached['wire_checks'] = json.loads(checks.read_text())
                except (ValueError, OSError):
                    pass
            history.add(sample(cached, elapsed))
            atomic_json(args.state, cached)
        if pending is None and now >= next_sample:
            health_due = now >= health_at
            pending = worker.submit(observe, observations, health_due)
            if health_due: health_at = now + 15
            # Anchor the cadence. Adding one second to each delayed wakeup
            # drifts through PPS boundaries and loses averaging observations.
            if not next_sample: next_sample = now
            next_sample += 1
            if next_sample <= now: next_sample = now + 1
        time.sleep(0.02)


if __name__ == '__main__':
    main()
