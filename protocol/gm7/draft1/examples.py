"""Synthetic examples only. Copyright (c) 2026 Jonathan Yantis. MIT."""
import json
from codec import FIELDS, ANNOUNCE, STATUS, ELIGIBLE, SHADOW, HOT, YIELD, WITHDRAW, PUBLIC_KEY, encode, decode
T = 1792800000000000


def packet(kind=ANNOUNCE, **changes):
    p = {name:0 for name,_ in FIELDS}
    p.update(kind=kind, flags=ELIGIBLE, group=0x0102030405060708,
             key_epoch=1, sender=0x1011121314151617, boot=0x2021222324252627,
             sequence=1, sent_us=T, max_age_us=2750000 if kind==ANNOUNCE else 4500000,
             source_gen=3,capture_gen=4,train_gen=5 if kind in (ANNOUNCE,WITHDRAW) else 0,
             priority1=128,clock_class=6,accuracy=32,variance=100,priority2=128)
    if kind in (ANNOUNCE,WITHDRAW):
        p.update(birth_us=T-10000000,founder=p['sender'],term=1,
                 active=p['sender'],active_boot=p['boot'],hot=0x3031323334353637,
                 hot_boot=0x4041424344454647)
    p.update(changes)
    return p


def examples():
    active = packet()
    status = packet(STATUS,sender=active['hot'],boot=active['hot_boot'],
                    flags=ELIGIBLE|SHADOW|HOT, birth_us=active['birth_us'],
                    founder=active['founder'],term=1,active=active['active'],
                    active_boot=active['active_boot'],hot=active['hot'],
                    hot_boot=active['hot_boot'],shadow_gen=7,train_gen=5)
    yield_packet = dict(active,flags=ELIGIBLE|YIELD,sequence=2,yield_us=T+750000)
    withdrawn = dict(active,kind=WITHDRAW,flags=0,sequence=3,yield_us=0,max_age_us=1000000)
    initial = packet(STATUS)
    cold = packet(STATUS,flags=0,source_gen=0,capture_gen=0,train_gen=0)
    out=[]
    for name,p in [('announce',active),('status-hot',status),('announce-yield',yield_packet),('status-unqualified',cold),
                   ('status-initial-eligible',initial),('withdraw-source-invalid',withdrawn)]:
        wire=encode(p,PUBLIC_KEY)
        out.append({'name':name,'hex':wire.hex(), 'decoded':decode(wire,PUBLIC_KEY,p['group'],p['key_epoch'])})
    return {'revision':'GM7-draft1','public_test_key_hex':PUBLIC_KEY.hex(),'examples':out}

if __name__=='__main__':
    print(json.dumps(examples(),indent=2,sort_keys=True))
