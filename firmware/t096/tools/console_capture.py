#!/usr/bin/env python3
"""MIT. Copyright (c) 2026 Jonathan Yantis.

Finite macOS console capture, exact USB/source/boot binding, read-only commands.
No reconnect, reset, RF command, clock control, daemon or private dependency.
"""
import argparse
import concurrent.futures
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import select
import subprocess
import termios
import time
import tty
import zlib


class Frames:
    def __init__(self):
        self.header = self.payload = None
        self.sequence = 0

    def feed(self, line):
        if len(line) > 3072:
            self.header = self.payload = None
            raise ValueError('oversized frame line')
        match = re.fullmatch(r'R2 ([0-9a-f]{8}) ([0-9a-f]{8}) (\d{1,4}) ([0-9a-f]{8})', line)
        if match:
            interrupted = self.header is not None
            self.header = tuple(int(match[i], 10 if i == 3 else 16) for i in range(1, 5))
            self.payload = None
            if self.header[2] > 2976:
                self.header = None
                raise ValueError('oversized payload')
            if interrupted:
                raise ValueError('previous frame truncated')
            return None
        if self.header is None:
            return None
        if self.payload is None:
            self.payload = line
            return None
        (request, sequence, size, crc), payload = self.header, self.payload
        self.header = self.payload = None
        if line != f'R2 END {request:08x} {sequence:08x}':
            raise ValueError('missing frame terminator')
        wire = payload.encode('ascii')
        if len(wire) != size or zlib.crc32(wire) != crc:
            raise ValueError('payload length/CRC mismatch')
        if sequence <= self.sequence:
            raise ValueError('frame sequence regressed; possible boot change')
        value = json.loads(payload)
        if not isinstance(value, dict):
            raise ValueError('payload must be an object')
        self.sequence = sequence
        return request, value


class Rotating:
    def __init__(self, folder, suffix, limit):
        self.folder, self.suffix, self.limit = folder, suffix, limit
        self.index = self.size = 0
        self.file = None

    def write(self, data):
        if self.file is None or self.size + len(data) > self.limit:
            if self.file:
                self.file.close()
            self.file = (self.folder / f'{self.index:06d}.{self.suffix}').open('xb')
            self.index += 1
            self.size = 0
        self.file.write(data)
        self.file.flush()
        self.size += len(data)

    def close(self):
        if self.file:
            self.file.close()


def resolve(serial, expected_pid=None):
    """Fresh exact serial to callout mapping and numeric descriptor ownership."""
    deadline=time.monotonic()+8
    tree = subprocess.check_output(['/usr/sbin/ioreg', '-r', '-c', 'IOUSBHostDevice', '-l', '-w', '0'], text=True, timeout=8)
    ports = set()
    for block in re.split(r'(?=^\+-o )', tree, flags=re.M):
        serials = re.findall(r'"(?:USB Serial Number|kUSBSerialNumberString)" = "([^"]+)"', block)
        if serial.casefold() in [s.casefold() for s in serials]:
            ports.update(re.findall(r'"IOCalloutDevice" = "([^"]+)"', block))
    if len(ports) != 1:
        raise OSError('USB serial mapping missing or ambiguous')
    port = ports.pop()
    remaining=deadline-time.monotonic()
    if remaining<=0:raise TimeoutError('identity scan deadline')
    owner = subprocess.run(['/usr/sbin/lsof', '-nPlX', '-Fp', port], text=True, capture_output=True, timeout=remaining)
    if owner.stderr.strip():
        raise OSError('console ownership unknown')
    pids = set(re.findall(r'^p(\d+)$', owner.stdout, flags=re.M))
    if expected_pid is None:
        safe = owner.returncode == 1 and not owner.stdout.strip()
    else:
        safe = owner.returncode == 0 and pids == {str(expected_pid)}
    if not safe:
        raise OSError('foreign or unknown console owner')
    return port


def validate_config(devices):
    if not isinstance(devices, dict) or not devices:
        raise ValueError('config requires devices')
    for unit, c in devices.items():
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,24}', unit):
            raise ValueError('invalid unit name')
        for key, n in [('silicon_id', 16), ('source', 64), ('commit', 40)]:
            if not re.fullmatch(r'[0-9a-f]{' + str(n) + '}', c[key]):
                raise ValueError('invalid expected identity: ' + key)
        if not isinstance(c['usb_serial'], str) or not 1 <= len(c['usb_serial']) <= 128:
            raise ValueError('invalid USB serial')
    if len({c['usb_serial'].casefold() for c in devices.values()}) != len(devices):
        raise ValueError('duplicate USB identity')


