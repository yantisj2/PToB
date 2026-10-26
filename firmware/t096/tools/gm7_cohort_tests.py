#!/usr/bin/env python3
"""Unseeded multi-process production GM7 service/scheduler radio model.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.

The native controller boundary is modeled, not hardware. Only completed encoded
events enter receivers, with primary collision, own TX, scanner channel and AUX
preemption gates. This is not RF or physical timing acceptance.
"""
import argparse, collections, hashlib, json, math, os, shutil, subprocess
from pathlib import Path


def build(deps, directory, physical=False):
    app=Path(__file__).resolve().parents[1]
    tiny=deps.resolve()/'modules/crypto/tinycrypt/lib'
    directory.mkdir(parents=True,exist_ok=False)
    for name in ['kernel.h','sys/atomic.h','random/random.h','bluetooth/bluetooth.h',
                 'bluetooth/conn.h','bluetooth/gatt.h','bluetooth/hci.h']:
        f=directory/'zephyr'/name
        f.parent.mkdir(parents=True,exist_ok=True)
        f.write_text('/* Public boundary supplied by fixture. */\n')
    compiler=os.environ.get('CC') or ('/opt/homebrew/opt/llvm/bin/clang' if
        Path('/opt/homebrew/opt/llvm/bin/clang').exists() else shutil.which('clang'))
    sources=[app/'protocol'/name for name in ['gm7_codec.c','gm7_control.c',
        'gm7_owner.c','gm7_schedule.c','gm7_air.c','gm7_bridge.c','gm7_promote.c',
        'group_auth.c','gnss_diagnostics.c']]
    sources += [app/'src'/name for name in ['gm7_source.c','precision.c','ui_render.c']]
    sources += [tiny/'source'/name for name in ['hmac.c','sha256.c','utils.c']]
    sources += [app/'tests/gm7_cohort_node.c']
    command=[compiler,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter',
        '-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined',
        '-fno-sanitize-recover=all','-DR2_GM7_NATIVE=1','-DR2_GM5_NATIVE=1',
        *['-I'+str(p) for p in [app/'include',app/'protocol',app/'tests',tiny/'include',directory]],
        *map(str,sources),'-o',str(directory/'node')]
    with (directory/'compile.log').open('x') as log:
        subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=45)
    inputs=sources+[app/'src/gm7_service.c',app/'tests/gm7_service_test.c',
        app/'tests/gm7_service_fake.h',Path(__file__).resolve()]
    (directory/'build.json').write_text(json.dumps(dict(command=command,
        inputs={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}),indent=2)+'\n')
    if physical:
        fixture=(app/'tests/radio_service_test.c').read_text().replace(
            'int main(int argc,char **argv)','int historical_radio_main(int argc,char **argv)')
        fixture=fixture.replace('capture_tick,1,R2_CAPTURE_NOW','capture_tick,4,R2_CAPTURE_NOW')
        assert fixture.endswith('}\n')
        (directory/'gm7_cohort_radio_fixture.inc').write_text(fixture[:-2]+' return 0;\n}\n')
        sources=[app/'tests/gm7_cohort_pt.c']
        sources += [app/'src'/n for n in ['radio_precision.c','relay_precision.c','core.c','clock.c','nmea.c','precision.c']]
        sources += [app/'protocol'/n for n in ['gm7_promote.c','gnss_diagnostics.c',
            'role_announcement.c','group_join.c','group_auth.c','group_control.c',
            'group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c']]
        sources += [tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]
        pt_command=command[:command.index(str(app/'protocol/gm7_codec.c'))]+list(map(str,sources))+['-o',str(directory/'pt')]
        with (directory/'pt.compile.log').open('x') as log:
            subprocess.run(pt_command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=45)
    return directory/'node'


