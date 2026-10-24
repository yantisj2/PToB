"""Offline scheduler amendment A1. Copyright 2026 Jonathan Yantis. MIT."""
FRAME_US=250000
MIN_RETRY_US=2000000

def uint64(x):
 if type(x)is not int or not 0<=x<2**64:raise ValueError('uint64')
 return x

def frame(utc_us):return uint64(utc_us)//FRAME_US

def reserved(n):return uint64(n)%4==0

def discovery(identity,n,eligible=True,event=False,withdrawn=False,aligned=True):
 uint64(identity);uint64(n)
 if not identity:raise ValueError('zero identity')
 if not aligned:return 'broad-discovery'
 if not reserved(n):return 'ordinary-secondary'
 if event or withdrawn or not eligible:return 'listen'
 return 'announce-copy' if (identity>>((n//4)%64))&1 else 'listen'

def response_admitted(request_tx_us):
 # Response must use the immediately following frame's secondary43ms.
 n=frame(request_tx_us)
 reply=(n+1)*FRAME_US+43000
 return not reserved(n+1) and 0<=reply+4000-request_tx_us<=250000

def retry_after(last_attempt_us,attempt):
 uint64(last_attempt_us);uint64(attempt)
 # Rotate frame residue, keeping STATUS/REQUEST transmit offset90ms.
 # First eligible frame >=2s later; add0..3 frames to hit attempt mod4.
 earliest=last_attempt_us+MIN_RETRY_US
 n=max(0,(earliest-90000+FRAME_US-1)//FRAME_US)
 n+=(attempt%4-n%4)%4
 return uint64(n*FRAME_US+90000)

def primary_event(original_us):
 uint64(original_us)
 return tuple(uint64(original_us+i*FRAME_US) for i in range(3)),uint64(original_us+750000)
