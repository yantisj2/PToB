#!/usr/bin/env python3
"""Independent public-vector HMAC check; no deployment credentials."""
import hashlib,hmac,json,struct,sys
v=json.load(open(sys.argv[1]));key=bytes.fromhex(v['public_test_psk_hex']);prefix=b'\x01'+struct.pack('<QI',int(v['group_id'],16),v['key_epoch'])
def check(domain,body,tag):assert hmac.new(key,domain+prefix+body,hashlib.sha256).digest()[:16]==tag
for name,domain in [('hello',b'PTOBG1C1'),('service',b'PTOBG1C1'),('direct',b'PTOBG1P1'),('join_accept',b'PTOBG4J4'),('relay',b'PTOBG1R1')]:
    w=bytes.fromhex(v['vectors'][name]);check(domain,w[:-16],w[-16:])
w=bytes.fromhex(v['vectors']['relay']);check(b'PTOBG1O1',w[14:74],w[74:90])
print('PASS independently computed G1 domains, version/group/epoch prefix and truncated SHA256')

for name in ('v14_held_relay','v14_serving1650_relay'):
    w=bytes.fromhex(v['vectors'][name]);check(b'PTOBG3O3',w[14:74],w[74:90]);check(b'PTOBG3R3',w[:174],w[174:])
