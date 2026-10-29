"""Independent C1 reservation lineage model. MIT Jonathan Yantis2026."""
from dataclasses import dataclass
MIN=946684800000000
MAX=4102444800000000

def u64(v):
 if type(v)is not int or not 0<=v<2**64:raise ValueError('uint64')
 return v

@dataclass
class Reservation:
 birth:int
 founder:int
 term:int
 sampled:int
 effective:int
 exposed:bool=False
 committed:bool=False
 terminal:bool=False
 cancelled:bool=False

 @classmethod
 def initial(cls,utc,founder,sampled,effective,qualified=True,winner=True):
  for v in (utc,founder,sampled,effective):u64(v)
  if not MIN<=utc<MAX or not founder or effective<sampled+3000 or not qualified or not winner:raise ValueError('reservation preconditions')
  return cls(utc,founder,1,sampled,effective)

 @classmethod
 def inherit(cls,birth,founder,term,sampled,effective):
  for v in (birth,founder,term,sampled,effective):u64(v)
  if not MIN<=birth<MAX or not founder or not term or term==2**64-1 or effective<sampled+3000:raise ValueError('inherited preconditions')
  return cls(birth,founder,term+1,sampled,effective)

 def aux_started(self):
  if self.cancelled or self.terminal:raise ValueError('cancelled')
  self.exposed=True

 def complete(self,start,end,authority_live=True):
  for v in (start,end):u64(v)
  if self.cancelled or self.terminal or not self.exposed or not authority_live or not self.effective<=start<=self.effective+2000 or not start<=end<=min(start+2000,self.effective+4000):raise ValueError('completion rejected')
  self.committed=True

 def abort(self):
  self.cancelled=True
  if self.exposed or self.committed:self.terminal=True;return (self.birth,self.founder,self.term)
  return None
