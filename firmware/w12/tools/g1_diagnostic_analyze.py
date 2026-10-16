#!/usr/bin/env python3
"""Offline causal G1 stage review; API observations are never RF stamps."""
import argparse,collections,hashlib,json,re
from pathlib import Path

def analyze(data):
    attempts={};scope=[];summaries=[];phases=[];sync=[];counts=collections.Counter()
    for number,line in enumerate(data.decode('utf8','replace').splitlines(),1):
        if line.startswith('W12_V12_LOG '):
            match=re.fullmatch(r'W12_V12_LOG capture_id=[0-9a-f]{16} log_seq=\d+ enqueue_us=\d+ record=(.*)',line)
            if not match:counts['malformed_envelope']+=1;continue
            line=match[1]
        kind=line.split(' ',1)[0]
        if kind not in ('W12_G1_LINK','W12_G1_SCOPE','W12_G1_DIAG','W12_G1_FILTER','W12_G1_TIME','W12_G1_PEER'):continue
        row=dict(re.findall(r'(\w+)=([^ ]+)',line));row['raw_line']=number;counts[kind]+=1
        if kind=='W12_G1_SCOPE':scope.append(row)
        elif kind!='W12_G1_LINK':summaries.append({'kind':kind,**row})
        else:
            if row['event'] in ('COLD_PHASE','PERMISSION_PHASE'):phases.append(row);continue
            if row['event'].startswith('SYNC_'):sync.append(row);continue
            # Version2 installed before this label change already retains the
            # terminating-owner bit. Normalize it without rewriting raw bytes.
            if row['event']=='CONNECT' and row.get('rc')=='0' and int(row.get('owners','0'),16)&32:
                row['event']='CONNECT_DRAIN_ACK';row['original_event']='CONNECT'
            key=int(row['gen']);attempts.setdefault(key,[]).append(row)
    latency=[]
    for generation,events in attempts.items():
        by=collections.defaultdict(list)
        for row in events:by[row['event']].append(row)
        def at(name):return int(by[name][0]['at_us']) if by[name] else None
        request=at('CONNECT_REQUEST');descriptor=at('ACL_DESCRIPTOR');connect=at('CONNECT');terminate=at('TERMINATE_ACK');disconnect=at('DISCONNECT')
        def delta(start,end):return end-start if start is not None and end is not None else None
        start=request-int(by['CONNECT_REQUEST'][0]['window_us']) if request is not None else None
        closure=delta(start,disconnect)
        latency.append({'generation':generation,'peer':events[0].get('peer'),'request_to_descriptor_us':delta(request,descriptor),
            'descriptor_to_CONNECT_us':delta(descriptor,connect),'request_to_CONNECT_us':delta(request,connect),
            'terminate_to_DISCONNECT_us':delta(terminate,disconnect),'DISCONNECT_window_us':closure,
            'quiet_violation_observed':closure is not None and closure>=500000,
            'controller_api_results':[{'event':r['event'],'rc':int(r['rc']),'raw_line':r['raw_line']} for r in events if r['event'] in ('CONNECT','CONNECT_DRAIN_ACK','DISCONNECT','CONNECT_ACK','TERMINATE_ACK','CANCEL_ACK','LOCATOR','PUBLIC_MARKER','JOIN_TX')],
            'events':events})
    sync_attempts=[];current=None
    for row in sync:
        if row['event']=='SYNC_REQUEST':
            if current is not None:sync_attempts.append(current)
            start=int(row['at_us'])-int(row['window_us'])
            current={'request_line':row['raw_line'],'peer':row.get('peer'),'window_start_us':start,'events':[],'closed_at_us':None}
        if current is not None:
            current['events'].append(row)
            if row['event']=='SYNC_LOST' or (row['event']=='SYNC_ESTABLISHED' and int(row['rc'])!=0):
                current['closed_at_us']=int(row['at_us']);sync_attempts.append(current);current=None
    if current is not None:sync_attempts.append(current)
    for attempt in sync_attempts:
        at=attempt['closed_at_us'];attempt['closure_window_us']=None if at is None else at-attempt['window_start_us']
        attempt['quiet_violation_observed']=any(int(r['at_us'])>=attempt['window_start_us']+500000 for r in attempt['events'])
    acl_quiet=sum(x['quiet_violation_observed'] for x in latency);sync_quiet=sum(x['quiet_violation_observed'] for x in sync_attempts)
    def maximum(field):
        values=[x[field] for x in latency if x[field] is not None]
        return max(values) if values else None
    return {'schema':1,'counts':dict(counts),'latency_max_us':{k:maximum(k) for k in ('request_to_descriptor_us','descriptor_to_CONNECT_us','terminate_to_DISCONNECT_us','DISCONNECT_window_us')},
        'quiet_violations':acl_quiet+sync_quiet,'quiet_violations_acl':acl_quiet,'quiet_violations_sync':sync_quiet,'sync_attempts':sync_attempts,'attempts':latency,'scope_transitions':scope,'phase_changes':phases,'sync_transitions':sync,'summaries':summaries,
        'reference':'PUBLIC_SDK_AND_APPLICATION_API_OBSERVATIONS;NOT_PHYSICAL_RF_TIMESTAMPS',
        'missing_stages':'RETAINED;ABSENCE_IS_NOT_A_PASS','precision':'UNQUALIFIED'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);p.add_argument('--out',type=Path,required=True);args=p.parse_args()
    index=json.loads((args.run/'rolling-capture.json').read_text());assert index['status']=='WRITERS_STOPPED'
    parts=[]
    for row in index['segments']:
        data=(args.run/row['file']).read_bytes();assert len(data)==row['bytes'] and hashlib.sha256(data).hexdigest()==row['sha256'];parts.append(data)
    data=b''.join(parts);result=analyze(data);result.update(raw_sha256=hashlib.sha256(data).hexdigest(),analyzer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    with args.out.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('attempts','summaries','scope_transitions','phase_changes','sync_transitions','sync_attempts')},indent=2))
if __name__=='__main__':main()
