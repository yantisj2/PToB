#!/usr/bin/env python3
"""Bounded system→PHC transfer and fail-closed Ethernet PTP supervision.

Copyright (c) 2026 Jonathan Yantis. MIT. Independently implemented; no Time Pi code.
"""
import argparse
import json
import math
import os
from pathlib import Path
import signal
import socket
import subprocess
import time

NS = 1_000_000_000
STATE = Path('/run/ptob-clocks')
MAX_STATUS_AGE = 3.0
MAX_TRANSFER_AGE = 2.0
MAX_READ_BRACKET_NS = 100_000
MAX_TRANSFER_ERROR_NS = 50_000  # Startup health fence, NOT an output accuracy claim.
SETTLE_S = 3.0


def boottime():
    return time.clock_gettime(time.CLOCK_BOOTTIME)


def read_json(path):
    return json.loads(Path(path).read_text())


def atomic_json(path, value):
    tmp = path.with_suffix('.tmp')
    tmp.write_text(json.dumps(value, allow_nan=False) + '\n')
    tmp.chmod(0o644)
    tmp.replace(path)


def notify(message):
    address = os.environ.get('NOTIFY_SOCKET')
    if address:
        if address.startswith('@'):
            address = '\0' + address[1:]
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sock:
            sock.sendto(message.encode(), address)


