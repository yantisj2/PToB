"""Clock ownership, timescale and fail-closed policy regression checks. MIT."""
import copy
from contextlib import ExitStack
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('clock_chain', Path(__file__).parents[1]/'tools/clock_chain.py')
chain = importlib.util.module_from_spec(spec)
spec.loader.exec_module(chain)


class ClockChainTests(unittest.TestCase):
    def setUp(self):
        self.now = 100.0
        self.status = dict(observed_boottime_s=100.0, chain=dict(complete=True),
                           tracking=dict(leap='Normal'), source_status=dict(current=True,
                           boot='hat-a', root='root-a', root_boot=1, epoch=2, parent='parent-a'))
        self.transfer = dict(observed_boottime_s=100.0, running=True, tai_offset_s=37,
                             device='/dev/ptp2', generation=1)
        self.capture = dict(offset_ns=100, bracket_ns=1000)

    def reason(self):
        return chain.serving_reason(self.status, self.transfer, self.capture, 37, '/dev/ptp2', self.now)

    def test_unknown_or_failed_evidence_withdraws(self):
        self.assertIsNone(self.reason())
        cases = [('status', 'observed_boottime_s', 96), ('status', 'observed_boottime_s', 101),
                 ('status', 'observed_boottime_s', float('nan')),
                 ('transfer', 'observed_boottime_s', 97), ('transfer', 'running', False),
                 ('transfer', 'tai_offset_s', 36), ('transfer', 'device', '/dev/ptp1'),
                 ('capture', 'offset_ns', 50001), ('capture', 'offset_ns', -50001),
                 ('capture', 'offset_ns', float('nan')), ('capture', 'bracket_ns', -1),
                 ('capture', 'bracket_ns', 100001)]
        for obj, key, value in cases:
            with self.subTest(obj=obj, key=key, value=value):
                target = getattr(self, obj); previous = target[key]
                target[key] = value
                self.assertIsNotNone(self.reason())
                target[key] = previous
        for obj, key in ((self.status['chain'], 'complete'),
                         (self.status['source_status'], 'current')):
            obj[key] = False
            self.assertIsNotNone(self.reason()); obj[key] = True
        for leap in ('Not synchronised', 'Insert second', 'Delete second', None):
            self.status['tracking']['leap'] = leap
            self.assertIsNotNone(self.reason())

    def test_source_and_transfer_recovery_requires_continuous_settle(self):
        readiness = chain.Readiness()
        token = chain.source_token(self.status, self.transfer, 37)
        self.assertFalse(readiness.update(None, token, 10)[0])
        self.assertTrue(readiness.update(None, token, 13)[0])
        for field in ('root', 'root_boot', 'epoch', 'parent', 'boot'):
            changed = copy.deepcopy(self.status)
            changed['source_status'][field] = 'changed'
            new_token = chain.source_token(changed, self.transfer, 37)
            self.assertFalse(readiness.update(None, new_token, 14)[0])
            self.assertTrue(readiness.update(None, new_token, 17)[0])
        self.assertFalse(readiness.update('lost admission', token, 18)[0])
        self.assertFalse(readiness.update(None, token, 20)[0])
        self.assertTrue(readiness.update(None, token, 23)[0])
        changed_transfer = dict(self.transfer, generation=2)
        self.assertFalse(readiness.update(None, chain.source_token(self.status, changed_transfer, 37), 24)[0])
        self.assertFalse(readiness.update(None, chain.source_token(self.status, changed_transfer, 38), 28)[0])

    def test_clock_read_preserves_epoch_integer_precision_and_bracket(self):
        epoch = 1_800_000_000*chain.NS
        with patch.object(chain.time, 'time_ns', side_effect=(epoch, epoch+1000)), \
             patch.object(chain.time, 'clock_gettime_ns', return_value=epoch+37*chain.NS+550):
            result = chain.bracket(123, 37)
        self.assertEqual(result['offset_ns'], 50)
        self.assertEqual(result['bracket_ns'], 1000)
        for delta in (-1, 100001):
            with patch.object(chain.time, 'time_ns', side_effect=(epoch, epoch+delta)), \
                 patch.object(chain.time, 'clock_gettime_ns', return_value=epoch):
                with self.assertRaises(ValueError): chain.bracket(123)

    def test_kernel_tai_unknown_is_refused_without_hardcoded_fallback(self):
        with patch.object(chain.time, 'CLOCK_TAI', 11, create=True):
            for seconds in (0, 9, 101, 37.5):
                with patch.object(chain, 'bracket', return_value=dict(offset_ns=int(seconds*chain.NS))):
                    with self.assertRaises(ValueError): chain.tai_offset()
            with patch.object(chain, 'bracket', return_value=dict(offset_ns=38*chain.NS+500)):
                self.assertEqual(chain.tai_offset(), 38)

    def test_transfer_restarts_child_on_kernel_offset_change_or_child_death(self):
        class Child:
            def __init__(self): self.dead = False
            def poll(self): return 1 if self.dead else None
            def terminate(self): self.dead = True
            def wait(self, timeout): return 0
        children, commands, handlers, rows = [], [], {}, []
        def start(argv):
            commands.append(argv); child = Child(); children.append(child); return child
        turns = [0]
        def sleep(delay):
            turns[0] += 1
            if turns[0] == 1: children[0].dead = True
            if turns[0] == 3: handlers[chain.signal.SIGTERM](None, None)
        with tempfile.TemporaryDirectory() as temp, ExitStack() as stack:
            stack.enter_context(patch.object(chain, 'STATE', Path(temp)))
            stack.enter_context(patch.object(chain, 'phc_device', return_value='/dev/ptp2'))
            stack.enter_context(patch.object(chain, 'tai_offset', side_effect=(37, 37, 38)))
            stack.enter_context(patch.object(chain, 'phc_observation', return_value=self.capture))
            stack.enter_context(patch.object(chain, 'boottime', return_value=100))
            stack.enter_context(patch.object(chain, 'notify'))
            stack.enter_context(patch.object(chain, 'atomic_json', side_effect=lambda p, r: rows.append(r)))
            stack.enter_context(patch.object(chain.signal, 'signal', side_effect=lambda sig, cb: handlers.update({sig:cb})))
            stack.enter_context(patch.object(chain.time, 'sleep', side_effect=sleep))
            stack.enter_context(patch.object(chain.subprocess, 'Popen', side_effect=start))
            stack.enter_context(patch.object(sys, 'argv', ['clock_chain.py', 'transfer']))
            chain.main()
        self.assertEqual(len(children), 3)
        self.assertTrue(all(c.dead for c in children))
        self.assertEqual([cmd[cmd.index('-O')+1] for cmd in commands], ['37', '37', '38'])
        self.assertTrue(all('-w' not in cmd and cmd[cmd.index('-s')+1] == 'CLOCK_REALTIME' for cmd in commands))
        self.assertFalse(rows[-1]['running'])

    def test_publication_after_loop_start_is_not_false_future(self):
        handlers, rows = {}, []
        self.status['observed_boottime_s'] = 100.1
        self.transfer['observed_boottime_s'] = 100.1
        def sleep(delay): handlers[chain.signal.SIGTERM](None, None)
        with tempfile.TemporaryDirectory() as temp, ExitStack() as stack:
            stack.enter_context(patch.object(chain, 'STATE', Path(temp)))
            stack.enter_context(patch.object(chain, 'phc_device', return_value='/dev/ptp2'))
            stack.enter_context(patch.object(chain, 'tai_offset', return_value=37))
            stack.enter_context(patch.object(chain, 'phc_observation', return_value=self.capture))
            stack.enter_context(patch.object(chain, 'read_json', side_effect=(self.status, self.transfer)))
            stack.enter_context(patch.object(chain, 'boottime', side_effect=(100.0, 100.2, 100.3)))
            stack.enter_context(patch.object(chain, 'notify'))
            stack.enter_context(patch.object(chain, 'atomic_json', side_effect=lambda p, r: rows.append(r)))
            stack.enter_context(patch.object(chain.signal, 'signal', side_effect=lambda sig, cb: handlers.update({sig:cb})))
            stack.enter_context(patch.object(chain.time, 'sleep', side_effect=sleep))
            stack.enter_context(patch.object(sys, 'argv', ['clock_chain.py', 'serve']))
            chain.main()
        self.assertEqual(rows[0]['reason'], 'settling after source/transfer change')
        self.assertFalse(rows[0]['ready'])


if __name__ == '__main__':
    unittest.main()
