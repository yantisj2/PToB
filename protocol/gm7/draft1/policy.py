"""Offline initial policies, not a radio state machine.
Copyright (c) 2026 Jonathan Yantis. MIT.
"""
from dataclasses import dataclass
from codec import (ANNOUNCE, STATUS, WITHDRAW, ELIGIBLE, Recent, uint, utc, validate)

DWELL_US = 60000000


class Admission:
    """Continuous independent GNSS health; BLE shadow never feeds admission."""
    def __init__(self):
        self.last = None
        self.since = None
        self.source = None
        self.expiry = 0
        self.failed = False

    def update(self, now, source_tuple, valid_until, *, gm, gnss, independent):
        uint(now,64); uint(valid_until,64)
        if len(source_tuple) != 3:
            raise ValueError('local source/capture/GNSS-map tuple')
        for x in source_tuple:
            uint(x,32)
        if any(type(x) is not bool for x in (gm,gnss,independent)):
            raise ValueError('boolean health flags')
        if self.last is not None and now < self.last:
            self.failed = True
        self.last = now
        if self.failed or not (gm and gnss and independent and all(source_tuple) and now < valid_until):
            self.since = self.source = None
            self.expiry = 0
            return False
        if self.since is None or source_tuple != self.source or now >= self.expiry:
            self.since = now
        self.source, self.expiry = source_tuple, valid_until
        return now-self.since >= DWELL_US


@dataclass(frozen=True)
class UTCMap:
    anchor_mono: int
    anchor_utc: int
    valid_until: int
    rate_ppb: int
    source: tuple
    leap_clear: bool

    def project(self, now, source):
        uint(now,64); uint(self.anchor_mono,64); uint(self.valid_until,64)
        utc(self.anchor_utc)
        if len(self.source) != 3:
            raise ValueError('map source tuple')
        for x in self.source:
            uint(x,32,True)
        if type(self.rate_ppb) is not int or abs(self.rate_ppb) > 100000:
            raise ValueError('rate outside +/-100 ppm')
        if self.leap_clear is not True or source != self.source or not self.anchor_mono <= now < self.valid_until:
            raise ValueError('unqualified/expired/mismatched/leap-ambiguous map')
        delta = now-self.anchor_mono
        # Explicit floor toward -infinity; no float, clamp, truncation or wrap.
        return utc(self.anchor_utc + delta + (delta*self.rate_ppb)//1000000000)


def create_lineage(identity, now, admission, utc_map):
    uint(identity,64,True); uint(now,64)
    if admission.failed or admission.since is None or admission.last != now or now >= admission.expiry or now-admission.since < DWELL_US:
        raise ValueError('current independent dwell required at transition')
    return (utc_map.project(now,admission.source), identity, 1)


def _inventory(rows, now):
    """Validate BEFORE filtering eligibility; duplicate identities fail closed.

    Exact repeated snapshots are deduplicated. Conflicting current snapshots
    (including ineligible ones) quarantine their whole physical identity.
    Invalid/expired rows are excluded, with the same result in every order.
    """
    uint(now,64)
    valid = {}
    bad = set()
    contexts = set()
    for row in rows:
        try:
            if not isinstance(row,Recent) or row.anchor_domain not in ('GNSS_UTC','TRUSTED_COARSE_UTC'):
                continue
            uint(row.received_mono,64); uint(row.until_mono,64)
            p = validate(row.packet)
            if not row.received_mono <= now < row.until_mono:
                continue
        except (ValueError,TypeError):
            continue
        contexts.add((p['group'],p['key_epoch']))
        identity = p['sender']
        if identity in valid and p != valid[identity].packet:
            bad.add(identity)
        else:
            valid[identity] = row
    if len(contexts) > 1:
        return []  # Never compare independently authenticated different groups.
    return [r for i,r in valid.items() if i not in bad]


def quality(p):
    return tuple(p[k] for k in ('priority1','clock_class','accuracy','variance','priority2','sender','boot'))


def initial_choice(rows, now, known_terms=None):
    """Local roster ranking only; caller owns discovery/election quiet interval.

    A recent eligible ACTIVE suppresses initial election. A local candidate must
    be represented with the same validation and current independent admission.
    """
    inventory = _inventory(rows,now)
    if merge_choice(inventory,now,known_terms) is not None:
        return None
    candidates = [r for r in inventory if r.packet['kind']==STATUS and r.packet['flags'] & ELIGIBLE]
    return min(candidates,key=lambda r:quality(r.packet),default=None)


def merge_choice(rows, now, known_terms=None):
    """Oldest birth, founder, then descending term within that lineage.

    Lowest full identity (sender,boot) breaks exact term ties. Quality never
    participates. The caller must stop a losing ACTIVE before following.
    """
    floors = known_terms or {}
    for (birth,founder),term in floors.items():
        utc(birth); uint(founder,64,True); uint(term,64,True)
    candidates = [r for r in _inventory(rows,now) if r.packet['kind']==ANNOUNCE and r.packet['flags'] & ELIGIBLE
                  and r.packet['term'] >= floors.get((r.packet['birth_us'],r.packet['founder']),1)]
    def order(r):
        p = r.packet
        return (p['birth_us'],p['founder'],-p['term'],p['sender'],p['boot'])
    return min(candidates,key=order,default=None)


def inherit_selected(selected, rows, now, known_terms=None):
    winner = merge_choice(rows,now,known_terms)
    if winner is None or selected != winner:
        raise ValueError('only selected current ACTIVE initializes lineage')
    p = winner.packet
    return (p['birth_us'],p['founder'],p['term'])


def successor_lineage(lineage):
    """Arithmetic only; does NOT authorize takeover or resurrection on reboot."""
    birth, founder, term = lineage
    utc(birth); uint(founder,64,True); uint(term,64,True)
    return birth, founder, uint(term+1,64,True)


def serving_scope(p):
    """Train/source changes cannot evade a terminal serving-term withdrawal."""
    return tuple(p[k] for k in ('group','key_epoch','birth_us','founder','term','active','active_boot'))


class WithdrawalFence:
    """Tiny negative-state reference, not a HOT authorization/state machine.

    Caller supplies its retained authoritative current ACTIVE snapshot; that
    knowledge is not established by this helper. Apply blocks before all positive
    policy/owner use; retain fences while lineage/retired-term state survives.
    """
    def __init__(self):
        self.terminal = set()

    def consume(self, withdrawal, current, now):
        live = _inventory([withdrawal],now)
        if not live or not isinstance(current,Recent):
            return False
        w=live[0].packet
        try:
            c=validate(current.packet)
            uint(current.received_mono,64)
        except (ValueError,TypeError):
            return False
        if current.received_mono>now or current.anchor_domain not in ('GNSS_UTC','TRUSTED_COARSE_UTC'):
            return False
        if w['kind']!=WITHDRAW or c['kind']!=ANNOUNCE or not c['flags'] & ELIGIBLE:
            return False
        scope=serving_scope(w)
        if scope!=serving_scope(c) or scope in self.terminal:
            return False
        if w['train_gen']!=c['train_gen'] or w['sequence']<=c['sequence'] or w['sent_us']<c['sent_us']:
            return False
        if w['hot'] and (w['hot'],w['hot_boot'])!=(c['hot'],c['hot_boot']):
            return False
        self.terminal.add(scope)
        return True  # Negative withdrawal only; never grants takeover.

    def blocks(self, recent):
        p=validate(recent.packet)
        return p['kind']==ANNOUNCE and serving_scope(p) in self.terminal