def run(binary, count, phases, duration, loss=0, preempt=0, connect_after=0, physical=False, legacy_scan=False, event_mode=None, owner_delays=(1000,), announce_after=0):
    procs = [subprocess.Popen([str(binary)]+(['--physical'] if physical else []), stdin=subprocess.PIPE,
             stdout=subprocess.PIPE, text=True, bufsize=1) for _ in range(count)]
    states = [None] * count
    precision_states=[None]*count
    jobs = {}
    completed = set()
    reasons = collections.Counter()
    links=collections.defaultdict(collections.Counter)
    events = []
    origins = [0, 777777, 2312345, 9301999]
    # Same low61 identity bits exercise the old A1 long collision interval.
    identities = [1, (1 << 63) | 1, (1 << 62) | 1, (1 << 61) | 1]
    pts=[subprocess.Popen([str(binary.parent/'pt')],stdin=subprocess.PIPE,
         stdout=subprocess.PIPE,text=True,bufsize=1) for _ in range(count)] if physical else []
    pt_states=[None]*count
    payloads=[None]*count
    g1_jobs={}
    g1_seen=set()
    next_owner=[0]*count
    poll_index=[0]*count
    before_restore=None

    def send(i, command):
        procs[i].stdin.write(command + '\n')

    def read(i):
        result = []
        procs[i].stdin.flush()
        while True:
            line = procs[i].stdout.readline().strip()
            if line == 'E':
                return result
            if not line:
                raise RuntimeError(f'node{i} exited {procs[i].poll()}')
            if line.startswith('M '):
                assert physical
                pts[i].stdin.write(line+'\n');pts[i].stdin.flush()
                reply=pts[i].stdout.readline().strip();assert reply.startswith('M '),reply
                assert pts[i].stdout.readline().strip()=='E'
                send(i,reply);procs[i].stdin.flush()
                events.append([now,i,'promotion_result',int(reply.split()[1])])
            else: result.append(line.split())

    try:
        for i in range(count):
            send(i, f'I {identities[i]} {900+i} {origins[i]} {phases[i]}')
            read(i)
        first_one = None
        converged_since = None
        held_one = False
        event_triggered=target=successor=None;trigger_result=None;retired=set();prior_hot=None
        selected_originals=[0]*count;confirmed=[None]*count;last_positive=[None]*count
        last_positive_before=None;last_selected_before=None;terminal_ads=set()
        terminal_meta=[];terminal_completed=[];terminal_ordinal=0
        first_new_g1_end=first_new_g1_proof_end=None;phase_report=[None]*count
        for now in range(66000000, 66000000 + duration * 1000000, 1000):
            for key,job in list(g1_jobs.items()):
                if job['end']>now: continue
                src,frame=key
                del g1_jobs[key]
                if not pt_states[src] or not pt_states[src][0]: continue
                q=precision_states[src]
                if not q or not q[0] or q[3]!=job['train'] or job['end']+origins[src]>q[2]:
                    reasons['g1_permission_or_scope_cancel']+=1;continue
                event=frame%65536
                tick=(job['start']+40+origins[src])*16
                pts[src].stdin.write(f'X {tick} {(now+origins[src])*16} {event} {job["hex"]}\n')
                reasons['g1_tx']+=1
                if event_triggered and src==successor and first_new_g1_end is None:first_new_g1_end=job['end']
                for dst in range(count):
                    if dst==src or not states[dst] or states[dst][2]!=identities[src]: continue
                    blocked=any(k[0]==dst and j['start']<job['end'] and j['end']>job['start'] for k,j in jobs.items())
                    if blocked:
                        reasons['g1_own_management']+=1;continue
                    if loss and (frame+dst)%loss==0:
                        reasons['g1_loss']+=1;continue
                    pts[dst].stdin.write(f'A {identities[src]}\n')
                    tick=(job['start']+40+origins[dst])*16
                    pts[dst].stdin.write(f'R {tick} {(now+origins[dst])*16} {event} {job["hex"]}\n')
                    reasons['g1_delivered']+=1
            due = [(key, job) for key, job in jobs.items()
                   if job['end'] <= now and key not in completed]
            for key, job in due:
                src, slot, ticket = key
                completed.add(key)
                send(src, f'C {slot} {ticket} 1')
                reasons['completed_tx'] += 1
                if job['kind']==1:
                    wire=bytes.fromhex(job['hex'])[4:]
                    seq=int.from_bytes(wire[40:48],'big')
                    if not last_positive[src] or last_positive[src]['sequence']!=seq:
                        last_positive[src]=dict(sequence=seq,sample_us=int.from_bytes(wire[150:158],'big')-origins[src],first_rf_start=job['start'],first_rf_end=job['end'])
                terminal_copy=event_triggered and src==target and job['hex'] in terminal_ads
                if terminal_copy:
                    terminal_ordinal+=1
                    terminal_completed.append(dict(start=job['start'],end=job['end'],hex=job['hex'],ordinal=terminal_ordinal))
                for dst in range(count):
                    if dst == src:
                        continue
                    link=links[f'{src}->{dst}'];link['attempted']+=1
                    reason = None
                    if now < 66000000+connect_after*1000000:
                        reason='partition'
                    # A full event's TX blocks primary acquisition/AUX follow.
                    for other_key, other in jobs.items():
                        if other_key == key:
                            continue
                        overlap = other['start'] < job['end'] and other['end'] > job['start']
                        primary_collision = (other['start'] < job['start'] + 184 and
                                             other['start'] + 184 > job['start'])
                        if overlap and other_key[0] == dst:
                            reason = 'own_tx'
                        if primary_collision:
                            reason = 'primary_collision'
                    if not reason and legacy_scan and states[dst] and not states[dst][10]:
                        # Stock unaligned scanner rotates37/38/39 each20ms.
                        if (job['start'] + origins[dst]) // 20000 % 3:
                            reason = 'scan_channel'
                    trial=hashlib.sha256(b'GM7-S1-radio-loss'+
                        reasons['completed_tx'].to_bytes(8,'big')+bytes([src,dst])).digest()
                    loss_trial=int.from_bytes(trial[:8],'big')
                    preempt_trial=int.from_bytes(trial[8:16],'big')
                    if job['kind']==1 and now<66000000+announce_after*1000000:
                        reason='intentional_ANN_loss'
                    if not reason and loss and loss_trial % loss == 0:
                        reason = 'loss'
                    if not reason and preempt and preempt_trial % preempt == 0:
                        reason = 'aux_preempt'
                    if event_triggered and dst in retired:
                        reason='receiver_closed'
                    if event_triggered and src==target:
                        if event_mode=='blackout':reason='blackout_all_communication'
                        elif terminal_copy and ((event_mode.endswith('-first') and terminal_ordinal!=1) or (event_mode.endswith('-last') and terminal_ordinal!=3)):
                            reason='intentional_terminal_copy_loss'
                    if not reason and states[dst] and states[dst][0]:
                        # Ordinary G1 owns its local phase at0/62.5ms for a
                        # conservative3ms. Competing management AUX cannot
                        # steal that radio reservation.
                        begin=job['start']+origins[dst]-(precision_states[dst][1] if precision_states[dst] else 0)
                        end=job['end']+origins[dst]-(precision_states[dst][1] if precision_states[dst] else 0)
                        if begin//62500 != end//62500 or begin%62500<3000:
                            reason='g1_preempt'
                    if reason:
                        reasons[reason] += 1
                        link[reason]+=1
                    else:
                        send(dst, f'D {job["end"]} {job["hex"]}')
                        reasons['delivered'] += 1
                        link['delivered']+=1
                        link['kind_'+str(job['kind'])]+=1
                        if len(events) < 500:
                            events.append([job['end'], src, dst, job['kind'], 'delivered'])
            new_jobs = {}
            for i in range(count):
                if now<next_owner[i]:
                    for key,job in jobs.items():
                        if key[0]==i and key not in completed:new_jobs[key]=job
                    continue
                next_owner[i]=now+owner_delays[poll_index[i]%len(owner_delays)]
                poll_index[i]+=1
                send(i, f'T {now}')
                for row in read(i):
                    if row[0] == 'S':
                        new_state = list(map(int, row[1:]))
                        if not legacy_scan:
                            assert len(new_state)==14, 'S2 requires current-control schema14'
                        if states[i] != new_state and (states[i] is None or states[i][:5] != new_state[:5]):
                            events.append([now, i, 'state', new_state])
                        states[i] = new_state
                    elif row[0] == 'V':
                        reasons['stage_'+row[4]+'_rc_'+row[2]] += 1
                        if len(events) < 10000:
                            events.append([now, i, 'journal', list(map(int,row[1:]))])
                    elif row[0] == 'O':
                        pts[i].stdin.write('B '+row[1]+'\n')
                        pts[i].stdin.flush()
                        payloads[i]=None
                        while True:
                            line=pts[i].stdout.readline().strip()
                            if line=='E': break
                            if not line: raise RuntimeError(f'PT{i} exited {pts[i].poll()}')
                            fields=line.split()
                            if fields[0]=='P': send(i,'P '+fields[1])
                            elif fields[0]=='Q': pt_states[i]=list(map(int,fields[1:]))
                            elif fields[0]=='L':
                                if event_triggered:events.append([now,i,'pt_lifecycle',list(map(int,fields[1:]))])
                            elif fields[0]=='Z':
                                z=list(map(int,fields[1:]))
                                if event_triggered and phase_report[i]!=z:
                                    events.append([now,i,'pt_phase',z]);phase_report[i]=z
                                if event_triggered and i==successor and z[1] and z[4] and first_new_g1_proof_end is None:
                                    first_new_g1_proof_end=(z[5]+15)//16-origins[i]
                            elif fields[0]=='G': payloads[i]=fields[1]
                            else: raise ValueError(fields)
                    elif row[0]=='C':
                        if event_triggered:events.append([now,i,'takeover_phase',list(map(int,row[1:]))])
                    elif row[0]=='K':
                        if event_triggered:events.append([now,i,'service_calls',list(map(int,row[1:]))])
                    elif row[0]=='H':confirmed[i]=list(map(int,row[1:]))
                    elif row[0]=='F':
                        selected_originals[i]=int(row[1])-origins[i] if int(row[1]) else 0
                        if event_triggered:events.append([now,i,'handover_bounds',list(map(int,row[1:]))])
                    elif row[0]=='N':
                        meta=[i,*map(int,row[1:7]),row[7]]
                        if meta not in terminal_meta:terminal_meta.append(meta)
                        if i==target:terminal_ads.add(row[7])
                    elif row[0] == 'A':
                        precision_states[i]=list(map(int,row[1:]))
                    elif row[0] == 'J':
                        slot, ticket, start, end, positive, kind = map(int, row[1:7])
                        new_jobs[i, slot, ticket] = dict(start=start, end=start+1400,
                            latest=end, positive=positive, kind=kind, hex=row[7])
                    else:
                        raise ValueError(row)
            # Preserve just-finished radio intervals through their completion
            # cohort, and drop canceled/unowned future work immediately.
            jobs = {key: job for key, job in jobs.items()
                    if job['end'] > now-4000 and key in completed} | new_jobs
            if physical:
                for i in range(count):
                    if not payloads[i]: continue
                    q=precision_states[i]
                    if not q or not q[0] or not q[4]:continue
                    start=q[5]-origins[i];frame=(q[5]-q[1])//62500
                    if 3000<=start-now<=7000 and (i,frame) not in g1_seen:
                        g1_seen.add((i,frame))
                        g1_jobs[i,frame]=dict(start=start,end=start+1800,hex=payloads[i],train=q[3])
            if event_mode and event_triggered is None and now>=196000000:
                leaders=[i for i,s in enumerate(states) if s[0]]
                hots=[i for i,s in enumerate(states) if s[1] and pt_states[i] and pt_states[i][4]]
                if len(leaders)==1 and len(hots)==1 and confirmed[leaders[0]] and confirmed[leaders[0]][0]==identities[hots[0]] and confirmed[leaders[0]][2]:
                    target=leaders[0];prior_hot=hots[0];successor=prior_hot;event_triggered=now
                    if event_mode=='cold-fallback':
                        retired={target,prior_hot}
                        successor=min((i for i in range(count)if i not in retired),key=lambda i:identities[i])
                    elif not event_mode.startswith('yield'):retired={target}
                    last_positive_before=last_positive[target];last_selected_before=selected_originals[successor]
                    send(target,'Y' if event_mode.startswith('yield') else 'W');trigger_result=read(target)
                    if event_mode=='cold-fallback':
                        send(prior_hot,'W');read(prior_hot)
            if announce_after and before_restore is None and now>=66000000+announce_after*1000000:
                before_restore=[list(state)for state in states]
            active = sum(s[0] for s in states)
            serving = next((i for i,s in enumerate(states) if s[0]), None)
            live=[i for i in range(count)if i not in retired]
            converged = active == 1 and all(states[i][2] == identities[serving] and
                states[i][3:5] == states[serving][3:5] and
                states[i][11:13] == states[serving][11:13] and
                states[i][13] for i in live)
            if converged:
                first_one = first_one or now
                converged_since = converged_since or now
                held_one |= now - converged_since >= 5000000
            else:
                converged_since = None
        return dict(owner_delays_us=owner_delays,announce_after_s=announce_after,states_before_ANN_restore=before_restore,event=event_mode,event_triggered=event_triggered,target=target,successor=successor,prior_hot=prior_hot,retired=sorted(retired),
                    trigger_result=trigger_result,terminal_meta=terminal_meta,terminal_completed=terminal_completed,
                    last_positive_before=last_positive_before,last_positive_final=last_positive[target] if target is not None else None,
                    last_selected_before=last_selected_before,first_new_g1_end=first_new_g1_end,
                    first_new_g1_proof_end=first_new_g1_proof_end,
                    stopped_target_no_output=(not states[target][0] and not pt_states[target][0]) if target is not None else None,
                    nodes=count, phases=phases, origins=origins[:count],
                    state_schema_columns=len(states[0]),
                    links={k:dict(v) for k,v in links.items()},
                    states=[dict(zip(['serving','hot','active','birth','term','control_sequence',
                        'mac','accepted','rejected','error','scan_aligned','founder','active_boot','control_current'],s))
                        for s in states], counters=dict(reasons),
                    first_one_us=first_one, held_one_5s=held_one,
                    final_converged=converged, final_hold_us=(now-converged_since) if converged_since else 0,
                    pt_states=pt_states, scanner='LEGACY_ROTATING' if legacy_scan else 'S2_CONTINUOUS_GM37',
                    events=events, duration_s=duration,
                    limits='Actual production service/owner/codec/scheduler'+(' and PT/servo/promotion' if physical else '')+'; modeled native completion, scan/AUX, capture/RF and collisions. No physical RF, independent UTC or hardware HOT acceptance.')
    finally:
        for p in procs:
            p.stdin.close()
            p.wait(timeout=5)
        for p in pts:
            p.stdin.close();p.wait(timeout=5)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path)
    p.add_argument('--deps', type=Path)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--duration', type=int, default=100)
    p.add_argument('--nodes', type=int, default=4)
    p.add_argument('--phases', default='0,0,0,0')
    p.add_argument('--loss-mod',type=int,default=0)
    p.add_argument('--preempt-mod',type=int,default=0)
    p.add_argument('--connect-after',type=int,default=0)
    p.add_argument('--physical',action='store_true')
    p.add_argument('--legacy-scan',action='store_true')
    p.add_argument('--owner-delays-us',default='1000',help='Repeating owner cadence; radio endpoints remain independent1ms model steps')
    p.add_argument('--announce-after',type=int,default=0,help='Drop all ANN copies until this elapsed second')
    p.add_argument('--event',choices=['yield','yield-first','yield-last','withdraw','withdraw-first','withdraw-last','blackout','cold-fallback'])
    a = p.parse_args()
    if not a.binary:
        assert a.deps,'--deps or --binary is required'
        a.binary=build(a.deps,a.out.resolve().with_suffix('.build'),a.physical)
    result = run(a.binary.resolve(), a.nodes, list(map(int,a.phases.split(','))), a.duration,
                 a.loss_mod,a.preempt_mod,a.connect_after,a.physical,a.legacy_scan,a.event,tuple(map(int,a.owner_delays_us.split(','))),a.announce_after)
    result['binary_sha256'] = hashlib.sha256(a.binary.read_bytes()).hexdigest()
    if a.physical:
        result['pt_binary_sha256']=hashlib.sha256((a.binary.parent/'pt').read_bytes()).hexdigest()
        result['bridge_sha256']=hashlib.sha256((Path(__file__).resolve().parents[1]/'tests/gm7_cohort_bridge.h').read_bytes()).hexdigest()
        app=Path(__file__).resolve().parents[1]
        result['fixture_sha256']={str(f.relative_to(app)):hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [app/'tests/gm7_cohort_node.c',app/'tests/gm7_cohort_pt.c',
                      app/'tests/gm7_service_test.c',app/'tests/gm7_service_fake.h',
                      app/'tests/radio_service_test.c',Path(__file__).resolve()]}
    a.out.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('events',)}))


if __name__ == '__main__':
    main()
