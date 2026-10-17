// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import Foundation

/// UTE1 continuity is a separately labelled local clock, never a refclock feed.
/// Host extrapolation is nominal and unqualified. Device records remain raw.
final class USBContinuousEstimate {
    var record: [String:Any]?, received: UInt64 = 0, estimateUTC: Int64 = 0
    var deviceTick: UInt64 = 0, discipline: UInt64 = 0, transport: String?
    var sourceScope: String?, segments = 0, discontinuities = 0, rejected = 0
    var anchorAt: UInt64 = 0, anchorUTC: Int64 = 0, slew: Int64 = 0
    var slewDuration: UInt64 = 2_000_000_000
    var recoveryCoherent = false
    var corroborations = 0, candidateGeneration: NSNumber?, candidateAt: UInt64 = 0, candidateDelta: Int64 = 0
    var lastReadAt: UInt64 = 0, lastReadUTC: Int64 = 0, disputed = false
    var transitionAt: UInt64 = 0
    var counterOK = false, session = false, root = false
    func ingest(_ helper: [String:Any], now: UInt64, observedAt: UInt64? = nil, allowMap: Bool = true) {
        guard let d=helper["local_estimate"] as? [String:Any] else {return}
        func u(_ k:String)->UInt64? {(d[k] as? String).flatMap(UInt64.init)}
        guard d["contract"] as? String == "UTE1", d["schema"] as? Int == 1,
              let silicon=d["silicon"] as? String, let boot=d["boot"] as? String,
              let usb=d["usb_epoch"] as? NSNumber, let capture=d["capture_epoch"] as? NSNumber else {rejected += 1;return}
        let identity="\(silicon):\(boot):\(usb):\(capture)"
        if let old=transport, old != identity {record=nil;received=0;counterOK=false;discontinuities += 1}
        transport=identity
        session=d["session_current"] as? Bool == true;root=d["root_envelope_current"] as? Bool == true
        // A missing/raced USB observation is not a counter discontinuity.
        if d["counter_valid"] as? Bool != true {
            session=false;root=false
            if d["estimate_reason"] as? Int == 4 {counterOK=false;record=nil;received=0}
            return
        }
        counterOK=true
        session=d["session_current"] as? Bool == true;root=d["root_envelope_current"] as? Bool == true
        // Transport facts remain actionable when no trustworthy host timestamp
        // exists. Such a reply must never re-anchor the local estimate.
        guard allowMap, d["estimate_present"] as? Bool == true else {return}
        guard let tick=u("tick"), let current=u("current_tick"), current>=tick,current-tick<16000,
              let last=u("last_discipline_tick"),last<=tick,
              let value=(d["estimate_utc_ns"] as? String).flatMap(Int64.init),value>0,
              let rootID=d["root"] as? String,rootID != "0000000000000000",
              let rootBoot=d["root_boot"] as? String,let epoch=d["source_epoch"] as? NSNumber,
              let path=d["path_scope"] as? String,let generation=d["map_generation"] as? NSNumber else {rejected += 1;return}
        let scope="\(rootID):\(rootBoot):\(epoch):\(path)"
        let at=observedAt ?? now
        guard at<=now,now-at<=100_000_000 else {rejected += 1;return}
        if received>0 && (tick<deviceTick || now<received) {counterOK=false;record=nil;received=0;discontinuities += 1;return}
        let changed=sourceScope != nil && sourceScope != scope
        let previous=projection(now:at)
        let oldGeneration=record?["map_generation"] as? NSNumber
        if changed {segments += 1;discontinuities += 1;transitionAt=now;lastReadAt=0;disputed=false;recoveryCoherent=false}
        if record==nil || changed {
            anchorAt=at;anchorUTC=value;slew=0;lastReadAt=0
            if record==nil {segments += 1}
        } else if oldGeneration != generation,let previous=previous {
            let delta=value.subtractingReportingOverflow(previous)
            if delta.overflow {disputed=true;rejected += 1;session=false;root=false;return}
            let targetChange=delta.partialValue.subtractingReportingOverflow(candidateDelta)
            let coherent=recoveryCoherent && !targetChange.overflow && targetChange.partialValue<=250_000 && targetChange.partialValue >= -250_000
            if (delta.partialValue > 1_000_000 || delta.partialValue < -1_000_000) && !coherent {
                // Engineering coherence gate, not an accuracy bound. Three
                // distinct maps over>=125ms can recover a long held drift or
                // coarse->causal host association without requiring a new root.
                let change=delta.partialValue.subtractingReportingOverflow(candidateDelta)
                if corroborations==0 || change.overflow || change.partialValue>250_000 || change.partialValue < -250_000 {
                    corroborations=0;candidateAt=at;candidateDelta=delta.partialValue
                }
                if candidateGeneration != generation {corroborations += 1;candidateGeneration=generation}
                if corroborations<3 || at<candidateAt || at-candidateAt<125_000_000 ||
                   delta.partialValue>Int64.max/2000 || delta.partialValue < -Int64.max/2000 {
                    disputed=true;rejected += 1;session=false;root=false;return
                }
            }
            disputed=false;recoveryCoherent=true;candidateDelta=delta.partialValue
            corroborations=0;candidateGeneration=nil
            // Preserve phase at the causal observation time, remove at most
            //1ms over2s (500ppm), never jump to a delayed response arrival.
            anchorAt=at;anchorUTC=previous;slew=delta.partialValue
            slewDuration=max(2_000_000_000,UInt64(abs(slew))*2000)
        }
        record=d;received=at;estimateUTC=value;deviceTick=tick;discipline=last;sourceScope=scope
        session=d["session_current"] as? Bool == true;root=d["root_envelope_current"] as? Bool == true
    }
    func ageMS(now:UInt64)->Double {
        Double(deviceTick>=discipline ? deviceTick-discipline : 0)/16000 + Double(now>=received ? now-received : 0)/1e6
    }
    func inheritedMapAgeMS(now:UInt64)->Double? {
        guard let d=record, let age=d["map_age_us"] as? NSNumber, age.uint64Value<UInt32.max,
              let anchor=(d["anchor_tick"] as? String).flatMap(UInt64.init), deviceTick>=anchor,
              let rate=d["map_ppb"] as? NSNumber, abs(rate.doubleValue)<=300000,
              now>=received else {return nil}
        let elapsed=Double(deviceTick-anchor)*62.5/(1+rate.doubleValue/1e9)
        return age.doubleValue/1000+elapsed/1e6+Double(now-received)/1e6
    }
    private func projection(now:UInt64)->Int64? {
        guard counterOK,record != nil,anchorAt>0,now>=anchorAt,now-anchorAt<=UInt64(Int64.max) else {return nil}
        let elapsed=now-anchorAt
        let fraction=Double(min(elapsed,slewDuration))/Double(slewDuration)
        let correction=Int64((Double(slew)*fraction).rounded())
        let (value,overflow)=anchorUTC.addingReportingOverflow(Int64(elapsed))
        let adjusted=value.addingReportingOverflow(correction)
        return overflow || adjusted.overflow ? nil:adjusted.partialValue
    }
    func utc(now:UInt64)->Int64? {
        guard var value=projection(now:now) else {return nil}
        // Presentation clock remains monotonic across compatible map updates.
        // Any contradictory source is quarantined above, never refclock-fed.
        if lastReadAt>0,now>=lastReadAt {
            let floor=lastReadUTC.addingReportingOverflow(Int64((now-lastReadAt)/2))
            if floor.overflow {return nil};value=max(value,floor.partialValue)
        }
        if now>=lastReadAt {lastReadAt=now;lastReadUTC=value}
        return value
    }
    func state(now:UInt64)->String {
        guard utc(now:now) != nil else {return "UNAVAILABLE"}
        // Permission bits are snapshots. Once records stop arriving the host
        // estimate remains local, never implicitly renews device authority.
        let current=now>=received && now-received<250_000_000
        let tracking=ageMS(now:now)<250 ? "TRACKING":"HOLDOVER"
        let change=transitionAt>0 && now>=transitionAt && now-transitionAt<500_000_000 ? "DISCONTINUITY / ":""
        if disputed {return "DISPUTED / HOLDOVER / LOCAL ONLY"}
        return change+tracking + (session && root && current ? "":" / LOCAL ONLY")
    }
    func sameTransport(scope:String?)->Bool {
        guard counterOK,record != nil,let old=scope,let record=record,
              let silicon=record["silicon"] as? String, let boot=record["boot"] as? String else {return false}
        guard let usb=record["usb_epoch"] as? NSNumber,let capture=record["capture_epoch"] as? NSNumber else {return false}
        return old == "\(silicon):\(boot):\(usb):\(capture):"+(sourceScope ?? "")
    }
    func snapshot(now:UInt64)->[String:Any] {
        ["contract":"UTE1","state":state(now:now),"utc_ns":utc(now:now).map(String.init) as Any? ?? NSNull(),
         "observation_age_ms":ageMS(now:now),"observation_age_basis":"RECEIVER_DISCIPLINE",
         "inherited_map_age_ms":inheritedMapAgeMS(now:now) as Any? ?? NSNull(),"uncertainty":"UNKNOWN","measurement":false,
         "source_scope":sourceScope as Any? ?? NSNull(),"segments":segments,"discontinuities":discontinuities,
         "rejected":rejected,"corroborations":corroborations,"slew_ns":String(slew),"slew_duration_ns":String(slewDuration),"session_snapshot_current":session,"root_envelope_snapshot_current":root,
         "last_device_record":record as Any? ?? NSNull(),"chrony_feed":false]
    }
}

