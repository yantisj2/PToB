"""GM7 implementation-review v2 layouts; offline only.
Copyright (c) 2026 Jonathan Yantis. MIT.
"""
import hashlib,hmac,json,struct
from pathlib import Path
U32=(1<<32)-1
KEY=bytes(range(32))
DOMAIN=b'PToB/GM7/implementation2\0'
HEAD=struct.Struct('>4sBBHHHQI')
# magic4, revision1, kind1, length2, flags2, reserved2, group8, key_epoch4.
CONTROL=[('sender','Q'),('boot','Q'),('sequence','Q'),('sent_utc_us','Q'),
 ('max_age_us','I'),('source_valid_us','I'),('source_gen','I'),('capture_gen','I'),('train_gen','I'),
 ('priority1','B'),('clock_class','B'),('accuracy','B'),('priority2','B'),('variance','H'),
 ('birth_us','Q'),('founder','Q'),('term','Q'),('active','Q'),('active_boot','Q'),
 ('hot','Q'),('hot_boot','Q'),('yield_utc_us','Q'),('shadow_gen','I'),('sent_mono_us','Q')]
REQUEST=[('requester','Q'),('requester_boot','Q'),('target','Q'),('target_boot','Q'),
 ('nonce_hi','Q'),('nonce_lo','Q'),('request_sequence','Q')]
ANCHOR=[('requester','Q'),('requester_boot','Q'),('nonce_hi','Q'),('nonce_lo','Q'),
 ('responder','Q'),('responder_boot','Q'),('response_sequence','Q'),('sampled_mono_us','Q'),
 ('utc_us','Q'),('birth_us','Q'),('founder','Q'),('term','Q'),('active','Q'),('active_boot','Q'),
 ('timing_epoch','I'),('source_gen','I'),('capture_gen','I'),('train_gen','I'),
 ('control_sequence','Q'),('valid_us','I'),('utc_error_us','I'),('depth','B'),('reserved','B')]
DIAG=[('sender','Q'),('boot','Q'),('sequence','Q'),('sampled_mono_us','Q'),
 ('birth_us','Q'),('founder','Q'),('term','Q'),('active','Q'),('active_boot','Q'),
 ('timing_epoch','I'),('valid_us','I')]
ROLE=DIAG+[('hot','Q'),('hot_boot','Q'),('source_gen','I'),('capture_gen','I'),
 ('train_gen','I'),('shadow_gen','I'),('role','B'),('known','B'),('blockers','H'),
 ('hot_duration_ms','I'),('active_duration_ms','I'),('pps_duration_ms','I')]
GNSS=DIAG+[('source_gen','I'),('capture_gen','I'),('train_gen','I'),
 ('known','H'),('values','H'),('sats','B'),('clock_class','B'),('accuracy','B'),
 ('priority1','B'),('priority2','B'),('reserved','B'),('variance','H'),('hdop_centi','H'),
 ('rmc_age_ms','H'),('pps_age_ms','H'),('gga_age_ms','H'),('utc_error_us','I')]
SCHEMAS={1:CONTROL,2:CONTROL,3:CONTROL,4:REQUEST,5:ANCHOR,6:ROLE,7:GNSS}

def uint(v,bits):
 if type(v) is not int or not 0<=v<1<<bits:raise ValueError('range/type')
 return v

