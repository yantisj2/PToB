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
    if words[0]!=0x52324631 or words[1] not in (2,3,4):raise ValueError('Invalid capsule schema')
    if zlib.crc32(struct.pack('<62I',*words[1:63]))!=words[63]:raise ValueError('Invalid capsule CRC')
    actual=struct.pack('<16I',*words[24:40]).decode('ascii')
    if actual!=source or words[2]|(words[3]<<32)!=boot:raise ValueError('Wrong failed source/boot binding')
    return decode_pa(*words[21:24])

def decode_register_capsule(words,source,boot):
    """Source175 register provenance; SCB values were read after kernel clearing.
    No inferred BFAR validity, memory contents, corruption cause or role authority.
    """
    pa=decode_capsule(words,source,boot)
    if words[1]!=4:return {'schema':words[1],'pa':pa,'registers':{'availability':'UNAVAILABLE_LEGACY'}}
    flags=words[40];basic=bool(flags&1);callee=bool(flags&2);returned=bool(flags&4)
    if flags&~31 or not flags&16 or words[62] or callee and not(basic and returned):
        raise ValueError('Invalid register flags')
    if not basic and any(words[41:57]):raise ValueError('Registers without ESF')
    if not callee and any(words[46:55]):raise ValueError('Unknown callee contains values')
    if not returned and any(words[55:57]):raise ValueError('Unknown entry contains values')
    if returned and words[56] not in (0xfffffff1,0xfffffff9,0xfffffffd):
        raise ValueError('Invalid Cortex-M basic EXC_RETURN')
    vector=words[14]&511;reason=words[4]
    if not ((vector==4 and 16<=reason<=21) or (vector==5 and 22<=reason<=28) or (vector==6 and 29<=reason<=36)):
        raise ValueError('Not a hardware memory/bus/usage fault')
    unreliable=reason in (17,18,23,24,32)
    if bool(flags&8)!=unreliable:raise ValueError('Stack provenance mismatch')
    regs=None
    if basic:
        regs={**dict(zip(['r0','r1','r2','r3','r12'],words[41:46])),
              'lr':words[6],'pc':words[5],'xpsr':words[7]}
        if callee:regs.update(zip(['r4','r5','r6','r7','r8','r9','r10','r11','psp'],words[46:55]))
    return {'schema':4,'pa':pa,'registers':{'availability':'SUPPLIED_UNRELIABLE_STACK' if unreliable else 'SUPPLIED' if basic else 'UNAVAILABLE',
        'basic_supplied':basic,'callee_provenance_checked':callee,'values':regs,
        'entry_msp':words[55] if returned else None,'exc_return':words[56] if returned else None,
        'scb_provenance':'POST_KERNEL_CLEAR_BFAR_VALIDITY_UNAVAILABLE'},
        'masks':{'saved':words[57],'primask':words[58],'control':words[59]},
        'masked_site':words[60],'masked_cycles':words[61]}

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


def decode_prepare_capsule(words,source,boot,verbose=False,context=False):
    """Source156 prepare observer; bind mode to the exact native build receipt.
    Schema3 replaces schema2 IRQ latency/interrupted-thread-frame words40..62.
    Callback/param words are opaque addresses, requiring the matching ELF.
    """
    if type(verbose) is not bool or type(context) is not bool or verbose and context:
        raise ValueError('Expected distinct explicit verbose/context modes')
    pa=decode_capsule(words,source,boot)
    out={'schema':words[1],'pa':pa}
    if words[1]!=3:
        out['prepare']={'availability':'UNAVAILABLE_SCHEMA2' if words[1]==2 else 'UNAVAILABLE_SCHEMA4_REGISTERS'}
        return out
    w=words[40:63];mode=0 if verbose else 1<<30
    valid_mode=(w[22]&(1<<30))==mode
    count=w[6]-1 if 1<=w[6]<=8 else 0
    unknown=bool(w[22]&(1<<31)) or not valid_mode or w[6]>8 or bool(w[22]&0x3fffc000)
    cut={'availability':'UNAVAILABLE_LEGACY'}
    if context and not w[6]:
        cut={'availability':'UNAVAILABLE_NO_FAILURE'}
        if any(w[:4]):unknown=True;cut={'availability':'UNKNOWN'}
    elif context:
        f=w[3]
        valid=(f>>28)==13 and not(f&(1<<27)) and bool(f&(1<<26))
        valid=valid and bool(f&(1<<22))==bool(w[1]) and bool(f&(1<<23))==bool(w[0])
        if not(f&(1<<16)) and (w[2] or f&0x0200ffff):valid=False
        if not valid:unknown=True;cut={'availability':'UNKNOWN'}
        else:
            cut={'availability':'CURRENT_CUT','owner':w[0],'abort_callback':w[1],
                'ticker_valid':bool(f&(1<<16)),'ticker_lazy':w[2]&65535,
                'ticker_req':(w[2]>>16)&255,'ticker_ack':w[2]>>24,
                'job_guard':f&255,'worker_trigger':(f>>8)&255,
                'start_pending':bool(f&(1<<17)),'stop_pending':bool(f&(1<<18)),
                'expiry_pending':bool(f&(1<<19)),'mayfly_state':(f>>20)&3,
                'mayfly_owner_equal':bool(f&(1<<24)),
                'ticker_context_equal':bool(f&(1<<25)),
                'done_sync':True,'low_latency_ull_done':False}
    elif not verbose and any(w[:4]):unknown=True;cut={'availability':'UNKNOWN_WRONG_CONTEXT_MODE'}
    availability='UNKNOWN' if unknown else 'NO_FAILURE_CAPTURED' if not w[6] else 'COMPLETE' if count==7 else 'UNKNOWN'
    out['schema3_omits']=['irq_latency','interrupted_thread_frame']
    out['prepare']={'availability':availability,'mode':'verbose' if verbose else 'failure-only','context':cut,
        'success_counters':{'state':('UNAVAILABLE' if unknown else 'ENABLED_VERBOSE') if verbose else 'DISABLED',
                            'values':w[:4] if verbose and not unknown else None},
        'raw_completion':w[6],'copied_entries':count if valid_mode else None,
        'failed_callback':w[4] if valid_mode and w[6] else None,
        'failed_param':w[5] if valid_mode and w[6] else None,
        'capture_ms':w[21] if valid_mode and w[6] else None,
        'entries':[{'callback':w[7+2*i],'param':w[8+2*i],
                    'is_resume':bool(w[22]&(1<<(2*i))),
                    'is_aborted':bool(w[22]&(2<<(2*i)))} for i in range(count if valid_mode else 0)]}
    return out