func usbContinuousEstimateTests()->Int {
    var checks=0
    func check(_ ok:Bool,_ label:String){if !ok{fatalError("UTE1: "+label)};checks += 1}
    let base:Int64=1_700_000_000_000_000_000
    func sample(_ tick:UInt64,_ utc:Int64,_ generation:Int=1)->[String:Any]{
        ["local_estimate":["contract":"UTE1","schema":1,"silicon":"1000000000000004","boot":"0000000000000063","usb_epoch":7,"capture_epoch":1,"counter_valid":true,"estimate_present":true,"session_current":true,"root_envelope_current":true,"tick":String(tick),"current_tick":String(tick+100),"last_discipline_tick":String(tick-1000),"estimate_utc_ns":String(utc),"root":"0000000000000011","root_boot":"0000000000000012","source_epoch":19,"path_scope":"00000000000000000000000000000001","map_generation":generation,"estimate_reason":0]]
    }
    let c=USBContinuousEstimate();c.ingest(sample(16_000_000,base),now:1_000_000_000,observedAt:1_000_000_000)
    let before=c.utc(now:1_015_500_000)!
    c.ingest(sample(16_240_000,base+15_000_000),now:1_016_000_000,observedAt:1_015_000_000)
    check(c.utc(now:1_016_000_000)!>before,"variable arrival latency")
    var previous=c.utc(now:1_016_000_000)!
    for i in 1...8448 {let now=1_016_000_000+UInt64(i)*15_625_000;let next=c.utc(now:now);check(next != nil && next!>previous,"132s uninterrupted local estimate");previous=next!}
    check(c.ageMS(now:133_016_000_000)>132000 && c.state(now:133_016_000_000).contains("LOCAL ONLY"),"age/authority never renewed by polling")
    check(c.snapshot(now:133_016_000_000)["chrony_feed"] as? Bool == false,"no synthetic observations")
    let d=USBContinuousEstimate();d.ingest(sample(16_000_000,base),now:1_000_000_000)
    for i in 0..<16 {let dt:UInt64=132_000_000_000+UInt64(i)*62_500_000;d.ingest(sample(16_000_000+dt*16/1000,base+Int64(dt)+2_000_000,i+2),now:1_000_000_000+dt)}
    check(!d.disputed && d.slewDuration>=2_000_000_000,"coherent same-scope reacquisition")
    var missing=sample(16_000_000,base);var value=missing["local_estimate"] as! [String:Any]
    value["counter_valid"]=false;value["estimate_present"]=false;value["estimate_reason"]=2;missing["local_estimate"]=value
    d.ingest(missing,now:134_000_000_000);check(d.utc(now:134_000_000_000) != nil,"missing SOF retains clock")
    value["estimate_reason"]=4;missing["local_estimate"]=value;d.ingest(missing,now:134_100_000_000)
    check(d.utc(now:134_100_000_000)==nil,"positive counter fault")
    let e=USBContinuousEstimate();e.ingest(sample(16_000_000,base),now:1_000_000_000)
    let original=e.anchorAt
    e.ingest(sample(16_240_000,base+15_000_000,2),now:1_016_000_000,allowMap:false)
    check(e.anchorAt==original && e.record?["map_generation"] as? Int == 1,"missing host bracket cannot re-anchor")
    var changed=sample(16_240_000,base+15_000_000,2)["local_estimate"] as! [String:Any]
    changed["counter_valid"]=false;changed["estimate_present"]=false;changed["estimate_reason"]=2;changed["capture_epoch"]=2
    e.ingest(["local_estimate":changed],now:1_017_000_000,allowMap:false)
    check(e.utc(now:1_017_000_000)==nil && e.discontinuities==1,"positive epoch change fences even without a timestamp")
    let f=USBContinuousEstimate();f.ingest(sample(16_000_000,base),now:1_000_000_000)
    changed["capture_epoch"]=1;changed["estimate_reason"]=4
    f.ingest(["local_estimate":changed],now:1_017_000_000,allowMap:false)
    check(f.utc(now:1_017_000_000)==nil,"counter fault fences without a timestamp")
    var aged=sample(16_000_000,base)["local_estimate"] as! [String:Any]
    aged["map_age_us"]=172000;aged["anchor_tick"]="16000000";aged["map_ppb"]=0
    let g=USBContinuousEstimate();g.ingest(["local_estimate":aged],now:1_000_000_000)
    check(g.inheritedMapAgeMS(now:1_100_000_000)==272 && g.ageMS(now:1_100_000_000)<101,"inherited source age stays distinct from receiver discipline age")
    aged["map_age_us"]=UInt32.max;g.record=aged
    check(g.inheritedMapAgeMS(now:1_100_000_000)==nil,"unknown source age stays unknown")
    return checks
}
