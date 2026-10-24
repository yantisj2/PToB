"""Proposed bounded controller-job windows, not HCI timing proof. MIT, Jonathan Yantis 2026."""
import hashlib,struct
from reference import uint
FRAME=250000
CONTROL_RX=(28000,58000)
PRIMARY_TX=(38000,40000)
SECONDARY_TX=(43000,45000)
STATUS_RX=(80000,120000)

def status_slot(identity,boot,cycle):
 for value in (identity,boot,cycle):uint(value,64)
 if not identity or not boot:raise ValueError('identity/boot')
 raw=b'GM7v2-slot\0'+struct.pack('>QQQ',identity,boot,cycle)
 return int.from_bytes(hashlib.sha256(raw).digest()[:8],'big')%8

def verify_window(identity,boot,cycle,jitter,guard=8000,airtime=2000,scheduling=2000):
 if type(jitter)is not int or not -250<=jitter<=250:raise ValueError('jitter')
 for x in (guard,airtime,scheduling):uint(x,32)
 if guard>8000 or airtime>2000 or scheduling>2000:raise ValueError('budget exceeds reservation')
 for start,end in (PRIMARY_TX,SECONDARY_TX):
  if start-guard<CONTROL_RX[0] or end+scheduling+guard>CONTROL_RX[1]:raise ValueError('control containment')
 if PRIMARY_TX[0]+scheduling+airtime>SECONDARY_TX[0]:raise ValueError('jobs overlap')
 start=90000+status_slot(identity,boot,cycle)*2500+jitter
 if start-guard<STATUS_RX[0] or start+scheduling+airtime+guard>STATUS_RX[1]:raise ValueError('STATUS containment')
 # A request in STATUS window can be replied to in next secondary opportunity.
 latest_reply=FRAME+SECONDARY_TX[0]+scheduling+airtime
 if latest_reply-STATUS_RX[0]>250000:raise ValueError('causal reply deadline')
 return start