def validate(p):
 if p.get('kind') not in SCHEMAS:raise ValueError('kind')
 fields=SCHEMAS[p['kind']]
 if set(p)!={'kind','flags','group','key_epoch'}|{k for k,_ in fields}:raise ValueError('field set')
 for k,f in fields:uint(p[k],struct.calcsize(f)*8)
 uint(p['flags'],16);uint(p['group'],64);uint(p['key_epoch'],32)
 if not p['group'] or not p['key_epoch']:raise ValueError('group/key')
 kind=p['kind'];flags=p['flags']
 if kind<=3:
  if flags&~31 or (kind!=2 and flags&16):raise ValueError('control flags')
  if not all(p[k] for k in ('sender','boot','sequence')):raise ValueError('identity')
  if not 0<p['max_age_us']<={1:2750000,2:4500000,3:1000000}[kind]:raise ValueError('age')
  lineage=[p[k] for k in ('birth_us','founder','term','active','active_boot')]
  if any(lineage) and not all(lineage):raise ValueError('partial lineage')
  if p['birth_us'] and not 946684800000000<=p['birth_us']<4102444800000000:raise ValueError('birth')
  if p['sent_utc_us'] and not 946684800000000<=p['sent_utc_us']<4102444800000000:raise ValueError('UTC')
  if flags&1 and not(p['source_gen'] and p['capture_gen']):raise ValueError('source')
  if bool(p['hot'])!=bool(p['hot_boot']):raise ValueError('nominee')
  if kind in (1,3):
   if not all(lineage) or (p['sender'],p['boot'])!=(p['active'],p['active_boot']) or not p['train_gen'] or p['shadow_gen']:raise ValueError('serving scope')
   if p['hot']==p['active']:raise ValueError('self nominee')
  if kind!=1 and p['source_valid_us']:raise ValueError('nonserving source permit')
  if kind==1:
   if not 0<p['source_valid_us']<=1500000:raise ValueError('physical source horizon')
   if not flags&1 or flags&6 or not p['sent_utc_us'] or p['birth_us']>p['sent_utc_us']:raise ValueError('ANNOUNCE')
   if bool(flags&8)!=bool(p['yield_utc_us']):raise ValueError('yield flag')
   if flags&8 and (not p['hot'] or p['yield_utc_us']-p['sent_utc_us']!=750000):raise ValueError('fixed yield')
  if kind==3 and (flags or p['yield_utc_us']):raise ValueError('WITHDRAW')
  if kind==2:
   if flags&8 or p['yield_utc_us']:raise ValueError('STATUS yield')
   if flags&1 and not flags&16:raise ValueError('RC not GM eligible')
   if bool(flags&2)!=bool(p['shadow_gen']) or bool(flags&2)!=bool(p['train_gen']):raise ValueError('shadow')
   if flags&2 and(not all(lineage) or p['sender']==p['active']):raise ValueError('shadow scope')
   if flags&4:
    if flags&19!=19 or not flags&2 or (p['hot'],p['hot_boot'])!=(p['sender'],p['boot']):raise ValueError('HOT')
   elif p['hot']:raise ValueError('STATUS nominee')
 elif kind==4:
  if flags or not all(p[k] for k in ('requester','requester_boot','target','target_boot','request_sequence')) or not(p['nonce_hi'] or p['nonce_lo']):raise ValueError('request')
 elif kind==5:
  if flags&~31 or (not flags&1 and not flags&16) or not all(p[k] for k in ('requester','requester_boot','responder','responder_boot','response_sequence','control_sequence','capture_gen')) or not(p['nonce_hi'] or p['nonce_lo']):raise ValueError('anchor identity/eligibility')
  if not 0<p['valid_us']<=1500000:raise ValueError('anchor permission age')
  lineage=[p[k] for k in ('birth_us','founder','term','active','active_boot')]
  if p['reserved']:raise ValueError('reserved')
  if flags&16:
   if flags&1 or not flags&2 or p['depth'] not in (1,2) or p['active']==p['responder'] or p['source_gen']:raise ValueError('relay not independent GNSS')
  elif p['depth'] or not p['source_gen']:raise ValueError('direct source/depth')
  if flags&2:
   if not all(lineage) or not(p['timing_epoch'] and p['train_gen']):raise ValueError('serving anchor')
   if not flags&16 and (p['active'],p['active_boot'])!=(p['responder'],p['responder_boot']):raise ValueError('direct serving identity')
  elif any(lineage) or p['timing_epoch'] or p['train_gen']:raise ValueError('candidate not timing source')
  for name in ('birth_us','utc_us'):
   if p[name] and not 946684800000000<=p[name]<4102444800000000:raise ValueError('UTC range')
  if bool(flags&4)!=bool(p['utc_us']) or flags&8 and(not flags&4 or p['utc_error_us']==U32):raise ValueError('UTC validity')
 else:
  if flags or not all(p[k] for k in ('sender','boot','sequence')) or not 0<p['valid_us']<={6:4500000,7:30000000}[kind]:raise ValueError('diagnostic header')
  lineage=[p[k] for k in ('birth_us','founder','term','active','active_boot')]
  if any(lineage) and not all(lineage):raise ValueError('diagnostic lineage')
  if kind==6:
   if p['blockers']&~8191:raise ValueError('blockers')
   if p['role']>4 or p['known']&~7 or bool(p['hot'])!=bool(p['hot_boot']):raise ValueError('role')
   for bit,name in enumerate(('hot_duration_ms','active_duration_ms','pps_duration_ms')):
    if bool(p['known']&(1<<bit))==(p[name]==U32):raise ValueError('duration known/unknown')
  else:
   if p['reserved'] or p['known']&~127 or p['values']&~7 or p['values']&~p['known']:raise ValueError('GNSS known')
   if p['values']&1 and p['rmc_age_ms']>=2500:raise ValueError('fix age')
   if p['values']&2 and p['pps_age_ms']>=1500:raise ValueError('PPS age')
   if p['values']&4 and p['values']&3!=3:raise ValueError('qualification')
   if p['known']&40 and p['gga_age_ms']>=4000:raise ValueError('GGA age')
   if bool(p['known']&64)==(p['utc_error_us']==U32):raise ValueError('UTC bound known')
   if p['known']&8:
    if p['sats']>99:raise ValueError('sats')
   elif p['sats']!=255:raise ValueError('sats unknown')
   if not p['known']&32 and p['hdop_centi']!=65535:raise ValueError('HDOP unknown')
 return p