def capture(unit, config, out, seconds, rotate_bytes, stop=None, snapshot_interval=30):
    folder = out / unit
    folder.mkdir(mode=0o700)
    raw, journal = [Rotating(folder, suffix, rotate_bytes) for suffix in ['raw', 'jsonl']]
    state = dict(unit=unit, status='INCOMPLETE', queries=0, timeouts=0, refused_frames=0,
                 missing_events=0, reset_or_configuration_commands=False)
    started = time.monotonic()
    deadline = started + seconds
    fd = lock = None
    buffer = b''
    frames = Frames()
    latest = None
    boot = 0
    # Existing firmware requires request IDs to increase across console owners.
    # Match the deployed tools' millisecond seed; exhaustion ends the segment.
    request = int(time.time() * 1000) & 0xffffffff
    timeout_streak = 0
    identity_at = 0

    def record(kind, **values):
        journal.write((json.dumps(dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                         host_mono_ns=time.monotonic_ns(), kind=kind, **values)) + '\n').encode())

    def pump(wait=0.02):
        nonlocal buffer, latest
        if stop is not None and stop.is_set():
            raise InterruptedError('capture stop requested')
        if fd is None or not select.select([fd], [], [], max(0, wait))[0]:
            return
        data = os.read(fd, 8192)
        if not data:
            raise OSError('USB disconnected; segment ends without reconnect')
        raw.write(data)
        buffer += data
        while b'\n' in buffer:
            line, buffer = buffer.split(b'\n', 1)
            decoded = line.decode('ascii', errors='replace')
            record('line', line=decoded)
            try:
                result = frames.feed(decoded)
            except (ValueError, UnicodeError) as e:
                state['refused_frames'] += 1
                record('frame_refused', error=str(e))
                continue
            if result:
                latest = result
                record('frame', request=result[0], payload=result[1])
        if len(buffer) > 8192:
            raise OSError('unterminated serial record')

    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as scanner:
        def fresh(opened):
            nonlocal identity_at
            future = scanner.submit(resolve, config['usb_serial'], os.getpid() if opened else None)
            while not future.done():
                if time.monotonic() >= deadline:
                    raise TimeoutError('capture deadline during identity scan')
                if opened:
                    pump()
                else:
                    concurrent.futures.wait([future], timeout=.02)
            port = future.result()
            if opened and port != state['port']:
                raise OSError('USB mapping changed')
            identity_at=time.monotonic()
            return port

        def query(op):
            nonlocal request, boot, latest, timeout_streak
            # Exact allowlist: these variants never alter service/profile/parent.
            if op not in ('status', 'clock', 'precision', 'network', 'join', 'rc-parent') and not re.fullmatch(r'events [0-9a-f]{8}', op):
                raise ValueError('non-read-only query refused')
            # Keep the same exclusive FD and unit lock. Responses stay bound
            # to source/boot; refresh OS identity/ownership every five seconds.
            if not boot or time.monotonic()-identity_at>=5:fresh(True)
            request += 1
            if request > 0xffffffff:
                raise OverflowError('request exhausted')
            command = f'r2 {request:08x} {config["silicon_id"]} {config["source"]} {boot:016x} {op}\n'.encode()
            latest = None
            if os.write(fd, command) != len(command):
                raise OSError('partial command write')
            state['queries'] += 1
            record('query', request=request, op=op,command=command.decode().strip())
            until = min(deadline, time.monotonic() + 2.5)
            while time.monotonic() < until:
                pump()
                if latest is None or latest[0] != request:
                    continue
                value = latest[1]
                if value.get('source') != config['source']:
                    raise ValueError('source mismatch')
                observed = int(value.get('boot', '0'), 16)
                if not observed or (boot and boot != observed):
                    raise ValueError('boot changed; start a new capture segment')
                if op == 'status' and any(value.get(k) != config[v] for k, v in [('silicon', 'silicon_id'), ('commit', 'commit')]):
                    raise ValueError('runtime identity mismatch')
                if op == 'status' and value.get('unit') != unit:
                    raise ValueError('unit mismatch')
                boot = observed
                timeout_streak = 0
                return value
            state['timeouts'] += 1
            record('query_timeout', request=request, op=op)
            timeout_streak += 1
            if timeout_streak >= 3:
                raise TimeoutError('three missing bound replies; end segment without reconnect')
            return None

        try:
            lock = open('/tmp/ptob-lab-' + unit + '.lock', 'a')
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            state['port'] = fresh(False)
            fd = os.open(state['port'], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            fcntl.ioctl(fd, termios.TIOCEXCL)
            tty.setraw(fd, termios.TCSANOW)
            attrs = termios.tcgetattr(fd)
            attrs[4] = attrs[5] = termios.B115200
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            first = query('status')
            while first is None and time.monotonic()<deadline-7:
                first=query('status')
            if first is None:
                raise TimeoutError('no fresh bound status')
            state.update(boot=f'{boot:016x}', first=first)
            version = tuple(map(int, first['version'].split('.')))
            cursor = 0
            snapshots_due=events_due=0
            while time.monotonic() < deadline - 7:
                now=time.monotonic()
                if now>=snapshots_due:
                    for op in ['network', 'precision', 'rc-parent', 'clock', 'join']:
                        if time.monotonic() >= deadline - 7:break
                        query(op)
                    snapshots_due=now+snapshot_interval
                if version >= (0, 15, 56) and now>=events_due:
                    for _ in range(32):
                        if time.monotonic() >= deadline - 7:
                            break
                        event = query(f'events {cursor:08x}')
                        if event is None or not event.get('found'):
                            break
                        sequence = event['sequence']
                        if sequence <= cursor:
                            raise ValueError('event cursor did not advance')
                        state['missing_events'] += event['missed']
                        cursor = sequence
                    events_due=time.monotonic()+5
                # Drain continuously between bounded polls; never sleep holding unread USB.
                until = min(deadline, time.monotonic() + .2)
                while time.monotonic() < until:
                    pump()
            while time.monotonic() < deadline:
                pump(min(.02, deadline-time.monotonic()))
            state['status'] = 'COMPLETE_WITH_GAPS' if any(state[k] for k in ['timeouts', 'refused_frames', 'missing_events']) else 'COMPLETE'
        except BaseException as e:
            state['error'] = repr(e)
            if isinstance(e, InterruptedError) and stop is not None and stop.is_set():
                state['status'] = 'STOPPED'
            record('capture_error', error=repr(e))
        finally:
            errors = []
            if buffer or frames.header:
                record('partial_tail', bytes=len(buffer), frame_pending=frames.header is not None)
            if fd is not None:
                try:
                    fcntl.ioctl(fd, termios.TIOCNXCL)
                except OSError as e:
                    errors.append(repr(e))
                try:
                    os.close(fd)
                except OSError as e:
                    errors.append(repr(e))
            for stream in [lock, raw, journal]:
                if stream is not None:
                    try:
                        stream.close()
                    except OSError as e:
                        errors.append(repr(e))
            state.update(elapsed_s=time.monotonic()-started, close_errors=errors, closed=not errors)
            if errors:
                state['status'] = 'INCOMPLETE'
            (folder / 'capture.json').write_text(json.dumps(state, indent=2) + '\n')
    return state


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', required=True, type=Path)
    p.add_argument('--unit', action='append', required=True)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--duration', type=int, default=3600)
    p.add_argument('--rotate-bytes', type=int, default=16*1024*1024)
    p.add_argument('--snapshot-interval',type=int,default=30)
    a = p.parse_args()
    if not 15 <= a.duration <= 14*86400 or a.rotate_bytes < 8192 or not 5<=a.snapshot_interval<=3600:
        p.error('duration must be 15s..14d and rotation at least 8192 bytes')
    config_bytes = a.config.read_bytes()
    devices = json.loads(config_bytes)['devices']
    validate_config(devices)
    if len(set(a.unit)) != len(a.unit) or any(u not in devices for u in a.unit):
        p.error('units must be distinct configured names')
    root = Path(__file__).resolve().parents[3] / 'artifacts'
    out = a.out.resolve()
    if root not in out.parents:
        p.error('raw console output must stay in ignored repository artifacts/')
    os.umask(0o077)
    out.mkdir(parents=True, exist_ok=False, mode=0o700)
    (out / 'run.json').write_text(json.dumps(dict(config_sha256=hashlib.sha256(config_bytes).hexdigest(), units=a.unit,
          duration_s=a.duration, rotate_bytes=a.rotate_bytes,snapshot_interval_s=a.snapshot_interval,
          identity_interval_s=5,event_poll_interval_s=5,UTC_accuracy='UNKNOWN',
          collection='READ_ONLY_SOFTWARE_DIAGNOSTICS', cross_device_clock='HOST_RECEIVE_TIME_ONLY'), indent=2)+'\n')
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(a.unit)) as workers:
        jobs = [workers.submit(capture, u, devices[u], out, a.duration, a.rotate_bytes,None,a.snapshot_interval) for u in a.unit]
        results = [j.result() for j in jobs]
    print(json.dumps([dict(unit=r['unit'], status=r['status'], closed=r['closed']) for r in results]))
    return 0 if all(r['status'] == 'COMPLETE' and r['closed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
