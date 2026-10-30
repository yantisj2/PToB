#!/usr/bin/env python3
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""Exact-device CoreSE nonce/boot/build/CRC console. Import does not open hardware."""
import argparse, fcntl, hashlib, json, re, secrets, subprocess, sys, termios, time, zlib, os, select, shutil
from pathlib import Path
from serial.tools import list_ports

def require(ok, reason='Invalid or incomplete CoreSE response'):
    if not ok:
        raise RuntimeError(reason)

def fields(line):
    p = line.split()
    out = {}
    for s in p[1:]:
        (k, v) = s.split('=', 1)
        require(k not in out and k and v, 'Invalid CoreSE response or device binding')
        out[k] = v
    return out

class Port:

    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        a = termios.tcgetattr(self.fd)
        a[0] = a[1] = a[3] = 0
        a[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
        a[4] = a[5] = termios.B115200
        a[6][termios.VMIN] = a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)

    def fileno(self):
        return self.fd

    def read(self, n):
        return os.read(self.fd, n) if select.select([self.fd], [], [], 0.02)[0] else b''

    def write(self, data):
        return os.write(self.fd, data)

    def close(self):
        os.close(self.fd)

class Client:

    def __init__(self, out, port, log):
        self.out = Path(out)
        self.r = json.loads((self.out / 'receipt.json').read_text())
        self.session = None
        self.frame = None
        self.buffer = b''
        lock_path = Path('/private/tmp' if sys.platform == 'darwin' else '/tmp') / ('ptob-corese-' + hashlib.sha256(port.encode()).hexdigest()[:16] + '.lock')
        self.lock = lock_path.open('a')
        os.chmod(lock_path, 384)
        fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        p = next((p for p in list_ports.comports() if p.device == port))
        require((p.vid, p.pid, (p.serial_number or '').upper()) == (12346, 4097, self.r['factory_mac'].upper()), 'USB serial/factory mismatch')
        require(not subprocess.run([shutil.which('lsof') or '/usr/sbin/lsof', p.device], capture_output=True).stdout, 'Invalid CoreSE response or device binding')
        self.port = Port(p.device)
        fcntl.ioctl(self.port.fileno(), termios.TIOCEXCL)
        self.startup_retries = 0
        self.log = Path(log).open('ab')

    def close(self):
        try:
            fcntl.ioctl(self.port.fileno(), termios.TIOCNXCL)
        except OSError as error:
            if error.errno not in (6,19):
                raise
        finally:
            self.port.close()
            self.log.close()
            self.lock.close()

    def request(self, action=None):
        nonce = secrets.token_hex(8)
        if action:
            require(re.fullmatch('(ui page [0-8]|ui render [0-8]|ui light|ui brightness (?:auto|[0-9]{2,3})|ui snap|ui capture|ui pixels [0-9]{1,5}|fine read 0|rc9 diag|mesh diag|shadow diag|prepare_update)', action), 'Bounded command only')
            if action.startswith('ui brightness ') and action!='ui brightness auto':require(32<=int(action.split()[-1])<=255,'Brightness must be32..255')
            if self.frame is None or time.monotonic() - self.received_at + (int(self.frame['end']['completed_us']) - int(self.frame['begin']['request_us'])) / 1000000.0 > 1.5:
                self.request()
            b = self.frame['begin']
            command = f"v12 do {b['ticket']} {nonce} {b['factory']} {b['session']} {action}\n"
        else:
            command = f'v12 status {nonce}\n'
        self.port.write(command.encode())
        deadline = time.monotonic() + 3
        raw = None
        rows = []
        begin = None
        while time.monotonic() < deadline:
            data = self.port.read(32768)
            if data:
                self.log.write(data)
                self.log.flush()
                self.buffer += data
            require(len(self.buffer) < 65536, 'Invalid CoreSE response or device binding')
            while b'\n' in self.buffer:
                (line, self.buffer) = self.buffer.split(b'\n', 1)
                if line.startswith(b'W12_V12_BEGIN '):
                    b = fields(line.decode())
                    if b.get('nonce') != nonce:
                        continue
                    require(b['factory'] == self.r['factory_mac'] and b['source'] == self.r['public_commit'] and (b['build'] == self.r['runtime_identity']['build_id']), 'Invalid CoreSE response or device binding')
                    require(b['version'] == '3' and re.fullmatch('[0-9a-f]{16}', b['session']) and int(b['session'], 16), 'Invalid CoreSE response or device binding')
                    require(self.session is None or self.session == b['session'], 'Device rebooted')
                    require(int(b['deadline_us']) - int(b['request_us']) == 1500000 and int(b['ticket_deadline_us']) - int(b['request_us']) == 2000000, 'Invalid CoreSE response or device binding')
                    begin = b
                    raw = b'\n' + line + b'\n'
                    rows = []
                elif begin and line.startswith(b'W12_V12_END '):
                    e = fields(line.decode())
                    require(e['nonce'] == nonce and e['ticket'] == begin['ticket'], 'Invalid CoreSE response or device binding')
                    require(e['crc32'] == f'{zlib.crc32(raw) & 4294967295:08x}', 'Invalid CoreSE response or device binding')
                    body = line.rsplit(b' end_crc32=', 1)[0]
                    require(e['end_crc32'] == f'{zlib.crc32(body, zlib.crc32(raw)) & 4294967295:08x}', 'Invalid CoreSE response or device binding')
                    require(int(e['rows']) == len(rows) and e['body_driver_drained'] == '1' and (e['counter_exhausted'] == '0'), 'Invalid CoreSE response or device binding')
                    require(e['request_us'] == begin['request_us'] and e['deadline_us'] == begin['deadline_us'] and (int(begin['request_us']) <= int(e['completed_us']) < int(e['deadline_us'])), 'Invalid CoreSE response or device binding')
                    require(len(raw) + len(line) + 1 <= 8704, 'Invalid CoreSE response or device binding')
                    for k in ('dropped', 'truncated', 'isr_refused', 'write_errors', 'expired', 'frame_drops'):
                        require(int(e[k]) == int(e['req_' + k]), 'Loss during requested response: ' + k)
                    names = [s.split(' ', 1)[0] for s in rows]
                    require(len(names) == len(set(names)), 'Invalid CoreSE response or device binding')
                    if len(rows) == 1:
                        permitted = ('CORESE_LIGHT',) if action == 'ui light' else ('CORESE_SHADOW',) if action == 'shadow diag' else ('CORESE_MESH',) if action == 'mesh diag' else ('CORESE_SNAPSHOT',) if action in ('ui snap','ui capture') or action and action.startswith('ui render ') else ('CORESE_PIXELS','CORESE_SNAPSHOT') if action and action.startswith('ui pixels ') else ()
                        require(names[0] in permitted, 'Unexpected diagnostic reply type')
                    else:
                        standard = ('W12_R2_STATUS','W12_V12_STATUS','W12_V12_GPS','W12_V12_LORA_STATUS','W12_V12_PUBLIC','W12_V12_CONSOLE','W12_V12_DIRECT','W12_V12_DIAGNOSTIC_TX','W12_V12_COMMAND','N30_UI')
                        fine = ('W12_R2_STATUS','W12_V12_STATUS','N30_FINE_STATUS',*(f'N30_FINE_EDGE{i}' for i in range(4)),'N30_FINE_CONTRACT','W12_V12_COMMAND','N30_UI')
                        rc9 = ('W12_R2_STATUS','W12_V12_STATUS',*(f'N30_RC9_{n}' for n in ('STATUS','ORIGIN','QUALITY','COUNTERS','LEASE','CONTRACT')),'W12_V12_COMMAND','N30_UI')
                        expected = fine if action == 'fine read 0' else rc9 if action == 'rc9 diag' else standard
                        require(tuple(names) == expected, 'Missing or reordered reply rows')
                        command_row=fields(rows[-2]);require(command_row.get('nonce')==nonce and command_row.get('accepted')=='1','Command was refused or not bound to request')
                        mesh = fields(rows[0])
                        require(names[0] == 'W12_R2_STATUS' and mesh['client_identity'] == self.r['device_id'] and (mesh['serving'] == '0') and (mesh['source'] == '0') and (mesh['client_control'] == '0') and (mesh['client_capabilities'] == '0'), 'Invalid CoreSE response or device binding')
                        require(mesh['enrollment_sha'] == self.r['runtime_identity']['r2_enrollment_sha256'], 'Invalid CoreSE response or device binding')
                    self.session = begin['session']
                    self.frame = {'begin': begin, 'end': e, 'rows': {n: fields(s) for (n, s) in zip(names, rows)}}
                    self.received_at = time.monotonic()
                    return self.frame
                elif begin and line:
                    rows.append(line.decode('ascii'))
                    raw += line + b'\n'
        if action is None and self.session is None and (self.startup_retries < 3):
            self.startup_retries += 1
            return self.request()
        raise TimeoutError('No complete fresh CoreSE response: ' + str(action))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True,help='Ignored immutable CoreSE build output')
    p.add_argument('--port',required=True,help='Exact reviewed USB device path')
    p.add_argument('--log',type=Path,required=True,help='Ignored raw response log')
    p.add_argument('--command',help='Bounded presentation/read diagnostic or explicit prepare_update')
    p.add_argument('--watch',type=float,default=0,help='Read status and fine health for this many seconds')
    a=p.parse_args()
    require(0<=a.watch<=3600,'Watch must be bounded to one hour')
    require(not(a.watch and a.command),'Choose one operation')
    require('artifacts' in a.log.resolve().parts,'Keep raw logs under ignored artifacts')
    c=Client(a.build,a.port,a.log)
    try:
        if not a.watch:
            print(json.dumps(c.request(a.command),indent=2))
        else:
            until=time.monotonic()+a.watch
            while time.monotonic()<until:
                status=c.request();fine=c.request('fine read 0')
                print(json.dumps({'status':status,'fine':fine}),flush=True)
                time.sleep(.9)
    finally:c.close()

if __name__=='__main__':main()