def bracket(clockid, expected_offset_s=0):
    """Adjacent reads: report the whole bracket, never call this calibrated error."""
    before = time.time_ns()
    value = time.clock_gettime_ns(clockid)
    after = time.time_ns()
    width = after-before
    if not 0 <= width <= MAX_READ_BRACKET_NS:
        raise ValueError('clock read bracket too wide or system clock stepped')
    return dict(offset_ns=value-(before+after)//2-expected_offset_s*NS,
                bracket_ns=width, realtime_ns=(before+after)//2)


def tai_offset():
    observation = bracket(time.CLOCK_TAI)
    seconds = round(observation['offset_ns']/NS)
    if not 10 <= seconds <= 100 or abs(observation['offset_ns']-seconds*NS) > MAX_READ_BRACKET_NS:
        raise ValueError('kernel CLOCK_TAI offset unavailable or inconsistent')
    return seconds


def phc_device(interface='eth0'):
    net = Path('/sys/class/net')/interface
    # macb registers below the netdev; other NICs use device/ptp or phydev/ptp.
    clocks = {p.name: p for base in (net, net/'device/ptp', net/'phydev/ptp')
              for p in base.glob('ptp*') if p.name.removeprefix('ptp').isdigit() and
              p.resolve() == (Path('/sys/class/ptp')/p.name).resolve()}
    if len(clocks) != 1:
        raise ValueError('require one PHC bound to Ethernet interface')
    return '/dev/' + next(iter(clocks))


def phc_observation(device, offset):
    fd = os.open(device, os.O_RDONLY)
    try:
        # Linux CLOCKFD dynamic POSIX clock, read-only fd; never adjusts a clock.
        return bracket((~fd << 3) | 3, offset)
    finally:
        os.close(fd)


def fresh(observation, field, now, limit):
    stamp = observation.get(field)
    return type(stamp) in (int, float) and math.isfinite(stamp) and 0 <= now-stamp <= limit


def serving_reason(status, transfer, capture, offset, device, now):
    """Every false/unknown input withdraws. This policy grants no UTC qualification."""
    if not fresh(status, 'observed_boottime_s', now, MAX_STATUS_AGE):
        return 'collector unavailable or stale'
    if not status.get('chain', {}).get('complete'):
        return 'admitted PPS/chrony path unavailable'
    if status.get('tracking', {}).get('leap') != 'Normal':
        return 'chrony unsynchronized or leap pending'
    if not status.get('source_status', {}).get('current'):
        return 'admitted source identity unavailable'
    if not fresh(transfer, 'observed_boottime_s', now, MAX_TRANSFER_AGE):
        return 'PHC transfer supervisor unavailable or stale'
    if (not transfer.get('running') or transfer.get('tai_offset_s') != offset or
            transfer.get('device') != device):
        return 'PHC transfer owner or timescale mismatch'
    if (type(offset) is not int or not 10 <= offset <= 100 or
            any(type(capture.get(k)) is not int for k in ('bracket_ns', 'offset_ns')) or
            not 0 <= capture['bracket_ns'] <= MAX_READ_BRACKET_NS or
            abs(capture['offset_ns']) > MAX_TRANSFER_ERROR_NS):
        return 'PHC transfer outside startup health fence'
    return None


def source_token(status, transfer, offset):
    source = status.get('source_status', {})
    return tuple(source.get(k) for k in ('boot', 'root', 'root_boot', 'epoch', 'parent')) + (
        transfer.get('generation'), offset)


class Readiness:
    def __init__(self):
        self.token = self.since = None

    def update(self, reason, token, now):
        if reason or token != self.token:
            self.token, self.since = token, None if reason else now
        if reason:
            return False, reason
        if self.since is None:
            self.since = now
        return (True, 'ready') if now-self.since >= SETTLE_S else (False, 'settling after source/transfer change')


def terminate(child):
    if child is not None and child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=1)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('role', choices=('transfer', 'serve', 'observe'))
    args = parser.parse_args()
    device = phc_device()
    if args.role == 'observe':
        offset = tai_offset()
        print(json.dumps(dict(device=device, tai_offset_s=offset,
                              phc_minus_system_tai=phc_observation(device, offset))))
        return
    STATE.mkdir(mode=0o755, exist_ok=True)
    # One supervising instance per role; systemd owns children in the same cgroup.
    import fcntl
    lock = (STATE/(args.role+'.lock')).open('a')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    stopping = False
    def stop(signum, frame):
        nonlocal stopping
        stopping = True
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    child, active_offset, active_token = None, None, None
    readiness = Readiness()
    output = STATE/('transfer.json' if args.role == 'transfer' else 'server.json')
    generation = time.monotonic_ns()
    transitions, previous_state = [], None
    notify('READY=1')
    try:
        while not stopping:
            now = boottime()
            row = dict(schema=1, observed_boottime_s=now, device=device, role=args.role,
                       generation=generation, running=False, output_accuracy_measured=False)
            try:
                offset = tai_offset()
                row['tai_offset_s'] = offset
                if args.role == 'transfer':
                    if child is not None and (child.poll() is not None or active_offset != offset):
                        terminate(child)
                        child = None
                    if child is None:
                        # Deliberately no -w: PHC must be current before link-up/PTP serving.
                        child = subprocess.Popen(['/usr/sbin/phc2sys', '-s', 'CLOCK_REALTIME',
                                                  '-c', device, '-O', str(offset), '-R', '4',
                                                  '-u', '16', '-m', '-q'])
                        active_offset, generation = offset, time.monotonic_ns()
                    row.update(running=child.poll() is None, generation=generation,
                               source='CLOCK_REALTIME', destination=device,
                               utc_tai_provenance='kernel CLOCK_TAI; chrony leapsectz right/UTC')
                    row['capture'] = phc_observation(device, offset)
                else:
                    status = read_json('/run/ptob-dashboard/status.json')
                    transfer = read_json(STATE/'transfer.json')
                    capture = phc_observation(device, offset)
                    # Writers may publish between loop start and these reads.
                    # Evaluate age only after obtaining the complete snapshot;
                    # a newer legitimate publication must not look future-dated.
                    now = boottime()
                    reason = serving_reason(status, transfer, capture, offset, device, now)
                    token = source_token(status, transfer, offset)
                    ready, reason = readiness.update(reason, token, now)
                    row.update(ready=ready, reason=reason, capture=capture)
                    if child is not None and (not ready or token != active_token):
                        terminate(child)
                        child = None
                    if child is not None and child.poll() is not None:
                        raise RuntimeError('ptp4l exited; restart supervisor after bounded backoff')
                    if ready and child is None:
                        child = subprocess.Popen(['/usr/sbin/ptp4l', '-f', '/etc/ptob/pi5/ptp4l.conf',
                                                  '-i', 'eth0', '--utc_offset', str(offset), '-m', '-q'])
                        active_token = token
                    row['running'] = child is not None and child.poll() is None
            except (OSError, ValueError, KeyError) as error:
                terminate(child)
                child = None
                readiness.update(str(error), None, now)
                row.update(ready=False, reason=str(error))
            current_state = (row.get('ready'), row.get('running'), row.get('reason'), row.get('generation'))
            if current_state != previous_state:
                transitions.append(dict(at_boottime_s=now, ready=row.get('ready'), running=row['running'],
                                        reason=row.get('reason'), generation=row.get('generation')))
                transitions = transitions[-16:]
                previous_state = current_state
            row['transitions'] = transitions
            atomic_json(output, row)
            notify('WATCHDOG=1')
            time.sleep(0.25)
    finally:
        terminate(child)
        atomic_json(output, dict(schema=1, observed_boottime_s=boottime(), running=False,
                                ready=False, reason='supervisor stopped', role=args.role))
        lock.close()


if __name__ == '__main__':
    main()
