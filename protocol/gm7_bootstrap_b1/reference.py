"""Conservative bootstrap endpoints B1. MIT Jonathan Yantis 2026."""
from dataclasses import dataclass
U64=2**64-1
LIMIT=250000

def bounded(x):
 if type(x)is not int or not 0<=x<=U64:raise ValueError('uint64')
 return x

@dataclass
class Pending:
 lower:int
 deadline:int
 consumed:bool=False
 last:int=0

 @classmethod
 def begin(cls,lower):
  bounded(lower)
  if lower>U64-LIMIT:raise ValueError('overflow')
  return cls(lower,lower+LIMIT,False,lower)

 def accept(self,upper,sample,valid,scope_authenticated=True):
  for v in (upper,sample,valid):bounded(v)
  if upper<self.last:self.consumed=True;raise ValueError('monotonic reset')
  self.last=upper
  if not scope_authenticated:raise ValueError('MAC/group/nonce/boot scope')
  if self.consumed:raise ValueError('consumed')
  self.consumed=True
  elapsed=upper-self.lower
  if upper>self.deadline or valid==0 or valid>1500000 or elapsed>=valid:raise ValueError('expired')
  remaining=valid-elapsed
  if upper>U64-remaining:raise ValueError('expiry overflow')
  return dict(offset_lo=self.lower-sample,offset_hi=upper-sample,
              source_until=upper+remaining,remaining=remaining,
              basis='HOST_CAUSAL_BOUND_NOT_RF')
