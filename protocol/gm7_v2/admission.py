"""Causal bootstrap reference, not production owner. Jonathan Yantis (c) 2026 MIT."""
from dataclasses import dataclass
from reference import validate,uint

@dataclass
class Session:
 group: int
 key_epoch: int
 peer: int
 boot: int
 floor: int
 offset_lo: int
 offset_hi: int
 established: int
 until: int
 source_until: int
 serving: bool
 last_seq: int=0

 def accept_control(self,p,now):
  validate(p);uint(now,64)
  if (p['group'],p['key_epoch'])!=(self.group,self.key_epoch):raise ValueError('session group')
  if p['kind'] not in (1,2,3) or (p['sender'],p['boot'])!=(self.peer,self.boot):raise ValueError('peer')
  if not self.established<=now<self.until:raise ValueError('session expiry')
  if p['sequence']<self.floor or p['sequence']<=self.last_seq:raise ValueError('replay/floor')
  drift=((now-self.established)*200+999999)//1000000
  if self.offset_hi-self.offset_lo+2*drift>500000:raise ValueError('uncertainty ceiling')
  earliest=p['sent_mono_us']+self.offset_lo-drift
  latest=p['sent_mono_us']+self.offset_hi+drift
  if earliest>now or now-earliest>=p['max_age_us']:raise ValueError('future/stale')
  if p['kind']==1 and earliest+p['source_valid_us']<=now:raise ValueError('physical source expired')
  if p['kind']==1 and self.serving:self.source_until=uint(earliest+p['source_valid_us'],64)
  if p['kind']==3:self.source_until=now;self.serving=False
  self.last_seq=p['sequence']
  return now+min(p['max_age_us'],p['max_age_us']-(now-earliest))


def anchor_reply(p,request,tx,rx):
 validate(p);validate(request);uint(tx,64);uint(rx,64)
 if p['kind']!=5 or request['kind']!=4 or not 0<=rx-tx<=250000:raise ValueError('causal RTT')
 if (p['group'],p['key_epoch'])!=(request['group'],request['key_epoch']):raise ValueError('group')
 for k in ('requester','requester_boot','nonce_hi','nonce_lo'):
  if p[k]!=request[k]:raise ValueError('nonce/recipient')
 if (p['responder'],p['responder_boot'])!=(request['target'],request['target_boot']):raise ValueError('target')
 remaining=p['valid_us']-(rx-tx)
 if remaining<=0:raise ValueError('source expiry')
 # A nonce is consumed by the caller after this succeeds, even if response repeats.
 return Session(p['group'],p['key_epoch'],p['responder'],p['responder_boot'],p['control_sequence'],
                tx-p['sampled_mono_us'],rx-p['sampled_mono_us'],rx,
                uint(rx+360000000,64),uint(rx+remaining,64),bool(p['flags']&2))
