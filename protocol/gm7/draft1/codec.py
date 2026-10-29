"""GM7-draft1 reference only. Copyright (c) 2026 Jonathan Yantis. MIT."""
from dataclasses import dataclass
import hashlib
import hmac
import struct

DOMAIN = b'PToB/GM7/draft1/control\x00'
PUBLIC_KEY = bytes(range(32))  # PUBLIC TEST KEY; never enrollment material.
ANNOUNCE, STATUS, WITHDRAW = 1, 2, 3
ELIGIBLE, SHADOW, HOT, YIELD = 1, 2, 4, 8
U64 = (1 << 64) - 1
UTC_MIN, UTC_MAX = 946684800000000, 4102444800000000  # [2000, 2100)
MAX_AGE = {ANNOUNCE: 2750000, STATUS: 4500000, WITHDRAW: 1000000}
# Every integer is unsigned, big endian, without padding.
FIELDS = [('kind','B'), ('flags','H'), ('group','Q'), ('key_epoch','I'),
          ('sender','Q'), ('boot','Q'), ('sequence','Q'), ('sent_us','Q'),
          ('max_age_us','I'), ('source_gen','I'), ('capture_gen','I'),
          ('train_gen','I'), ('priority1','B'), ('clock_class','B'),
          ('accuracy','B'), ('priority2','B'), ('variance','H'),
          ('birth_us','Q'), ('founder','Q'), ('term','Q'),
          ('active','Q'), ('active_boot','Q'), ('hot','Q'), ('hot_boot','Q'),
          ('yield_us','Q'), ('shadow_gen','I')]
PREFIX = struct.Struct('>4sBHH')  # magic, revision, total length, reserved
BODY = struct.Struct('>' + ''.join(fmt for _, fmt in FIELDS))
SIZE = PREFIX.size + BODY.size + 16
NAMES = {name for name, _ in FIELDS}


def uint(value, bits, nonzero=False):
    if type(value) is not int or not (int(nonzero) <= value < (1 << bits)):
        raise ValueError('unsigned integer range/type')
    return value


def utc(value):
    uint(value, 64)
    if not UTC_MIN <= value < UTC_MAX:
        raise ValueError('UTC outside [2000,2100)')
    return value


def validate(p):
    if set(p) != NAMES:
        raise ValueError('exact field set required')
    for name, fmt in FIELDS:
        uint(p[name], struct.calcsize(fmt) * 8)
    if p['kind'] not in MAX_AGE or p['flags'] & ~15:
        raise ValueError('kind/flags')
    for name in ('group','key_epoch','sender','boot','sequence'):
        if not p[name]:
            raise ValueError('zero identity/context')
    utc(p['sent_us'])
    if not 0 < p['max_age_us'] <= MAX_AGE[p['kind']]:
        raise ValueError('finite age')
    eligible = bool(p['flags'] & ELIGIBLE)
    if eligible and not all(p[k] for k in ('source_gen','capture_gen')):
        raise ValueError('eligible source tuple')
    lineage = [p[k] for k in ('birth_us','founder','term','active','active_boot')]
    if any(lineage) and not all(lineage):
        raise ValueError('partial lineage')
    if p['birth_us']:
        utc(p['birth_us'])
        if p['birth_us'] > p['sent_us']:
            raise ValueError('birth after send')
    if bool(p['hot']) != bool(p['hot_boot']):
        raise ValueError('partial nominee')
    if p['hot'] and p['hot'] == p['active']:
        raise ValueError('ACTIVE cannot nominate itself')
    if p['kind'] in (ANNOUNCE, WITHDRAW):
        if (p['kind']==ANNOUNCE and not eligible) or p['flags'] & (SHADOW | HOT) or not all(lineage):
            raise ValueError('ANNOUNCE role/lineage')
        if (p['sender'],p['boot']) != (p['active'],p['active_boot']) or p['shadow_gen'] or not p['train_gen']:
            raise ValueError('ANNOUNCE source')
        if p['kind']==WITHDRAW and (p['flags'] or p['yield_us']):
            raise ValueError('WITHDRAW has no flags or timed yield')
        if bool(p['flags'] & YIELD) != bool(p['yield_us']):
            raise ValueError('yield flag')
        if p['yield_us']:
            utc(p['yield_us'])
            if not p['hot'] or not 0 < p['yield_us']-p['sent_us'] <= 1000000:
                raise ValueError('yield bounded future time')
    else:
        if p['flags'] & YIELD or p['yield_us']:
            raise ValueError('STATUS cannot yield')
        if bool(p['flags'] & SHADOW) != bool(p['shadow_gen']) or bool(p['flags'] & SHADOW) != bool(p['train_gen']):
            raise ValueError('shadow generation')
        if p['flags'] & SHADOW and (not all(lineage) or p['sender'] == p['active']):
            raise ValueError('shadow source')
        if p['flags'] & HOT:
            if not eligible or not p['flags'] & SHADOW or (p['hot'],p['hot_boot']) != (p['sender'],p['boot']):
                raise ValueError('HOT self/eligibility/shadow')
        elif p['hot'] or p['hot_boot']:
            raise ValueError('STATUS nominee only for HOT self')
    return p


