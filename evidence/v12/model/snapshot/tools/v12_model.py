"""V12 proposal state machine and discrete-event transport model.

This is executable proposed behavior, not firmware/controller emulation. All RF,
clock maps, source fitness and work durations are declared synthetic inputs.
Time in the scheduler is integer microseconds; timing observations are ns.
"""
from __future__ import annotations

import dataclasses
import hashlib
import heapq
import hmac
import math
import random
import struct
from collections import Counter, deque
from typing import Callable

TC01, C1, TC03, C3 = 0, 2, 4, 5
G4, G5, G6, RELAY = 6, 9, 10, 11
CANDIDATES = (TC01, TC03, C3, G4, G5, G6)
MAX_GMS = 4
NAMES = {TC01: "TC-01", TC03: "TC-03", C3: "C-3", C1: "C-1",
         G4: "SYNTHETIC-GM4", G5: "SYNTHETIC-GM5", G6: "SYNTHETIC-GM6", RELAY:"SYNTHETIC-TIME-RELAY"}
KINDS = {name: i + 1 for i, name in enumerate(
    ("REPORT", "PREPARE", "READY", "COMMIT", "ACTIVE", "STOP", "STOPPED"))}
FORMAT = struct.Struct("<BBBBQQQQQBQIQQQQQIB")
DOMAIN = b"PTOB12C\x01"
FIXTURE_KEYS = {node: hashlib.sha256(f"PUBLIC V12 FIXTURE {node}".encode()).digest()[:16]
                for node in CANDIDATES}
MAX64 = (1 << 64) - 1


def mac64(data: bytes, key: bytes) -> bytes:
    """SipHash-2-4, compatible primitive with current MAC64; public test keys only."""
    mask = MAX64
    k0, k1 = struct.unpack("<QQ", key)
    v = [0x736f6d6570736575 ^ k0, 0x646f72616e646f6d ^ k1,
         0x6c7967656e657261 ^ k0, 0x7465646279746573 ^ k1]

    def rotate(x, n):
        return ((x << n) | (x >> (64 - n))) & mask

    def round_():
        v[0] = (v[0] + v[1]) & mask; v[1] = rotate(v[1], 13) ^ v[0]
        v[0] = rotate(v[0], 32)
        v[2] = (v[2] + v[3]) & mask; v[3] = rotate(v[3], 16) ^ v[2]
        v[0] = (v[0] + v[3]) & mask; v[3] = rotate(v[3], 21) ^ v[0]
        v[2] = (v[2] + v[1]) & mask; v[1] = rotate(v[1], 17) ^ v[2]
        v[2] = rotate(v[2], 32)

    end = len(data) // 8 * 8
    for offset in range(0, end, 8):
        m = int.from_bytes(data[offset:offset + 8], "little")
        v[3] ^= m
        round_(); round_()
        v[0] ^= m
    last = ((len(data) & 255) << 56) | int.from_bytes(data[end:], "little")
    v[3] ^= last
    round_(); round_()
    v[0] ^= last
    v[2] ^= 255
    for _ in range(4):
        round_()
    return struct.pack("<Q", v[0] ^ v[1] ^ v[2] ^ v[3])


@dataclasses.dataclass(frozen=True)
class Message:
    kind: str
    sender: int
    target: int
    boot: int
    coordinator_boot: int
    challenge: int
    sequence: int
    decision: int = 0
    root: int = 255
    root_boot: int = 0
    source_epoch: int = 0
    source_second: int = 0
    evidence_us: int = 0
    created_us: int = 0
    expires_us: int = 0
    permit_until_us: int = 0
    config_epoch: int = 1
    flags: int = 0

    def encode(self) -> bytes:
        if self.kind not in KINDS or self.sender not in CANDIDATES or self.target not in CANDIDATES:
            raise ValueError("unknown kind or participant")
        if self.root not in (*CANDIDATES, 255) or self.flags & ~3 or not self.config_epoch:
            raise ValueError("invalid root, flags or config")
        if not all(0 < x <= MAX64 for x in
                   (self.boot, self.coordinator_boot, self.challenge, self.sequence)):
            raise ValueError("zero/exhausted incarnation or sequence")
        if self.expires_us <= self.created_us or self.evidence_us > self.created_us:
            raise ValueError("invalid original time or deadline")
        try:
            body = FORMAT.pack(12, KINDS[self.kind], self.sender, self.target,
                               self.boot, self.coordinator_boot, self.challenge, self.sequence,
                               self.decision, self.root, self.root_boot, self.source_epoch,
                               self.source_second, self.evidence_us, self.created_us,
                               self.expires_us, self.permit_until_us, self.config_epoch, self.flags)
        except struct.error as e:
            raise ValueError("field range") from e
        return body + mac64(DOMAIN + body, FIXTURE_KEYS[self.sender])

    @classmethod
    def decode(cls, wire: bytes) -> Message:
        if len(wire) != FORMAT.size + 8:
            raise ValueError("message length")
        raw = FORMAT.unpack(wire[:-8])
        version, kind, sender, target = raw[:4]
        if version != 12 or kind not in KINDS.values() or sender not in FIXTURE_KEYS:
            raise ValueError("version, kind or identity")
        if not hmac.compare_digest(mac64(DOMAIN + wire[:-8], FIXTURE_KEYS[sender]), wire[-8:]):
            raise ValueError("MAC")
        m = cls(next(k for k, v in KINDS.items() if v == kind), sender, target, *raw[4:])
        if m.encode() != wire:
            raise ValueError("noncanonical message")
        return m


