#!/usr/bin/env python3
"""Decode source155 fault-only scalar cuts. No connection-generation inference.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import struct
import zlib
LATENCY_CANCEL_MARKER=0xC1

def u32(value):
    if type(value) is not int or not 0 <= value <= 0xffffffff:
        raise ValueError('Expected an unsigned 32-bit word')
    return value

def decode_pa(operation,result,ms):
    operation=u32(operation);ms=u32(ms)
    if type(result) is not int or not -(1<<31) <= result <= 0xffffffff:
        raise ValueError('Expected a raw or signed 32-bit result')
    raw=result & 0xffffffff
    signed=struct.unpack('<i',struct.pack('<I',raw))[0]
    marker=operation>>24
    answer={'operation':operation,'result_signed':signed,'result_raw':raw,'ms':ms}
    if marker==LATENCY_CANCEL_MARKER:
        flags=(operation>>16)&15
        answer.update(kind='LATENCY_CANCEL_FAILURE_CURRENT_SLOT',slot=(operation>>20)&15,
                      valid_slot=bool(flags&1),update_mark=bool(flags&2),
                      disable_mark=bool(flags&4),latency_cancel=bool(flags&8),
                      current_handle=operation&65535)
    else:
        answer['kind']='UNKNOWN_OPERATION_MARKER' if marker else 'PA_OPERATION'
    return answer

def decode_capsule(words,source,boot):
    """Caller supplies the failed source/boot binding, never the recovery boot."""
    if len(words)!=64 or any(type(w) is not int or not 0<=w<=0xffffffff for w in words):
        raise ValueError('Invalid capsule words')
    if words[0]!=0x52324631 or words[1] not in (2,3):raise ValueError('Invalid capsule schema')
    if zlib.crc32(struct.pack('<62I',*words[1:63]))!=words[63]:raise ValueError('Invalid capsule CRC')
    actual=struct.pack('<16I',*words[24:40]).decode('ascii')
    if actual!=source or words[2]|(words[3]<<32)!=boot:raise ValueError('Wrong failed source/boot binding')
    return decode_pa(*words[21:24])

def decode_disconnect(detail,result,incarnation,b):
    result=u32(result);incarnation=u32(incarnation);b=u32(b)
    out={'incarnation':incarnation}
    if detail==29:
        out.update(access_address_le=b,event_counter=result&65535,role=(result>>16)&1)
    elif detail==30:
        out.update(latency_event=result&65535,ull_ref=(result>>16)&255,
                   latency_cancel=bool(result&(1<<24)),update_mark=bool(result&(1<<25)),
                   disable_mark=bool(result&(1<<26)),req=b&255,ack=(b>>8)&255,lazy_current=b>>16)
    elif detail==31:
        out.update(ticker_valid=bool(result&1),context_matches=bool(result&2),
                   job_guard=(result>>8)&255,worker_trigger=(result>>16)&255,ticks_periodic=b)
    else:raise ValueError('Not a source155 disconnect detail')
    return out