def validate_key(key):
    if type(key) is not bytes or len(key) != 32 or not any(key):
        raise ValueError('nonzero 32-byte key')


def encode(p, key):
    validate(p)
    validate_key(key)
    body = PREFIX.pack(b'GM7\x00',1,SIZE,0) + BODY.pack(*(p[n] for n,_ in FIELDS))
    return body + hmac.digest(key, DOMAIN + body, 'sha256')[:16]


def decode(wire, key, group, key_epoch):
    uint(group,64,True); uint(key_epoch,32,True)
    validate_key(key)
    if type(wire) is not bytes or len(wire) != SIZE or type(key) is not bytes or len(key) != 32:
        raise ValueError('exact wire/key length')
    if PREFIX.unpack_from(wire) != (b'GM7\x00',1,SIZE,0):
        raise ValueError('header/reserved/version')
    if not hmac.compare_digest(wire[-16:],hmac.digest(key,DOMAIN+wire[:-16],'sha256')[:16]):
        raise ValueError('MAC')
    p = dict(zip((n for n,_ in FIELDS),BODY.unpack(wire[PREFIX.size:-16])))
    validate(p)
    if (p['group'],p['key_epoch']) != (group,key_epoch):
        raise ValueError('group/key epoch')
    return p


@dataclass(frozen=True)
class Recent:
    """A bounded-recent authenticated claim, NOT proof of live first contact."""
    packet_items: tuple
    received_mono: int
    until_mono: int
    anchor_domain: str

    @property
    def packet(self):
        return dict(self.packet_items)


class Verifier:
    """Caller supplies independent qualified/coarse UTC. No silent cache eviction."""
    def __init__(self, key, group, key_epoch):
        validate_key(key)
        uint(group,64,True); uint(key_epoch,32,True)
        self.key, self.group, self.key_epoch = key, group, key_epoch
        self.seen = {}
        self.last_mono = None

    def accept(self, wire, now_mono, utc_lo, utc_hi, anchor_domain):
        uint(now_mono,64); utc(utc_lo); utc(utc_hi)
        if anchor_domain not in ('GNSS_UTC','TRUSTED_COARSE_UTC') or not 0 <= utc_hi-utc_lo <= 200000:
            raise ValueError('independent bounded UTC anchor required')
        if self.last_mono is not None and now_mono < self.last_mono:
            raise ValueError('monotonic regression')
        self.last_mono = now_mono
        p = decode(wire,self.key,self.group,self.key_epoch)
        if p['sent_us'] > utc_lo+100000 or utc_hi-p['sent_us'] >= p['max_age_us']:
            raise ValueError('future/stale')
        identity = (p['sender'],p['boot'])
        prior = self.seen.get(identity)
        if prior and (p['sequence'] <= prior[0] or p['sent_us'] < prior[1]):
            raise ValueError('replay/regressing send time')
        remaining = min(p['max_age_us'], p['max_age_us']-(utc_hi-p['sent_us']))
        until = uint(now_mono+remaining,64)
        self.seen[identity] = (p['sequence'],p['sent_us'])
        return Recent(tuple(p.items()), now_mono, until, anchor_domain)