@dataclasses.dataclass(frozen=True)
class Profile:
    mode: str = "D"
    seconds: int = 120
    seed: int = 1
    clients: int = 1
    warm_start: bool = True
    pa_interval_us: int = 125000
    legacy_interval_us: int = 1000000
    history: int = 4
    discovery_interval_us: int = 250000
    discovery_phase_us: int = 5000
    scan_us: int = 400000
    scan_phase_us: int = 80000
    rx_guard_us: int = 500
    acl_interval_us: int = 30000
    acl_event_us: int = 1800
    acl_phase_us: int = 7000
    control_life_us: int = 250000
    control_queue: int = 8
    work_us: int = 1000
    loss: float = 0.0
    ack_loss: float = 0.0
    rf_loss: float = 0.0
    burst_start_us: int = 45000000
    burst_us: int = 0
    asymmetric: bool = False
    source_loss_start_us: int = 60000000
    source_loss_us: int = 0
    coordinator_reboot_us: int = 0
    root_reboot_us: int = 0
    lease_us: int = 3000000
    stop_delay_us: int = 2000
    stop_bound_us: int = 5000
    map_bound_us: int = 100
    map_error_us: int = 0
    map_known: bool = True
    stop_known: bool = True
    timer_ppm: float = 0.0
    timer_bound_ppm: float = 100.0
    clock_ppb: float = 10000.0
    drift_ppb_s: float = 0.0
    timestamp_noise_ns: float = 20.0
    timestamp_bias_ns: int = 0
    ambiguity_rate: float = 0.0
    logger_stall_us: int = 0
    logger_at_us: int = 30000000
    permit_pause_start_us: int = 0
    permit_pause_us: int = 0
    data_update_misses: bool = False
    discovery_air_us: int = 1000
    discovery_blackout_us: int = 0
    gm_count: int = 3
    indirect_gm: int = 255
    disconnect_node: int = 255
    disconnect_at_us: int = 0
    one_hop: bool = False
    far_clients: int = 1
    relay_phase_us: int = 62500
    relay_map_bias_ns: int = 0
    relay_guard_us: int = 500
    relay_fresh_us: int = 500000
    upstream_loss: float = 0.0
    downstream_loss: float = 0.0
    relay_reboot_us: int = 0
    relay_observer_stall_us: int = 0
    quiet_listen: bool = False
    ble_window_us: int = 400000
    quiet_run_choice: int = 0
    run_active_us: int = 250000
    two_windows: bool = False
    full_clients: int = 0
    observe_GMs: bool = False
    native_w12: bool = False
    held_limit_us: int = 1500000
    coarse_client: bool = False
    sync_timeout_us: int = 1000000
    precision_min_refs: int = 2
    precision_min_span_us: int = 0

    def validate(self):
        if self.mode not in "ABCD" or len(self.mode) != 1:
            raise ValueError("mode must be A, B, C or D")
        if not 10 <= self.seconds <= 86400 or not 1 <= self.clients <= 3:
            raise ValueError("duration/client count")
        if not 7500 <= self.pa_interval_us <= 81918750 or self.pa_interval_us % 1250:
            raise ValueError("periodic interval")
        if self.acl_interval_us < 7500 or self.acl_interval_us % 1250:
            raise ValueError("connection interval")
        if not 1 <= self.history <= 4 or not 1 <= self.control_queue <= 32:
            raise ValueError("bounded capacity")
        if not 0 <= self.scan_us <= 500000 or not 0 <= self.scan_phase_us < 1000000:
            raise ValueError("listening cap/phase")
        for name in ("loss", "ack_loss", "rf_loss", "ambiguity_rate"):
            if not 0 <= getattr(self, name) <= 1:
                raise ValueError(name)
        if not 2 <= self.gm_count <= 6 or not 0 <= self.far_clients <= self.clients:
            raise ValueError("GM population/far clients")
        if self.indirect_gm not in (*CANDIDATES,255) or self.indirect_gm == TC03:
            raise ValueError("indirect GM fixture")
        if self.disconnect_node not in (*CANDIDATES,255) or self.disconnect_node == TC03:
            raise ValueError("disconnect GM fixture")
        if not 0 <= self.upstream_loss <= 1 or not 0 <= self.downstream_loss <= 1:
            raise ValueError("hop loss")
        if self.one_hop and (self.mode not in "BD" or self.relay_phase_us >= self.pa_interval_us):
            raise ValueError("one hop requires periodic mode and bounded phase")
        if (self.quiet_listen or self.two_windows) and (self.mode not in "BD" or self.pa_interval_us not in (62500,125000) or
                self.quiet_run_choice not in (0,1) or not 300000<=self.ble_window_us<=450000 or
                self.run_active_us not in (250000,375000,500000)):
            raise ValueError("two-window profile requires62.5/125ms cadence and300-450ms BLE window")
        if not 0<=self.full_clients<=self.clients or not 500000<=self.held_limit_us<=5000000:
            raise ValueError("full client count/finite held horizon")
        if not 100000<=self.sync_timeout_us<=16000000:
            raise ValueError("periodic sync timeout")
        if not 2<=self.precision_min_refs<=32 or self.precision_min_span_us>3000000:
            raise ValueError("precision estimator readiness")
        if not 0 < self.lease_us <= 15000000 or not 0 < self.control_life_us < self.lease_us:
            raise ValueError("authority/deadline limit")
        if self.map_bound_us < 0 or self.stop_bound_us < 0 or self.timer_bound_ppm < 0:
            raise ValueError("negative bound")
        for f in dataclasses.fields(self):
            if f.name.endswith("_us") and f.name != "map_error_us" and getattr(self, f.name) < 0:
                raise ValueError(f.name)
        if not self.discovery_interval_us or not self.legacy_interval_us or not self.acl_event_us:
            raise ValueError("zero cadence/event")
        if self.timer_ppm <= -1000000:
            raise ValueError("nonpositive clock rate")


def union_length(intervals):
    if not intervals:
        return 0
    ordered = sorted(intervals)
    total = 0
    begin, end = ordered[0]
    for a, b in ordered[1:]:
        if a <= end:
            end = max(end, b)
        else:
            total += end - begin
            begin, end = a, b
    return total + end - begin


class Radio:
    """One half-duplex radio; union and listening caps include declared guards."""
    def __init__(self, capped=True):
        self.hard = []
        self.union = {}
        self.rx = {}
        self.reasons = Counter()
        self.capped = capped

    @staticmethod
    def pieces(start, end, width):
        while start < end:
            cycle = start // width
            stop = min(end, (cycle + 1) * width)
            yield cycle, (start - cycle * width, stop - cycle * width)
            start = stop

    def admit(self, start, end, receive, background=False):
        if start < 0 or end <= start:
            return False
        self.hard = [(a, b) for a, b in self.hard if b > start - 1000000]
        if not background and any(a < end and start < b for a, b in self.hard):
            self.reasons["half_duplex"] += 1
            return False
        for cycle, interval in self.pieces(start, end, 6000000):
            if self.capped and union_length(self.union.get(cycle, []) + [interval]) > 3000000:
                self.reasons["union_cap"] += 1
                return False
        if receive and self.capped:
            for cycle, interval in self.pieces(start, end, 1000000):
                if union_length(self.rx.get(cycle, []) + [interval]) > 500000:
                    self.reasons["rx_cap"] += 1
                    return False
        return True

    def reserve(self, start, end, receive, background=False):
        if not self.admit(start, end, receive, background):
            return False
        if not background:
            self.hard.append((start, end))
        for cycle, interval in self.pieces(start, end, 6000000):
            self.union.setdefault(cycle, []).append(interval)
        if receive:
            for cycle, interval in self.pieces(start, end, 1000000):
                self.rx.setdefault(cycle, []).append(interval)
        return True

    def metrics(self):
        return {"administrative_caps_enforced":self.capped,
                "max_union_us_per_6s": max(map(union_length, self.union.values()), default=0),
                "max_rx_us_per_1s": max(map(union_length, self.rx.values()), default=0),
                "ledger_total_union_us":sum(map(union_length,self.union.values())),
                "ledger_total_rx_us":sum(map(union_length,self.rx.values())),
                "refusals": dict(self.reasons)}


@dataclasses.dataclass
class Peer:
    boot: int = 1
    source_epoch: int = 1
    last_sequence: int = 0
    last_second: int = -1
    evidence_us: int = -6000001
    unique: int = 0
    ready: bool = False


@dataclasses.dataclass
class Permit:
    root: int
    root_boot: int
    source_epoch: int
    coordinator_boot: int
    decision: int
    until_us: int
    stop_at_us: int


class Clock:
    """Causal affine proposal estimator; synthetic output, never qualification."""
    def __init__(self,min_refs=2,min_span_us=0):
        self.rows = deque(maxlen=32)
        self.scope = None
        self.last_reference_us = -1
        self.last_available_us = -1
        self.last_output_ns = None
        self.rate = 1.0
        self.continuity_ns = 0.0
        self.fit_anchor = None
        self.min_refs=min_refs
        self.min_span_us=min_span_us

    def ready(self):
        return (len(self.rows)>=self.min_refs and
                self.rows[-1][0]-self.rows[0][0]>=self.min_span_us*1000)

    def ingest(self, scope, event, reference_us, available_us, local_ns, source_ns):
        if reference_us > available_us or reference_us <= self.last_reference_us:
            return False
        if scope != self.scope:
            self.rows.clear()
            self.fit_anchor = None
            self.scope = scope
        self.rows.append((local_ns, source_ns))
        self.last_reference_us = reference_us
        self.last_available_us = available_us
        if len(self.rows) >= 2:
            x0, y0 = self.rows[0]
            pairs = [(x - x0, y - y0) for x, y in self.rows]
            sx = sum(x for x, _ in pairs); sy = sum(y for _, y in pairs)
            n = len(pairs)
            denominator = n * sum(x*x for x, _ in pairs) - sx*sx
            if denominator:
                rate = (n * sum(x*y for x, y in pairs) - sx*sy) / denominator
                if not .9995 <= rate <= 1.0005:
                    self.rows.clear()
                    self.fit_anchor = None
                    return False
                self.rate = rate
                # Causal fitted intercept, rather than anchoring extrapolation
                # to the last noisy capture. Absolute bias is still retained.
                self.fit_anchor = (x0+sx/n, y0+sy/n)
        return True

    def predict(self, now_us, local_ns):
        if not self.ready() or now_us - self.last_reference_us >= 5000000:
            return None
        x, y = self.fit_anchor or self.rows[-1]
        value = y + (local_ns - x) * self.rate + self.continuity_ns
        if self.last_output_ns is not None and value < self.last_output_ns:
            self.continuity_ns += self.last_output_ns - value
            value = self.last_output_ns
        self.last_output_ns = value
        return value


