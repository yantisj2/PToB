"""MIT. Copyright (c) 2026 Jonathan Yantis. Offline capture regressions."""
import importlib.util
import concurrent.futures
import errno
import json
import os
import subprocess
import select
import sys
import threading
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zlib

spec = importlib.util.spec_from_file_location('capture', Path(__file__).resolve().parents[1]/'tools/console_capture.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def frame(sequence=1, payload=None):
    payload = json.dumps({'boot': '0000000000000001'} if payload is None else payload)
    return [f'R2 00000001 {sequence:08x} {len(payload)} {zlib.crc32(payload.encode()):08x}',
            payload, f'R2 END 00000001 {sequence:08x}']


class CaptureTest(unittest.TestCase):
    def test_owned_callout_with_unopened_tty_rc1(self):
        tree='+-o ExactDevice\n  "USB Serial Number" = "fixture"\n  "IOCalloutDevice" = "/dev/cu.fixture"\n'
        for code in (0,1):
            with patch.object(m.subprocess,'check_output',return_value=tree),patch.object(m.subprocess,'run',return_value=subprocess.CompletedProcess([],code,'p18134\nf4\n','')) as scan:
                self.assertEqual(m.resolve('fixture',18134),'/dev/cu.fixture')
                self.assertEqual(scan.call_args.args[0][-2:],['/dev/cu.fixture','/dev/tty.fixture'])

    def test_owned_alias_foreign_ambiguous_and_unknown_rejected(self):
        tree='+-o ExactDevice\n  "USB Serial Number" = "exact"\n  "IOCalloutDevice" = "/dev/cu.exact"\n'
        for code,stdout,stderr in ((1,'p999\nf4\n',''),(0,'p18134\np999\nf4\n',''),(1,'p18134\np999\nf4\n',''),(1,'',''),(2,'p18134\nf4\n',''),(1,'p18134\nf4\n','ownership warning')):
            with self.subTest(code=code,stdout=stdout,stderr=stderr),patch.object(m.subprocess,'check_output',return_value=tree),patch.object(m.subprocess,'run',return_value=subprocess.CompletedProcess([],code,stdout,stderr)):
                with self.assertRaises(OSError):m.resolve('exact',18134)
        # Initial free-port policy is unchanged and never accepts an owner.
        for code,stdout,allowed in ((1,'',True),(0,'',False),(1,'p18134\nf4\n',False)):
            with patch.object(m.subprocess,'check_output',return_value=tree),patch.object(m.subprocess,'run',return_value=subprocess.CompletedProcess([],code,stdout,'')):
                if allowed:self.assertEqual(m.resolve('exact'),'/dev/cu.exact')
                else:
                    with self.assertRaises(OSError):m.resolve('exact')

    def ownership(self,rc,stdout='',stderr='',expected_pid=17):
        from types import SimpleNamespace
        tree='+-o USB\n"USB Serial Number" = "fixture"\n"IOCalloutDevice" = "/dev/cu.fixture"\n'
        with patch.object(m.subprocess,'check_output',return_value=tree), patch.object(m.subprocess,'run',return_value=SimpleNamespace(returncode=rc,stdout=stdout,stderr=stderr)):
            return m.resolve('fixture',expected_pid)

    def test_ownership_evidence_foreign_and_inspection_failure(self):
        for rc,stdout,stderr,category in [(0,'p99\n','','foreign_owner'),(1,'','','ownership_unproven'),(2,'','','inspection_failure'),(0,'p17\n','denied','inspection_failure'),(0,'garbage','','inspection_failure')]:
            with self.subTest(category=category),self.assertRaises(m.OwnershipError) as raised:
                self.ownership(rc,stdout,stderr)
            e=raised.exception.evidence
            self.assertEqual((e['classification'],e['returncode'],e['stdout'],e['stderr']),(category,rc,stdout,stderr))
            self.assertEqual(e['aliases'],['/dev/cu.fixture','/dev/tty.fixture'])
            self.assertEqual(e['expected_pid'],17)

    def test_ownership_rc1_own_pid_and_unowned_initial(self):
        self.assertEqual(self.ownership(1,'p17\n'),'/dev/cu.fixture')
        self.assertEqual(self.ownership(0,'p17\n'),'/dev/cu.fixture')
        self.assertEqual(self.ownership(1,expected_pid=None),'/dev/cu.fixture')
        with self.assertRaises(m.OwnershipError):self.ownership(0,expected_pid=None)

    def test_ownership_output_bounded(self):
        with self.assertRaises(m.OwnershipError) as raised:self.ownership(2,'x'*5000,'e'*6000)
        e=raised.exception.evidence
        self.assertEqual((len(e['stdout']),len(e['stderr'])),(4096,4096))
        self.assertTrue(e['stdout_truncated'] and e['stderr_truncated'])

    def test_inspection_wait_and_commands_share_original_deadline(self):
        tree='+-o USB\n"USB Serial Number" = "fixture"\n"IOCalloutDevice" = "/dev/cu.fixture"\n'
        from unittest.mock import Mock
        lock=Mock();lock.acquire.return_value=True
        with patch.object(m,'_IDENTITY_SCAN_LOCK',lock),patch.object(m.time,'monotonic',side_effect=[100,100,102,105]),patch.object(m.subprocess,'check_output',return_value=tree) as ioreg,patch.object(m.subprocess,'run',return_value=subprocess.CompletedProcess([],1,'p17\n','')) as lsof:
            self.assertEqual(m.resolve('fixture',17),'/dev/cu.fixture')
        lock.acquire.assert_called_once_with(timeout=8)
        lock.release.assert_called_once_with()
        self.assertEqual(ioreg.call_args.kwargs['timeout'],6)
        self.assertEqual(lsof.call_args.kwargs['timeout'],3)

    def test_wait_timeout_never_launches_inspector_or_releases_unowned_lock(self):
        from unittest.mock import Mock
        lock=Mock();lock.acquire.return_value=False
        with patch.object(m,'_IDENTITY_SCAN_LOCK',lock),patch.object(m.time,'monotonic',side_effect=[100,103]),patch.object(m.subprocess,'check_output') as ioreg,patch.object(m.subprocess,'run') as lsof:
            with self.assertRaisesRegex(TimeoutError,'waiting for inspector'):m.resolve('fixture',17)
        lock.acquire.assert_called_once_with(timeout=5)
        lock.release.assert_not_called();ioreg.assert_not_called();lsof.assert_not_called()

    def test_mapping_exhaustion_does_not_start_another_eight_second_scan(self):
        tree='+-o USB\n"USB Serial Number" = "fixture"\n"IOCalloutDevice" = "/dev/cu.fixture"\n'
        with patch.object(m.time,'monotonic',side_effect=[100,100,101,109]),patch.object(m.subprocess,'check_output',return_value=tree) as ioreg,patch.object(m.subprocess,'run') as lsof:
            with self.assertRaisesRegex(TimeoutError,'identity scan deadline'):m.resolve('fixture',17)
        self.assertEqual(ioreg.call_args.kwargs['timeout'],7)
        lsof.assert_not_called()
        self.assertTrue(m._IDENTITY_SCAN_LOCK.acquire(blocking=False))
        m._IDENTITY_SCAN_LOCK.release()

    def test_lock_covers_ownership_command_as_well_as_USB_mapping(self):
        tree='+-o USB\n"USB Serial Number" = "fixture"\n"IOCalloutDevice" = "/dev/cu.fixture"\n'
        first_ident=[None];lsof_entered=threading.Event();release=threading.Event()
        second_started=threading.Event();second_ioreg=threading.Event()
        def ioreg(*args,**kwargs):
            if threading.get_ident()!=first_ident[0]:second_ioreg.set()
            return tree
        def lsof(*args,**kwargs):
            if threading.get_ident()==first_ident[0]:
                lsof_entered.set();self.assertTrue(release.wait(2))
            return subprocess.CompletedProcess([],1,'p17\n','')
        def first():
            first_ident[0]=threading.get_ident();return m.resolve('fixture',17)
        def second():
            second_started.set();return m.resolve('fixture',17)
        with patch.object(m.subprocess,'check_output',side_effect=ioreg),patch.object(m.subprocess,'run',side_effect=lsof),concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            a=pool.submit(first)
            try:
                self.assertTrue(lsof_entered.wait(2))
                b=pool.submit(second);self.assertTrue(second_started.wait(2))
                self.assertFalse(second_ioreg.wait(.15))
            finally:release.set()
            self.assertEqual(a.result(timeout=2),'/dev/cu.fixture')
            self.assertEqual(b.result(timeout=2),'/dev/cu.fixture')

    def held_pty_inspector(self, external):
        """Real lsof, own PTY, and a deliberately prolonged fork/exec window."""
        master,slave=os.openpty();port=os.ttyname(slave)
        ready_r,ready_w=os.pipe();release_r,release_w=os.pipe()
        tree='+-o PTYFixture\n"USB Serial Number" = "fixture"\n"IOCalloutDevice" = "'+port+'"\n'
        second_entered=threading.Event();second_started=threading.Event();trace=[]
        def preexec():
            # Test-only: prolong fork inheritance before close_fds/exec. The
            # actual inspector never uses preexec_fn or touches this PTY.
            os.write(ready_w,b'1');os.read(release_r,1)
        def held_spawn():
            with subprocess.Popen(['/usr/bin/true'],preexec_fn=preexec) as child:
                self.assertEqual(child.wait(timeout=4),0)
        first_ident=[None]
        def ioreg(*args,**kwargs):
            first=threading.get_ident()==first_ident[0]
            trace.append('first-enter' if first else 'second-enter')
            if first and not external:held_spawn()
            else:second_entered.set()
            trace.append('first-exit' if first else 'second-exit')
            return tree
        def first():
            first_ident[0]=threading.get_ident()
            return held_spawn() if external else m.resolve('fixture',os.getpid())
        def second():
            second_started.set();return m.resolve('fixture',os.getpid())
        try:
            self.assertFalse(os.get_inheritable(slave))
            with patch.object(m.subprocess,'check_output',side_effect=ioreg),concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                first_job=pool.submit(first)
                try:
                    self.assertTrue(select.select([ready_r],[],[],3)[0])
                    self.assertEqual(os.read(ready_r,1),b'1')
                    second_job=pool.submit(second)
                    self.assertTrue(second_started.wait(2))
                    if external:
                        with self.assertRaises(m.OwnershipError) as raised:second_job.result(timeout=3)
                        evidence=raised.exception.evidence
                        self.assertEqual(evidence['classification'],'foreign_owner')
                        self.assertEqual(len(evidence['pids']),2)
                        self.assertIn(str(os.getpid()),evidence['pids'])
                        # The inherited child still has the parent's same FD.
                        self.assertEqual(evidence['stdout'].count('f'+str(slave)+'\n'),2)
                    else:
                        self.assertFalse(second_entered.wait(.15))
                finally:
                    os.write(release_w,b'1')
                self.assertEqual(first_job.result(timeout=3),None if external else port)
                if not external:
                    self.assertEqual(second_job.result(timeout=3),port)
                    self.assertLess(trace.index('first-exit'),trace.index('second-enter'))
        finally:
            for fd in (master,slave,ready_r,ready_w,release_r,release_w):os.close(fd)

    @unittest.skipUnless(sys.platform=='darwin' and Path('/usr/sbin/lsof').exists(),'macOS ownership inspector')
    def test_concurrent_inspectors_cannot_observe_each_others_fork_inheritance(self):
        self.held_pty_inspector(external=False)

    @unittest.skipUnless(sys.platform=='darwin' and Path('/usr/sbin/lsof').exists(),'macOS ownership inspector')
    def test_actual_foreign_pty_owner_is_still_rejected(self):
        self.held_pty_inspector(external=True)

    def test_first_fault_burst_uses_one_owner_once(self):
        f=m.FaultSnapshot();commands=[];events=[]
        f.observe(dict(late=7,storage_fail=0,missing_map=0,fault=False,error=0))
        self.assertIsNone(f.reason)
        f.observe(dict(late=8,storage_fail=0,missing_map=0,fault=False,error=0))
        def query(op,until):commands.append((op,until));return {'bound':True}
        f.collect(query,lambda kind,**kw:events.append((kind,kw)))
        f.observe(dict(late=9));f.collect(query,lambda *a,**k:None)
        self.assertEqual([x[0] for x in commands],['lora','status','clock'])
        self.assertEqual(len({x[1] for x in commands}),1)
        self.assertTrue(events[-1][1]['complete'])
        f.observe(dict(fault=True));f.collect(query,lambda *a,**k:None)
        self.assertEqual([x[0] for x in commands],['lora','status','clock']*2)
        f.observe(dict(fault=True));f.collect(query,lambda *a,**k:None)
        self.assertEqual(len(commands),6)

    def test_first_fault_burst_retains_partial_on_boot_failure(self):
        f=m.FaultSnapshot();f.observe(dict(fault=True));events=[];commands=[]
        def query(op,until):
            commands.append(op)
            if op=='status':raise ValueError('boot changed')
            return {'raw_fault':'saved'}
        with self.assertRaisesRegex(ValueError,'boot changed'):
            f.collect(query,lambda kind,**kw:events.append((kind,kw)))
        self.assertEqual(commands,['lora','status'])
        self.assertEqual(events[-1][1],dict(completed=['lora'],complete=False))
        self.assertEqual(f.done,{'terminal'})

    def test_detached_usb_release_still_closes_real_fd(self):
        fd, other = os.pipe()
        try:
            with patch.object(m.fcntl, 'ioctl', side_effect=OSError(errno.ENXIO, 'detached USB')):
                release_errors, close_errors = m.close_serial(fd)
            self.assertEqual(len(release_errors), 1)
            self.assertEqual(close_errors, [])
            with self.assertRaises(OSError) as raised:
                os.fstat(fd)
            self.assertEqual(raised.exception.errno, errno.EBADF)
        finally:
            os.close(other)

    def test_failed_close_remains_unknown_without_retry(self):
        fd, other = os.pipe()
        try:
            with patch.object(m.fcntl, 'ioctl'), patch.object(m.os, 'close', side_effect=OSError(errno.EINTR, 'unknown close')) as close:
                release_errors, close_errors = m.close_serial(fd)
                close.assert_called_once_with(fd)
            self.assertEqual(release_errors, [])
            self.assertEqual(len(close_errors), 1)
            os.fstat(fd)
        finally:
            os.close(fd)
            os.close(other)

    def test_crc_sequence_and_truncation(self):
        f = m.Frames()
        for line in frame():
            result = f.feed(line)
        self.assertEqual(result[0], 1)
        f.feed(frame()[0]);f.feed(frame()[1])
        with self.assertRaisesRegex(ValueError, 'regressed'):
            f.feed(frame()[2])
        f = m.Frames();f.feed(frame()[0]);f.feed('{}')
        with self.assertRaisesRegex(ValueError, 'CRC'):
            f.feed(frame()[2])
        f = m.Frames();f.feed(frame()[0])
        with self.assertRaisesRegex(ValueError, 'truncated'):
            f.feed(frame(2)[0])
        self.assertIsNone(f.feed(frame(2)[1]))
        self.assertEqual(f.feed(frame(2)[2])[0], 1)

    def test_noise_and_limits(self):
        f = m.Frames()
        self.assertIsNone(f.feed('#R2_TIMING {}'))
        with self.assertRaises(ValueError):
            f.feed('R2 00000001 00000001 9999 00000000')
        with self.assertRaises(ValueError):
            f.feed('x'*3073)
        for line in frame(payload=[]):
            if line.startswith('R2 END'):
                with self.assertRaises(ValueError):f.feed(line)
            else:f.feed(line)

    def test_rotation_retains_all_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory);writer = m.Rotating(p, 'raw', 8)
            for data in [b'12345', b'67890', b'abc']:
                writer.write(data)
            writer.close()
            self.assertEqual(b''.join(x.read_bytes() for x in sorted(p.iterdir())), b'1234567890abc')
            self.assertEqual(len(list(p.iterdir())), 2)

    def test_explicit_identities(self):
        c = dict(silicon_id='1'*16, source='2'*64, commit='3'*40, usb_serial='synthetic')
        m.validate_config({'CLOCK': c})
        for bad in [{'CLOCK': {**c, 'source': 'not-source'}}, {'../CLOCK': c}, {'A': c, 'B': c}]:
            with self.assertRaises(ValueError):m.validate_config(bad)


if __name__ == '__main__':
    unittest.main()
