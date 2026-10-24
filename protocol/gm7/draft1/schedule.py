"""Synthetic window arithmetic only; no radio scheduling implementation.
Copyright (c) 2026 Jonathan Yantis. MIT.
"""
from dataclasses import dataclass
from codec import uint

FRAME=250000
TIMING=((0,10000),(62500,72500),(125000,135000),(187500,197500))
CONTROL_RX=(30000,52000)
CONTROL_TX=(40000,42000)
STATUS_RX=(80000,120000)


@dataclass(frozen=True)
class Budget:
    mapping_us: int=2000
    drift_us: int=500
    scheduling_us: int=2000
    turnaround_us: int=1000
    trusted: bool=True

    def guard(self):
        values=(self.mapping_us,self.drift_us,self.scheduling_us,self.turnaround_us)
        for value in values: uint(value,32)
        guard=sum(values)
        if self.trusted is not True or guard>8000:
            raise ValueError('alignment untrustworthy or guard exhausted')
        return guard


def contained(inner,outer):
    return outer[0]<=inner[0] and inner[1]<=outer[1]


def windows(identity,jitter,budget=Budget()):
    uint(identity,64,True)
    if type(jitter) is not int or not -250<=jitter<=250:
        raise ValueError('STATUS jitter outside +/-250us')
    guard=budget.guard()
    start=90000+(identity%8)*2500+jitter
    status=(start,start+2000) # hypothetical bounded airtime, not measured
    for tx,rx in ((CONTROL_TX,CONTROL_RX),(status,STATUS_RX)):
        if not contained((tx[0]-guard,tx[1]+guard),rx):
            raise ValueError('guarded TX outside RX')
    occupied=sorted(TIMING+(CONTROL_RX,STATUS_RX))
    if any(a[1]>b[0] for a,b in zip(occupied,occupied[1:])):
        raise ValueError('half-duplex window collision')
    return {'control_tx':CONTROL_TX,'standby_control_rx':CONTROL_RX,
            'status_tx':status,'active_status_rx':STATUS_RX,'guard_us':guard}


@dataclass(frozen=True)
class Event:
    kind: str
    scope: tuple
    sequence: int
    origin_mono: int # original control TX start derived from ACTIVE frame phase

    def __post_init__(self):
        if self.kind not in ('YIELD','WITHDRAW') or not self.scope:
            raise ValueError('event kind/scope')
        uint(self.sequence,64,True);uint(self.origin_mono,64)
        uint(self.origin_mono+1000000,64)

    @property
    def copies(self): return tuple(self.origin_mono+i*FRAME for i in range(3))

    @property
    def deadline(self): return self.origin_mono+(750000 if self.kind=='YIELD' else 1000000)

    def dispatch(self,copy,now,budget=Budget()):
        budget.guard();uint(now,64)
        if type(copy) is not int or not 0<=copy<3: raise ValueError('copy index')
        # Bounded dispatch lateness stays in its ORIGINAL guarded opportunity.
        nominal=self.copies[copy]
        return nominal<=now<=nominal+budget.scheduling_us and now+2000<self.deadline


def create_event(kind,scope,sequence,frame_start,budget=Budget()):
    budget.guard();uint(frame_start,64)
    return Event(kind,scope,sequence,uint(frame_start+CONTROL_TX[0],64))


class EventBook:
    """Idempotence only, AFTER codec/freshness/current-scope/armed-HOT checks.

    Does not authorize takeover. Original bytes/event body must be immutable.
    """
    def __init__(self):
        self.seen=set();self.terminal=set();self.pending={}

    def receive(self,event,now):
        uint(now,64)
        if not event.origin_mono<=now<event.deadline or event in self.seen or event.scope in self.terminal:
            return False
        self.seen.add(event)
        if event.kind=='WITHDRAW':
            self.terminal.add(event.scope);self.pending.pop(event.scope,None)
        else:
            current=self.pending.get(event.scope)
            if current is not None: return False # cannot slide an existing boundary
            self.pending[event.scope]=event
        return True

    def can_dispatch(self,event,copy,now,budget=Budget()):
        if event.kind=='YIELD' and event.scope in self.terminal:return False
        return event.dispatch(copy,now,budget)


def blackout_first_tx_bound(last_original_contact,processing_mono,control_phase,budget=Budget()):
    """Next aligned opportunity and conservative first-TX completion bound.

    Synthetic arithmetic only; the phase and all budgets must be trustworthy.
    """
    guard=budget.guard()
    for value in (last_original_contact,processing_mono,control_phase):uint(value,64)
    earliest=max(uint(last_original_contact+2750000,64),processing_mono)
    latest=uint(last_original_contact+3000000,64)
    index=max(0,(earliest-control_phase+FRAME-1)//FRAME)
    nominal=uint(control_phase+index*FRAME,64)
    completion=uint(nominal+guard+2000,64)
    if completion>latest:raise ValueError('3s first-TX bound unachievable; never extend')
    return nominal,completion