class Receiver:
    def __init__(self, node, phase, min_refs=2,min_span_us=0):
        self.node = node
        self.phase = phase
        self.synced = None
        self.history = {}  # exact physical originals, never correction arrivals
        self.submitted = {}  # key -> original time, expired independently of repeats
        self.clock = Clock(min_refs,min_span_us)
        self.corrected = []
        self.errors = []
        self.counters = Counter()
        self.last_success_us = -1
        self.far = False
        self.path = None
        self.original_source_us = -1
        self.extension_permit = None
        self.origin_deadline_us = 0
        self.precision_updates = []
        self.quiet_errors = []
        self.always_online = False
        self.gm_observer = False
        self.window_updates = set()
        self.eligible_windows = set()
        self.delivery_states = Counter()


class Simulator:
    def __init__(self, cfg: Profile):
        cfg.validate()
        self.cfg = cfg
        self.now = 0
        self.end = cfg.seconds * 1000000
        self.events = []
        self.order = 0
        self.rng = random.Random(cfg.seed)
        self.trace = []
        self.counters = Counter()
        self.boots = {n: 1 for n in CANDIDATES}
        self.source_epochs = {n: 1 for n in CANDIDATES}
        self.recovery_at = {n: -70000000 if cfg.warm_start else 0 for n in CANDIDATES}
        self.coordinator = TC03
        # Stable authorized slots, including the coordinator. Overflow is never
        # promoted through traffic order or a relay; it is forced to client mode.
        self.requested = CANDIDATES[:cfg.gm_count]
        self.participants = tuple(n for n in self.requested[:MAX_GMS] if n != cfg.indirect_gm)
        self.direct = set(self.participants)
        self.forced_clients = set(self.requested) - self.direct
        self.coordinator_boot = 1
        self.challenges = {n: 100 + n for n in CANDIDATES}
        self.sequences = Counter()
        self.accepted_sequences = {}
        self.peers = {n: Peer() for n in CANDIDATES}
        self.radios = {n: Radio(not (cfg.native_w12 and n==C3)) for n in CANDIDATES}
        self.receivers = []
        for i in range(cfg.clients):
            node = C1 if i == 0 else 6 + i
            self.radios[node] = Radio(not (cfg.native_w12 and (not cfg.quiet_listen or i<cfg.full_clients)))
            receiver=Receiver(node, (cfg.scan_phase_us + i*271000) % 1000000,
                              cfg.precision_min_refs,cfg.precision_min_span_us)
            receiver.far = cfg.one_hop and i >= cfg.clients-cfg.far_clients
            receiver.always_online = i<cfg.full_clients
            self.receivers.append(receiver)
        self.relay = Receiver(RELAY, cfg.scan_phase_us,cfg.precision_min_refs,cfg.precision_min_span_us) if cfg.one_hop else None
        if self.relay:
            self.radios[RELAY] = Radio(not cfg.native_w12)
        self.relay_train = 0
        self.relay_scope = None
        self.relay_history = deque(maxlen=cfg.history)
        self.relay_events = 0
        self.relay_offers = []
        self.relay_trace = []
        self.relay_violations = []
        self.max_relay_source_age_us = 0
        for node in sorted(self.forced_clients):
            receiver=Receiver(node,(cfg.scan_phase_us+node*271000)%1000000,
                              cfg.precision_min_refs,cfg.precision_min_span_us)
            receiver.far=cfg.one_hop and node==cfg.indirect_gm
            self.receivers.append(receiver)
        self.gm_receivers=[]
        if cfg.observe_GMs:
            for node in self.participants:
                receiver=Receiver(node,cfg.scan_phase_us,cfg.precision_min_refs,cfg.precision_min_span_us)
                receiver.always_online=receiver.gm_observer=True
                self.gm_receivers.append(receiver)
        self.coarse_receipts=[]
        if cfg.coarse_client:
            self.radios[12]=Radio()
        self.queues = {(a, b): deque() for a in CANDIDATES for b in CANDIDATES if a != b}
        self.link_turn = Counter()
        self.permits = {}
        self.issued_until = 0
        self.pending_root = None
        self.commit_ack_until = 0
        self.uncertain = False
        self.preparing_root = None
        self.prepare_until = 0
        self.prepared = {}  # candidate's accepted PREPARE decision
        self.retired_decisions = Counter()
        self.active_root = None
        self.stopping = False
        self.decision = 0
        self.server_quiet_until = 0
        self.last_grant = -1000000
        self.train_epochs = Counter()
        self.train_started = {}
        self.sent_history = {n: deque(maxlen=cfg.history) for n in CANDIDATES}
        self.event_numbers = Counter()
        self.offers = Counter()
        self.offer_references = []
        self.output_grid = Counter()
        self.overlap = []
        self.late_stops = []
        self.api_assumptions = {"controller_stop_bound_us": cfg.stop_bound_us,
                                "clock_map_bound_us": cfg.map_bound_us,
                                "timer_rate_bound_ppm": cfg.timer_bound_ppm}
        self.call(0, 3, self.report_round)
        self.call(0, 0, self.tick)
        self.call(cfg.discovery_phase_us, 2, self.discovery)
        if cfg.mode in "CD":
            for node in self.participants:
                if node == TC03:
                    continue
                self.schedule_link(node, TC03, 0)
        if cfg.one_hop:
            self.call(cfg.relay_phase_us, 1, self.relay_publish)
        if cfg.relay_reboot_us:
            self.call(cfg.relay_reboot_us, -2, self.relay_reboot)
        if cfg.disconnect_at_us and cfg.disconnect_node != 255:
            self.call(cfg.disconnect_at_us, -2, lambda:self.disconnect(cfg.disconnect_node))
        self.call(100000, 4, self.score)
        if cfg.coordinator_reboot_us:
            self.call(cfg.coordinator_reboot_us, -2, lambda: self.reboot(TC03))
        if cfg.root_reboot_us:
            self.call(cfg.root_reboot_us, -2, lambda: self.reboot(TC01))
        if cfg.source_loss_us:
            self.call(cfg.source_loss_start_us, -2, self.source_loss)
            self.call(cfg.source_loss_start_us + cfg.source_loss_us, -2, self.source_return)

    def call(self, at, priority, callback: Callable):
        if at <= self.end:
            self.order += 1
            heapq.heappush(self.events, (max(at, self.now), priority, self.order, callback))

    def log(self, kind, **fields):
        self.trace.append({"at_us": self.now, "kind": kind, **fields})

    def blocked(self, sender, target, at):
        c = self.cfg
        return (c.burst_start_us <= at < c.burst_start_us + c.burst_us and
                (not c.asymmetric or sender == TC03))

    def source_ready(self, node):
        if node == TC01 and self.cfg.source_loss_start_us <= self.now < self.cfg.source_loss_start_us + self.cfg.source_loss_us:
            return False
        return self.now - self.recovery_at[node] >= 60000000

    def local_ns(self, receiver, at_us):
        c = self.cfg
        at_ns = at_us * 1000
        return (at_ns + 1000000 + int(at_ns*c.clock_ppb/1e9) +
                int(.5*c.drift_ppb_s*(at_us/1e6)**2))

    def message(self, kind, sender, target, **changes):
        self.sequences[sender] += 1
        if self.sequences[sender] > MAX64:
            raise ValueError("sequence exhausted: withdraw")
        values = dict(kind=kind, sender=sender, target=target, boot=self.boots[sender],
                      coordinator_boot=self.coordinator_boot,
                      challenge=self.challenges[target if sender == TC03 else sender],
                      sequence=self.sequences[sender], created_us=self.now,
                      expires_us=self.now + self.cfg.control_life_us)
        values.update(changes)
        return Message(**values)

    def send(self, m):
        wire = m.encode()
        if m.sender not in self.direct or m.target not in self.direct:
            self.counters["non_direct_or_overflow_control_refused"] += 1
            return
        if m.sender == m.target:
            self.receive(wire)
            return
        q = self.queues[m.sender, m.target]
        if len(q) >= self.cfg.control_queue:
            self.counters["queue_refused"] += 1
            return
        q.append([wire, 0])

    def schedule_link(self, sender, target, at):
        c = self.cfg
        phase = (c.acl_phase_us + (sender + target)*3700) % c.acl_interval_us
        slot = max(0, (at - phase + c.acl_interval_us - 1)//c.acl_interval_us)
        self.call(slot*c.acl_interval_us + phase, 2, lambda: self.link(sender, target))

    def link(self, sender, target):
        if sender not in self.direct or target not in self.direct:
            return
        # One connection event for the pair, including empty-event occupancy.
        # A whole record plus link ACK uses the declared event envelope. Direction
        # alternates when both queues have traffic; semantic ACKs are separate.
        pair = tuple(sorted((sender, target)))
        a, b = pair
        directions = [(a,b),(b,a)]
        if self.link_turn[pair] % 2:
            directions.reverse()
        self.link_turn[pair] += 1
        sender, target = next(((a,b) for a,b in directions if self.queues[a,b]), directions[0])
        q = self.queues[sender, target]
        stop = self.now + self.cfg.acl_event_us
        admitted = all(self.radios[n].admit(self.now, stop, True) for n in (sender, target))
        if admitted:
            for n in (sender, target):
                assert self.radios[n].reserve(self.now, stop, True)
            self.counters["ACL_events"] += 1
        else:
            self.counters["ACL_radio_deferred"] += 1
        if q:
            wire, attempts = q[0]
            m = Message.decode(wire)
            if self.now >= m.expires_us or attempts >= 16:
                q.popleft(); self.counters["message_expired"] += 1
            elif admitted:
                q[0][1] += 1
                self.counters["ACL_attempts"] += 1
                if not self.blocked(sender, target, self.now) and self.rng.random() >= self.cfg.loss:
                    available = stop + self.cfg.work_us
                    if self.cfg.logger_at_us <= available < self.cfg.logger_at_us + self.cfg.logger_stall_us:
                        available = self.cfg.logger_at_us + self.cfg.logger_stall_us
                    self.call(available, 1, lambda w=wire: self.receive(w))
                    if self.rng.random() >= self.cfg.ack_loss:
                        q.popleft()
                    else:
                        self.counters["lost_link_ACK"] += 1
                else:
                    self.counters["ACL_loss"] += 1
        self.schedule_link(*pair, self.now + self.cfg.acl_interval_us)

    def receive(self, wire):
        try:
            m = Message.decode(wire)
        except ValueError:
            self.counters["invalid_wire"] += 1
            return False
        if (m.coordinator_boot != self.coordinator_boot or m.boot != self.boots[m.sender] or
            m.challenge != self.challenges[m.target if m.sender == TC03 else m.sender] or
            m.config_epoch != 1 or self.now >= m.expires_us or m.created_us > self.now):
            self.counters["scope_or_age_refused"] += 1
            return False
        outbound = m.kind in ("PREPARE", "COMMIT", "STOP")
        if ((outbound and (m.sender != TC03 or m.root != m.target)) or
            (not outbound and m.target != TC03) or
            (m.kind in ("READY", "ACTIVE", "STOPPED") and m.root != m.sender)):
            self.counters["role_refused"] += 1
            return False
        if m.sender not in self.direct or m.target not in self.direct:
            self.counters["non_direct_or_overflow_control_refused"] += 1
            return False
        key = (m.target, m.sender, m.boot, m.coordinator_boot, m.challenge)
        if m.sequence <= self.accepted_sequences.get(key, 0):
            self.counters["replay_refused"] += 1
            return False
        # Only accepted complete authenticated messages consume replay state.
        self.accepted_sequences[key] = m.sequence
        if m.kind == "REPORT":
            p = self.peers[m.sender]
            if (p.boot, p.source_epoch) != (m.boot, m.source_epoch):
                p.unique = 0; p.last_second = -1
            p.boot, p.source_epoch = m.boot, m.source_epoch
            p.last_sequence = m.sequence
            p.ready = m.flags == 3
            if not p.ready:
                p.unique = 0
            if m.source_second > p.last_second and m.evidence_us > p.evidence_us:
                if self.now - p.evidence_us >= 6000000:
                    p.unique = 0
                p.unique += int(p.ready)
                p.last_second = m.source_second
                p.evidence_us = m.evidence_us
            elif m.source_second < p.last_second:
                p.ready = False
            self.counters["reports_accepted"] += 1
        elif m.kind == "PREPARE":
            if (m.root_boot != self.boots[m.target] or
                m.source_epoch != self.source_epochs[m.target] or
                not self.source_ready(m.target) or not self.cfg.map_known or not self.cfg.stop_known or
                m.decision <= self.retired_decisions[m.target]):
                self.counters["prepare_refused"] += 1
                return False
            self.prepared[m.target] = m.decision
            self.send(self.message("READY", m.target, TC03, root=m.target,
                                   root_boot=m.root_boot, source_epoch=m.source_epoch, decision=m.decision))
        elif m.kind == "READY":
            if (m.root == self.preparing_root and m.decision == self.decision and
                self.now < self.prepare_until and self.live(m.root) and self.now >= self.issued_until):
                self.preparing_root = None
                self.commit(m.root, renewal=False)
        elif m.kind == "COMMIT":
            c = self.cfg
            if (m.root != m.target or m.root_boot != self.boots[m.target] or
                m.source_epoch != self.source_epochs[m.target] or not self.source_ready(m.target) or
                not c.map_known or not c.stop_known or self.now >= m.permit_until_us or
                m.permit_until_us > m.created_us + c.lease_us or
                m.decision <= self.retired_decisions[m.target] or
                (m.target not in self.permits and self.prepared.get(m.target) != m.decision)):
                self.counters["grant_refused"] += 1
                return False
            old = self.permits.get(m.target)
            if old and m.decision <= old.decision:
                self.counters["grant_refused"] += 1
                return False
            # Convert coordinator deadline conservatively through an assumed map.
            # Adverse tests intentionally falsify these assumptions, never hide it.
            remaining = m.permit_until_us - self.now
            margin = c.map_bound_us + c.stop_bound_us + math.ceil(remaining*c.timer_bound_ppm/1e6)
            stop_at = self.now + int((remaining - margin - c.map_error_us)/(1 + c.timer_ppm/1e6))
            if stop_at <= self.now:
                self.counters["grant_refused"] += 1
                return False
            first = old is None
            self.permits[m.target] = Permit(m.target, m.root_boot, m.source_epoch,
                                           m.coordinator_boot, m.decision, m.permit_until_us, stop_at)
            if first:
                self.train_epochs[m.target] += 1
                self.train_started[m.target]=self.now
                self.sent_history[m.target].clear()
                self.log("PUBLISHER_ENABLED", root=m.target, decision=m.decision, until_us=m.permit_until_us)
                self.audit()
                self.call(self.now, 1, lambda n=m.target, t=self.train_epochs[m.target]: self.publish(n, t))
            self.call(stop_at, -1, lambda n=m.target, d=m.decision: self.expire_permit(n, d))
            self.send(self.message("ACTIVE", m.target, TC03, root=m.target,
                                   root_boot=m.root_boot, decision=m.decision))
        elif m.kind == "ACTIVE":
            if m.root == self.pending_root and m.decision == self.decision:
                self.active_root = m.root
                self.pending_root = None
                self.uncertain = False
                self.log("ACTIVE_ACK", root=m.root, decision=m.decision)
        elif m.kind == "STOP":
            if m.decision < self.retired_decisions[m.target] or (m.target in self.permits and
                    m.decision < self.permits[m.target].decision):
                self.counters["stop_refused"] += 1
                return False
            self.retired_decisions[m.target] = max(self.retired_decisions[m.target], m.decision)
            self.prepared.pop(m.target, None)
            self.call(self.now + self.cfg.stop_delay_us, -1,
                      lambda n=m.target, d=m.decision: self.stop(n, d, reply=True))
        elif m.kind == "STOPPED":
            if self.stopping and m.decision == self.decision and m.root == self.active_root:
                self.issued_until = self.now
                self.active_root = None
                self.stopping = False
                self.log("STOPPED_ACK", root=m.root, decision=m.decision)
        return True

    def report_round(self):
        if self.cfg.mode in "CD":
            for node in self.participants:
                if node not in self.direct:
                    continue
                evidence = self.now // 1000000 * 1000000
                self.send(self.message("REPORT", node, TC03,
                                       source_epoch=self.source_epochs[node],
                                       source_second=self.now//1000000 + 1, evidence_us=evidence,
                                       flags=3 if self.source_ready(node) else 0))
        self.call(self.now + 1000000, 3, self.report_round)

    def live(self, node):
        p = self.peers[node]
        return node in self.direct and p.ready and p.unique >= 3 and self.now - p.evidence_us < 6000000

    def prepare(self, root):
        self.decision += 1
        self.preparing_root = root
        self.prepare_until = self.now + self.cfg.control_life_us * 2
        self.send(self.message("PREPARE", TC03, root, root=root, root_boot=self.boots[root],
                               source_epoch=self.source_epochs[root], decision=self.decision))
        self.log("PREPARE_ISSUED", root=root, decision=self.decision)

    def commit(self, root, renewal=True):
        if renewal:
            self.decision += 1
        until = self.now + self.cfg.lease_us
        self.issued_until = max(self.issued_until, until + self.cfg.stop_bound_us)
        self.pending_root = root
        self.commit_ack_until = self.now + self.cfg.control_life_us
        self.last_grant = self.now
        self.send(self.message("COMMIT", TC03, root, root=root, root_boot=self.boots[root],
                               source_epoch=self.source_epochs[root], decision=self.decision,
                               permit_until_us=until))
        self.log("COMMIT_ISSUED", root=root, decision=self.decision, until_us=until)

    def tick(self):
        if self.cfg.mode in "CD":
            if self.now >= self.server_quiet_until:
                paused = self.cfg.permit_pause_start_us <= self.now < self.cfg.permit_pause_start_us + self.cfg.permit_pause_us
                if self.active_root is not None and not self.live(self.active_root) and not self.stopping:
                    self.decision += 1
                    self.stopping = True
                    self.send(self.message("STOP", TC03, self.active_root,
                                           root=self.active_root, decision=self.decision))
                    self.log("STOP_ISSUED", root=self.active_root, decision=self.decision)
                if self.stopping and self.now >= self.issued_until:
                    self.active_root = self.pending_root = None
                    self.stopping = False
                if self.issued_until and self.now >= self.issued_until:
                    self.pending_root = None
                    self.active_root = None
                    self.uncertain = False
                if self.pending_root is not None and self.now >= self.commit_ack_until:
                    self.uncertain = True
                if self.preparing_root is not None and self.now >= self.prepare_until:
                    self.preparing_root = None
                    self.counters["prepare_timeout"] += 1
                if not paused and not self.stopping:
                    incumbent = self.active_root if self.active_root is not None else self.pending_root
                    if incumbent is not None and self.live(incumbent):
                        if not self.uncertain and self.pending_root is None and self.now - self.last_grant >= 1000000:
                            self.commit(incumbent)
                    elif self.now >= self.issued_until and self.pending_root is None and self.preparing_root is None:
                        # Stable rank; healthy incumbent retained; no client vote.
                        ready = [n for n in self.participants if self.live(n)]
                        if ready:
                            self.prepare(min(ready, key=CANDIDATES.index))
        elif not self.permits and TC01 in self.direct and self.source_ready(TC01):
            # A/B are fixed-source passive transport fixtures, not a new BMCA.
            self.permits[TC01] = Permit(TC01, self.boots[TC01], self.source_epochs[TC01], 0, 0, self.end+1, self.end+1)
            self.train_epochs[TC01] += 1
            self.train_started[TC01]=self.now
            self.log("PUBLISHER_ENABLED", root=TC01, decision=0, until_us=self.end+1)
            self.call(self.now, 1, lambda t=self.train_epochs[TC01]: self.publish(TC01, t))
        self.audit()
        self.call(self.now + 10000, 0, self.tick)

    def audit(self):
        enabled = list(self.permits)
        if len(enabled) > 1:
            self.overlap.append({"at_us": self.now, "roots": enabled})
        for node, permit in self.permits.items():
            if self.now > permit.until_us + self.cfg.stop_bound_us:
                self.late_stops.append({"at_us": self.now, "root": node, "deadline_us": permit.until_us})

    def expire_permit(self, node, decision):
        if node in self.permits and self.permits[node].decision == decision:
            self.call(self.now + self.cfg.stop_delay_us, -1, lambda: self.stop(node, decision))

    def stop(self, node, decision, reply=False):
        current = self.permits.get(node)
        if current and (reply or current.decision == decision):
            if self.now > current.until_us + self.cfg.stop_bound_us:
                self.late_stops.append({"at_us": self.now, "root": node, "deadline_us": current.until_us})
            self.permits.pop(node)
            self.retired_decisions[node] = max(self.retired_decisions[node], current.decision, decision)
            self.log("PUBLISHER_STOPPED", root=node, decision=decision)
        if reply:
            self.send(self.message("STOPPED", node, TC03, root=node, decision=decision))

    def reboot(self, node):
        if node in self.permits:
            self.stop(node, self.permits[node].decision)
        self.boots[node] += 1
        self.source_epochs[node] += 1
        self.recovery_at[node] = self.now
        self.challenges[node] += 1000
        if node == TC03:
            self.coordinator_boot += 1
            # Keep outstanding publishers fenced; reboot never erases grant lifetime.
            self.server_quiet_until = max(self.now + self.cfg.lease_us + self.cfg.stop_bound_us,
                                          self.issued_until)
            self.active_root = self.pending_root = self.preparing_root = None
            self.stopping = False
            self.peers = {n: Peer() for n in CANDIDATES}
            for n in CANDIDATES:
                self.challenges[n] += 10000
        self.log("REBOOT", node=node, boot=self.boots[node], quiet_until=self.server_quiet_until)

    def source_loss(self):
        self.source_epochs[TC01] += 1
        if TC01 in self.permits:
            self.stop(TC01, self.permits[TC01].decision)
        self.log("SOURCE_LOST", node=TC01)

    def source_return(self):
        self.source_epochs[TC01] += 1
        self.recovery_at[TC01] = self.now
        self.log("SOURCE_RETURNED", node=TC01, ready_after_us=self.now + 60000000)

    def disconnect(self, node):
        self.direct.discard(node)
        self.forced_clients.add(node)
        if node in self.permits:
            self.stop(node, self.permits[node].decision)
        for r in list(self.gm_receivers):
            if r.node==node:
                self.gm_receivers.remove(r)
                r.gm_observer=r.always_online=False
                self.receivers.append(r)
                if r.synced is not None and r.synced[0] in self.train_started:
                    self.align_quiet(r,r.synced[0])
                # The remaining public receiver cannot keep a former uncapped
                # W12 GM privilege. Existing occupancy remains in the ledger.
                self.radios[node].capped=True
        self.log("DIRECT_LINK_LOST_CLIENT_ONLY", node=node)

    def scan(self, r, at):
        phase = (at - r.phase) % 1000000
        window=min(self.cfg.scan_us,self.cfg.ble_window_us) if self.cfg.quiet_listen and not r.always_online and r is not self.relay else self.cfg.scan_us
        return phase < window

    def awake(self,r,start,end=None):
        if not self.cfg.quiet_listen or r.always_online or r is self.relay:
            return True
        phase=(start-r.phase)%1000000
        return phase<self.cfg.ble_window_us and (end is None or phase+(end-start)<=self.cfg.ble_window_us)

    def align_quiet(self,r,root):
        if self.cfg.quiet_listen and not r.always_online and r is not self.relay:
            r.phase=(self.train_started[root]+self.cfg.quiet_run_choice*500000-1000)%1000000

    def original_run(self,root,offset=0):
        return not (self.cfg.quiet_listen or self.cfg.two_windows) or (self.now-self.train_started[root]-offset)%500000<self.cfg.run_active_us

    def mark_update(self,r,root):
        if r.clock.ready():
            r.precision_updates.append(self.now)
            if 10000000<=self.now<self.end-1000000:
                r.window_updates.add((root,self.boots[root],self.train_epochs[root],
                                      (self.now-self.train_started[root])//500000))

    def discovery(self):
        emitted = []
        if self.now >= self.cfg.discovery_blackout_us:
            for node in self.permits:
                if self.radios[node].reserve(self.now, self.now+self.cfg.discovery_air_us, False):
                    emitted.append(node)
                    self.counters["discovery_TX"] += 1
        observers = self.receivers + self.gm_receivers + ([self.relay] if self.relay else [])
        for r in observers:
            if r.gm_observer and r.node in self.permits:
                continue
            if r.synced is not None and self.now - r.last_success_us > self.cfg.sync_timeout_us:
                r.synced = None
                r.counters["sync_lost"] += 1
            if self.cfg.mode in "AC" or r.synced is None:
                cycle = self.now//1000000
                begin = cycle*1000000 + r.phase
                window=min(self.cfg.scan_us,self.cfg.ble_window_us) if self.cfg.quiet_listen and not r.always_online and r is not self.relay else self.cfg.scan_us
                self.radios[r.node].reserve(begin, begin+window, True, background=True)
            if self.cfg.mode in "BD" and r.synced is None and self.scan(r, self.now):
                for node in emitted:
                    if r.far or r.node==node:
                        continue
                    if (self.radios[r.node].reserve(self.now, self.now+self.cfg.discovery_air_us, True) and
                        not self.blocked(node, r.node, self.now) and self.rng.random() >= self.cfg.rf_loss):
                        r.synced = (node, self.boots[node], self.train_epochs[node])
                        self.align_quiet(r,node)
                        r.last_success_us = self.now
                        r.counters["sync_established"] += 1
                        break
            if self.relay_scope is not None and r.far and r.synced is None and self.scan(r,self.now):
                begin=self.now+self.cfg.discovery_air_us+500
                end=begin+self.cfg.discovery_air_us
                if (self.relay_valid() and self.radios[RELAY].admit(begin,end,False) and
                    self.radios[r.node].admit(begin,end,True)):
                    self.radios[RELAY].reserve(begin,end,False)
                    self.radios[r.node].reserve(begin,end,True)
                    if self.rng.random() >= self.cfg.downstream_loss:
                        r.synced=self.relay_scope[:3]
                        self.align_quiet(r,self.relay_scope[3])
                        r.last_success_us=self.now
                        r.counters["relay_sync_established"]+=1
        # A coarse observer scans for100ms every4s. It records only public
        # discovery/source age; these arrivals never enter a precision estimator.
        if self.cfg.coarse_client and self.now%4000000<100000:
            self.radios[12].reserve(self.now,self.now+100000,True,background=True)
            for node in emitted:
                if not self.blocked(node,12,self.now) and self.rng.random()>=self.cfg.rf_loss:
                    self.coarse_receipts.append((self.now,node))
                    break
        self.call(self.now + self.cfg.discovery_interval_us, 2, self.discovery)

    def publish(self, node, train=None):
        permit = self.permits.get(node)
        if permit is None or (train is not None and train != self.train_epochs[node]):
            return
        period = self.cfg.pa_interval_us if self.cfg.mode in "BD" else self.cfg.legacy_interval_us
        self.call(self.now + period, 1, lambda: self.publish(node, train))
        if not self.source_ready(node):
            return
        if self.cfg.mode in "CD" and (permit.coordinator_boot != self.coordinator_boot or
                                       permit.root_boot != self.boots[node] or
                                       permit.source_epoch != self.source_epochs[node]):
            # A disconnected root still honors its original permit, including old
            # coordinator epoch until expiry. Local root/source boot changes refuse.
            if permit.root_boot != self.boots[node] or permit.source_epoch != self.source_epochs[node]:
                return
        self.event_numbers[node] += 1
        event = self.event_numbers[node]
        scope = (node, self.boots[node], self.train_epochs[node], self.source_epochs[node])
        corrections = list(self.sent_history[node])
        ad_size = 118 + 20*len(corrections) if self.cfg.mode in "BD" else 31
        duration = (1+4+2+ad_size+3)*8
        if not self.radios[node].reserve(self.now, self.now+duration, False):
            self.counters["publication_radio_refused"] += 1
            return
        reference_us = self.now + 40
        has_original=self.original_run(node)
        self.counters["timing_packets"]+=1
        if has_original:
            self.offers[node] += 1
            self.offer_references.append(reference_us)
        captured = reference_us*1000
        if has_original and (not self.cfg.data_update_misses or event % 7):
            self.sent_history[node].append((event, reference_us, captured))
        elif has_original:
            self.counters["payload_generation_missed"] += 1
        legacy_listeners = []
        for r in self.receivers + self.gm_receivers + ([self.relay] if self.relay else []):
            if r.far or r.node==node:
                continue
            eligible = r.synced == scope[:3] if self.cfg.mode in "BD" else self.scan(r, self.now)
            if not eligible:
                continue
            begin = max(0, self.now-self.cfg.rx_guard_us) if self.cfg.mode in "BD" else self.now
            stop = self.now+duration+self.cfg.rx_guard_us
            if r is not self.relay and not self.awake(r,begin,stop):
                r.counters["scheduled_quiet_packet_skipped"]+=1
                continue
            if not self.radios[r.node].reserve(begin, stop, True):
                r.counters["radio_refused"] += 1
                continue
            hop_loss = self.cfg.upstream_loss if r is self.relay else 0
            if self.blocked(node, r.node, self.now) or self.rng.random() < max(self.cfg.rf_loss,hop_loss):
                r.counters["RF_loss"] += 1
                continue
            r.last_success_us = self.now
            ambiguous=self.rng.random() < self.cfg.ambiguity_rate or (self.cfg.data_update_misses and event % 7 == 0)
            if ambiguous:
                r.counters["ambiguous_original"] += 1
                continue
            local = self.local_ns(r, reference_us)
            local += self.cfg.timestamp_bias_ns + int(self.rng.gauss(0, self.cfg.timestamp_noise_ns))
            local = round(local/50)*50  # declared native 20 MHz capture quantization
            if has_original:
                r.history[scope, event] = (reference_us, local)
            if r is self.relay:
                # Declared verified extension token on a directly received root
                # train; never a forwarded GM record. Deadline remains original.
                r.extension_permit = dataclasses.replace(permit)
            # First-use lifetime; bounded originals; never refreshed by repeats.
            r.history = {k: v for k, v in r.history.items() if self.now-v[0] < 3000000}
            if self.cfg.mode in "BD":
                self.call(stop+self.cfg.work_us, 3,
                          lambda r=r, s=scope, cs=corrections: self.correct(r, s, cs))
            else:
                legacy_listeners.append(r)
        if self.cfg.mode in "AC":
            self.call(self.now+20000, 1,
                      lambda rs=legacy_listeners, s=scope, e=event, ref=reference_us, cap=captured: self.legacy_correction(rs, s, e, ref, cap))

    def legacy_correction(self, receivers, scope, event, reference_us, captured):
        node = scope[0]
        if node not in self.permits or not self.radios[node].reserve(self.now, self.now+1000, False):
            return
        for r in receivers:
            if not self.scan(r, self.now) or not self.radios[r.node].reserve(self.now, self.now+1000, True):
                continue
            if self.blocked(node, r.node, self.now) or self.rng.random() < self.cfg.rf_loss:
                continue
            self.call(self.now+1000+self.cfg.work_us, 3,
                      lambda r=r: self.correct(r, scope, [(event, reference_us, captured)]))

    def correct(self, r, scope, corrections):
        if r is self.relay and scope[0] not in self.participants:
            r.counters["relay_depth_or_origin_refused"]+=1
            return
        r.submitted = {k: t for k, t in r.submitted.items() if self.now-t < 3000000}
        for event, reference_us, captured in corrections:
            key = (scope, event)
            if key in r.submitted:
                r.counters["duplicate_correction"] += 1
                continue
            original = r.history.get(key)
            if original is None or self.now-reference_us >= 3000000:
                r.counters["missing_or_expired_original"] += 1
                continue
            if self.cfg.logger_at_us <= self.now < self.cfg.logger_at_us + self.cfg.logger_stall_us:
                self.call(self.cfg.logger_at_us+self.cfg.logger_stall_us, 3,
                          lambda r=r, s=scope, cs=[(event, reference_us, captured)]: self.correct(r, s, cs))
                continue
            if r.clock.ingest(scope, event, reference_us, self.now, original[1], captured):
                r.submitted[key] = reference_us
                r.corrected.append((reference_us, self.now))
                self.mark_update(r,scope[0])
                r.counters["corrected"] += 1
                r.original_source_us=reference_us
                r.path=(scope[0],scope[1],scope[3],0)

    def relay_valid(self):
        r=self.relay
        if r is None or r.extension_permit is None or not r.clock.ready():
            return False
        permit=r.extension_permit
        return (self.now < permit.stop_at_us and self.now-r.original_source_us < self.cfg.relay_fresh_us and
                r.clock.scope == (permit.root,permit.root_boot,self.train_epochs[permit.root],permit.source_epoch) and
                r.path == (permit.root,permit.root_boot,permit.source_epoch,0))

    def relay_reboot(self):
        if not self.relay:
            return
        self.relay.clock=Clock(self.cfg.precision_min_refs,self.cfg.precision_min_span_us)
        self.relay.history.clear();self.relay.submitted.clear()
        self.relay.extension_permit=None;self.relay.synced=None
        self.relay_scope=None;self.relay_train+=1;self.relay_history.clear()
        self.relay_trace.append({"at_us":self.now,"kind":"RELAY_REBOOT"})

    def relay_publish(self):
        if not self.relay_valid():
            self.call(self.now+self.cfg.pa_interval_us,1,self.relay_publish)
            if self.relay_scope is not None:
                self.relay_trace.append({"at_us":self.now,"kind":"RELAY_WITHDRAWN"})
                self.relay_scope=None;self.relay_history.clear()
            self.counters["relay_ineligible"]+=1
            return
        r=self.relay;permit=r.extension_permit
        desired=((r.clock.last_reference_us-40)%self.cfg.pa_interval_us+self.cfg.relay_phase_us)%self.cfg.pa_interval_us
        offset=(desired-self.now)%self.cfg.pa_interval_us
        if offset:
            self.call(self.now+offset,1,self.relay_publish)
            return
        self.call(self.now+self.cfg.pa_interval_us,1,self.relay_publish)
        origin=(permit.root,permit.root_boot,permit.source_epoch)
        if self.relay_scope is None or self.relay_scope[3:] != origin:
            self.relay_train+=1
            self.relay_scope=(RELAY,1,self.relay_train,*origin)
            self.relay_history.clear()
            self.relay_trace.append({"at_us":self.now,"kind":"RELAY_ENABLED","origin":origin,"depth":1,
                                     "relative_phase_us":self.cfg.relay_phase_us})
        scope=self.relay_scope
        duration=(10+118+24+20*len(self.relay_history))*8
        if self.now+duration >= permit.stop_at_us or self.now+duration-r.original_source_us >= self.cfg.relay_fresh_us:
            self.counters["relay_deadline_headroom_refused"]+=1
            return
        if not self.radios[RELAY].reserve(self.now,self.now+duration,False):
            self.counters["relay_TX_radio_refused"]+=1
            return
        self.relay_events+=1;event=self.relay_events;ref=self.now+40
        mapped=r.clock.predict(ref,self.local_ns(r,ref))
        if mapped is None:
            return
        mapped+=self.cfg.relay_map_bias_ns
        upstream_ref=r.original_source_us
        self.max_relay_source_age_us=max(self.max_relay_source_age_us,self.now-upstream_ref)
        if self.now-upstream_ref >= self.cfg.relay_fresh_us or self.now >= permit.stop_at_us:
            self.relay_violations.append({"at_us":self.now,"kind":"expired_extension"})
        corrections=list(self.relay_history)
        has_original=self.original_run(permit.root,self.cfg.relay_phase_us)
        generation_valid=not self.cfg.data_update_misses or event%7 != 0
        if has_original and generation_valid:
            self.relay_history.append((event,ref,mapped,upstream_ref,permit.stop_at_us))
        elif has_original:
            self.counters["relay_payload_generation_missed"]+=1
        if has_original:
            self.relay_offers.append(ref)
            self.counters["relay_physical_offers"]+=1
        for client in self.receivers:
            if not client.far or client.synced != scope[:3]:
                continue
            start=max(0,self.now-self.cfg.relay_guard_us);stop=self.now+duration+self.cfg.relay_guard_us
            if not self.awake(client,start,stop):
                client.counters["scheduled_quiet_packet_skipped"]+=1;continue
            if not self.radios[client.node].reserve(start,stop,True):
                client.counters["relay_RX_radio_refused"]+=1;continue
            if self.rng.random()<max(self.cfg.rf_loss,self.cfg.downstream_loss):
                client.counters["relay_RF_loss"]+=1;continue
            client.last_success_us=self.now
            if not generation_valid or self.rng.random()<self.cfg.ambiguity_rate:
                client.counters["relay_ambiguous_original"]+=1;continue
            local=self.local_ns(client,ref)+self.cfg.timestamp_bias_ns+int(self.rng.gauss(0,self.cfg.timestamp_noise_ns))
            if has_original:
                client.history[scope,event]=(ref,round(local/50)*50)
            client.history={k:v for k,v in client.history.items() if self.now-v[0]<3000000}
            available=stop+self.cfg.work_us+self.cfg.relay_observer_stall_us
            self.call(available,3,lambda client=client,scope=scope,cs=corrections:self.relay_correct(client,scope,cs))

    def relay_correct(self, client, scope, corrections):
        # Depth-one timing only; upstream original age and original permission
        # cannot be refreshed by residence, downstream repeats or arrivals.
        for event,ref,source,upstream_ref,until in corrections:
            key=(scope,event)
            if key in client.submitted:
                client.counters["relay_duplicate_correction"]+=1;continue
            original=client.history.get(key)
            if (original is None or self.now-ref>=3000000 or self.now-upstream_ref>=self.cfg.relay_fresh_us or
                self.now>=until or len(scope)!=6 or scope[0]!=RELAY or scope[3] not in self.participants):
                client.counters["relay_origin_age_or_permission_refused"]+=1;continue
            if client.clock.ingest(scope,event,ref,self.now,original[1],source):
                client.submitted[key]=ref
                client.submitted={k:t for k,t in client.submitted.items() if self.now-t<3000000}
                client.corrected.append((ref,self.now));client.counters["corrected"]+=1
                self.mark_update(client,scope[3])
                client.path=(*scope[3:],1);client.original_source_us=upstream_ref
                client.origin_deadline_us=until

    def score(self):
        for r in self.receivers+self.gm_receivers:
            if 10000000<=self.now<self.end-1000000:
                for root in self.permits:
                    if r.node!=root and self.source_ready(root):
                        index=(self.now-self.train_started[root])//500000
                        begin=self.train_started[root]+index*500000
                        if begin>=10000000 and begin+500000<=self.end-1000000:
                            r.eligible_windows.add((root,self.boots[root],self.train_epochs[root],index))
            prediction = r.clock.predict(self.now, self.local_ns(r, self.now))
            if self.now-r.clock.last_reference_us>=self.cfg.held_limit_us:
                prediction=None
            # Expired source/permission cannot create new corrections or extend
            # publication. A finite local held output retains its original age
            # and UNKNOWN quality; it creates no RF authority or PTS permission.
            fresh=(self.now-r.original_source_us<self.cfg.relay_fresh_us and
                   (not r.far or self.now<r.origin_deadline_us))
            state="UNAVAILABLE" if prediction is None else ("FRESH" if fresh else "HELD")
            r.delivery_states[state]+=1
            if prediction is not None and self.cfg.quiet_listen and not self.awake(r,self.now):
                r.quiet_errors.append(prediction-self.now*1000)
            self.output_grid[r.node] += 1
            if prediction is None:
                r.counters["unavailable_grid"] += 1
            else:
                r.errors.append(prediction-self.now*1000)
        self.call(self.now+100000, 4, self.score)

    def run(self):
        while self.events:
            self.now, _, _, callback = heapq.heappop(self.events)
            callback()
        listeners = {}
        for r in self.receivers+self.gm_receivers:
            references = sorted(t for t, _ in r.corrected)
            points = [0, *references, self.end]
            gaps = [b-a for a, b in zip(points, points[1:])]
            errors = sorted(abs(x) for x in r.errors)
            rms = math.sqrt(sum(x*x for x in r.errors)/len(r.errors)) if r.errors else None
            max_error = max(errors) if errors else None
            cutoff = self.end - self.cfg.history * (self.cfg.pa_interval_us if self.cfg.mode in "BD" else self.cfg.legacy_interval_us)
            first_ref = references[0] if references else self.end + 1
            offered=self.relay_offers if r.far else self.offer_references
            expected = sum(first_ref <= t <= cutoff for t in offered)
            corrected = sum(first_ref <= t <= cutoff for t in references)
            listeners[str(r.node)] = {
                "corrected": len(r.corrected), "first_corrected_us": r.corrected[0][1] if r.corrected else None,
                "first_usable_update_us":r.precision_updates[0] if r.precision_updates else None,
                "max_gap_us_including_acquisition_tail": max(gaps),
                "max_warmed_reference_gap_us": max((b-a for a,b in zip(references,references[1:])), default=None),
                "corrected_fraction_after_acquisition_emitted": corrected/expected if expected else None,
                "emitted_opportunities_scored": expected,
                "timing_path":"ONE_HOP_EXTENSION" if r.far else "DIRECT",
                "clock_mode":"GM_ALWAYS_ONLINE" if r.gm_observer else ("FULL_PRECISION" if r.always_online or not self.cfg.quiet_listen else "HALF_DUTY_CLIENT"),
                "last_origin_path":r.path,
                "max_correction_latency_us": max((b-a for a, b in r.corrected), default=None),
                "grid_available_fraction": len(r.errors)/max(1, self.output_grid[r.node]),
                "fresh_grid_fraction":r.delivery_states["FRESH"]/max(1,self.output_grid[r.node]),
                "delivery_states":dict(r.delivery_states),
                "precision_update_seconds_after_10s":len({t//1000000 for t in r.precision_updates if 10000000<=t<self.end-1000000}),
                "warmed_1Hz_update_fraction":len({t//1000000 for t in r.precision_updates if 10000000<=t<self.end-1000000})/max(1,self.cfg.seconds-11),
                "missed_update_seconds_after_10s":max(0,self.cfg.seconds-11-len({t//1000000 for t in r.precision_updates if 10000000<=t<self.end-1000000})),
                "eligible_source_windows":len(r.eligible_windows),
                "warmed_two_window_fraction":len(r.window_updates&r.eligible_windows)/len(r.eligible_windows) if r.eligible_windows else None,
                "synthetic_max_quiet_hold_abs_ns":max(map(abs,r.quiet_errors),default=None),
                "synthetic_rms_ns": rms,
                "synthetic_p99_abs_ns": errors[min(len(errors)-1, math.ceil(.99*len(errors))-1)] if errors else None,
                "synthetic_max_abs_ns": max_error,
                "numeric_screen": bool(r.errors) and rms < 400 and max_error <= 1000,
                "counters": dict(r.counters)}
        radios = {str(n): radio.metrics() for n, radio in self.radios.items()}
        safety = {"no_multiple_enabled_publishers": not self.overlap,
                  "publisher_deadline": not self.late_stops,
                  "union_cap": all(not v["administrative_caps_enforced"] or v["max_union_us_per_6s"] <= 3000000 for v in radios.values()),
                  "listen_cap": all(not v["administrative_caps_enforced"] or v["max_rx_us_per_1s"] <= 500000 for v in radios.values()),
                  "client_quiet_schedule":all(not self.cfg.quiet_listen or r.always_online or
                       self.radios[r.node].metrics()["max_rx_us_per_1s"]<=500000 for r in self.receivers),
                  "correction_causality": all(a <= b for r in self.receivers+self.gm_receivers for a, b in r.corrected),
                  "max_four_direct_GMs":len(self.direct)<=MAX_GMS and not (set(self.permits)-set(self.participants)),
                  "one_hop_timing_only":not self.relay_violations and all(r.path is None or r.path[-1]<=1 for r in self.receivers)}
        return {"schema": 1, "profile": dataclasses.asdict(self.cfg),
                "authority_scope": "CONNECTED_COORDINATOR_V12_PROPOSAL" if self.cfg.mode in "CD" else "FIXED_SOURCE_TRANSPORT_FIXTURE",
                "execution": "SYNTHETIC_OFFLINE_ONLY", "independent_precision": None, "PTS": "OFF",
                "assumptions": self.api_assumptions, "safety": safety, "safety_pass": all(safety.values()),
                "offers": dict(self.offers), "listeners": {str(r.node):listeners[str(r.node)] for r in self.receivers},
                "GM_precision_observers":{str(r.node):listeners[str(r.node)] for r in self.gm_receivers}, "radios": radios,
                "coarse_client":{"enabled":self.cfg.coarse_client,"period_us":4000000,"window_us":100000,
                                 "public_receipts":len(self.coarse_receipts),"precision_anchors":0,"GM_authority":False},
                "counters": dict(self.counters), "publisher_overlap": self.overlap,
                "late_stops": self.late_stops,
                "membership":{"cap":MAX_GMS,"participants":list(self.participants),"direct_at_end":sorted(self.direct),
                              "forced_client":sorted(self.forced_clients),"relay_has_GM_authority":False},
                "four_unit_mesh_mapping":{str(TC01):"TC-01 GM/root",str(TC03):"TC-03 GM/coordinator",
                        str(RELAY):"C-3 time-only relay",str(C1):"C-1 far client"} if
                        self.cfg.one_hop and self.cfg.gm_count==2 and self.cfg.clients==1 and self.cfg.far_clients==1 else None,
                "one_hop":{"enabled":self.cfg.one_hop,"phase_us":self.cfg.relay_phase_us,
                           "offers":len(self.relay_offers),"transitions":self.relay_trace,
                           "max_original_source_age_at_TX_us":self.max_relay_source_age_us,
                           "violations":self.relay_violations,"independent_precision":None},
                "duty_profile":{"enabled":self.cfg.quiet_listen,"runs_per_second":2 if self.cfg.quiet_listen or self.cfg.two_windows else None,
                                "BLE_window_us":self.cfg.ble_window_us,"contiguous_quiet_us":1000000-self.cfg.ble_window_us,
                                "client_only":True,"GM_online_us_per_second":1000000,"relay_online_us_per_second":1000000,
                                "native_W12_administrative_caps_removed":self.cfg.native_w12,
                                "control_30ms_profile_provides_S3_blackout":False,"PTS_qualified":False},
                "transitions": self.trace}


def run(cfg: Profile):
    return Simulator(cfg).run()