def encode(p,key=KEY):
 validate(p)
 if type(key)is not bytes or len(key)!=32 or not any(key):raise ValueError('key')
 body=struct.pack('>'+''.join(f for _,f in SCHEMAS[p['kind']]),*(p[k] for k,_ in SCHEMAS[p['kind']]))
 raw=HEAD.pack(b'G7V2',2,p['kind'],HEAD.size+len(body)+16,p['flags'],0,p['group'],p['key_epoch'])+body
 return raw+hmac.digest(key,DOMAIN+raw,'sha256')[:16]

def decode(raw,key=KEY,expected_group=0x0102030405060708,expected_epoch=1):
 if len(raw)<HEAD.size+16:raise ValueError('short')
 magic,version,kind,size,flags,reserved,group,epoch=HEAD.unpack_from(raw)
 if magic!=b'G7V2' or version!=2 or kind not in SCHEMAS or size!=len(raw) or reserved:raise ValueError('header')
 if group!=expected_group or epoch!=expected_epoch:raise ValueError('configured group/key epoch')
 if type(key)is not bytes or len(key)!=32 or not any(key):raise ValueError('key')
 if not hmac.compare_digest(raw[-16:],hmac.digest(key,DOMAIN+raw[:-16],'sha256')[:16]):raise ValueError('MAC')
 fmt='>'+''.join(f for _,f in SCHEMAS[kind])
 if struct.calcsize(fmt)!=len(raw)-HEAD.size-16:raise ValueError('body length')
 return validate(dict(kind=kind,flags=flags,group=group,key_epoch=epoch,**dict(zip((k for k,_ in SCHEMAS[kind]),struct.unpack(fmt,raw[HEAD.size:-16])))))

def blank(kind):return dict(kind=kind,flags=0,group=0x0102030405060708,key_epoch=1,**{k:0 for k,_ in SCHEMAS[kind]})

def examples():
 t=1792800000000000
 a=blank(1);a.update(flags=1,sender=17,boot=33,sequence=7,sent_utc_us=t,max_age_us=2750000,source_valid_us=1200000,source_gen=3,capture_gen=4,train_gen=5,priority1=128,clock_class=2,accuracy=255,priority2=128,variance=65535,birth_us=t-10000000,founder=17,term=1,active=17,active_boot=33,hot=49,hot_boot=65,sent_mono_us=10000000)
 status=blank(2);status.update(flags=17,sender=49,boot=65,sequence=3,sent_utc_us=t,max_age_us=4500000,source_gen=8,capture_gen=9,priority1=128,clock_class=2,accuracy=255,priority2=128,variance=65535,sent_mono_us=9000000)
 req=blank(4);req.update(requester=81,requester_boot=97,target=17,target_boot=33,nonce_hi=0x1020304050607080,nonce_lo=0x90a0b0c0d0e0f001,request_sequence=1)
 anchor=blank(5);anchor.update(flags=7,requester=81,requester_boot=97,nonce_hi=req['nonce_hi'],nonce_lo=req['nonce_lo'],responder=17,responder_boot=33,response_sequence=1,sampled_mono_us=10010000,utc_us=t+10000,birth_us=a['birth_us'],founder=17,term=1,active=17,active_boot=33,timing_epoch=3,source_gen=3,capture_gen=4,train_gen=5,control_sequence=7,valid_us=1200000,utc_error_us=U32)
 cand=dict(anchor,flags=5,responder=49,responder_boot=65,birth_us=0,founder=0,term=0,active=0,active_boot=0,timing_epoch=0,train_gen=0,control_sequence=3)
 role=blank(6);role.update(sender=17,boot=33,sequence=1,sampled_mono_us=10000000,birth_us=a['birth_us'],founder=17,term=1,active=17,active_boot=33,timing_epoch=3,valid_us=4500000,hot=49,hot_boot=65,source_gen=3,capture_gen=4,train_gen=5,role=1,known=6,hot_duration_ms=U32,active_duration_ms=10000,pps_duration_ms=80000)
 gnss=blank(7);gnss.update({k:role[k] for k,_ in DIAG});gnss.update(valid_us=30000000,source_gen=3,capture_gen=4,train_gen=5,known=63,values=7,sats=12,clock_class=2,accuracy=255,priority1=128,priority2=128,variance=65535,hdop_centi=87,rmc_age_ms=100,pps_age_ms=20,gga_age_ms=200,utc_error_us=U32)
 relay=dict(anchor,flags=22,responder=113,responder_boot=129,source_gen=0,capture_gen=12,train_gen=13,depth=1)
 rows=[('relay-anchor',relay),('announce',a),('eligible-candidate-status',status),('bootstrap-request',req),('serving-anchor',anchor),('candidate-anchor',cand),('role-diagnostic',role),('gnss-diagnostic',gnss),('yield',dict(a,flags=9,sequence=8,yield_utc_us=t+750000)),('withdraw',dict(a,kind=3,flags=0,sequence=9,sent_utc_us=0,max_age_us=1000000,source_valid_us=0))]
 return {'revision':'GM7-implementation2','public_key_hex':KEY.hex(),'vectors':[{'name':n,'hex':encode(p).hex(),'decoded':p} for n,p in rows]}

if __name__=='__main__':print(json.dumps(examples(),indent=2,sort_keys=True))
