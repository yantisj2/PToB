"""Clock continuity evidence for a bounded radio-power experiment.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
def continuity(frames):
    seen=False;missing=backward=stale=0;last=None;max_age=0
    for f in frames:
        u=f.get('rows',{}).get('TWATCH_UI',{})
        if not u:continue
        valid=u.get('clock')=='1'
        if seen and not valid:missing+=1
        if not valid:continue
        seen=True
        utc=int(u['utc_us'])
        if last is not None and utc<last:backward+=1
        last=utc
        if u.get('ready')=='1' and u.get('usb')=='1' and u.get('asleep')=='0':
            age=max(0,int(f['begin']['request_us'])-int(u['sampled_us']))
            max_age=max(max_age,age)
            if age>3000000:stale+=1
    return dict(observed_time=seen,missing_after_acquisition=missing,backward_steps=backward,
                awake_usb_stale_samples=stale,max_awake_snapshot_age_us=max_age,
                passed=seen and not(missing or backward or stale))
