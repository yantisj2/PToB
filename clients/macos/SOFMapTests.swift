import Foundation

func sofMapTests() -> Int {
    var count = 0
    func check(_ ok: Bool,_ why: String) {if !ok {fputs("SOF map test failed: \(why)\n",stderr);exit(1)};count += 1}
    let source: UInt64 = 1_000_000_000, offset: Int64 = 1_789_999_000_000_000_000
    func packet(missing: Bool = false,sequence: UInt32 = 1) -> [String:Any] {
        var p: [String:Any] = ["silicon":tc04Silicon,"version":1,"timescale":"POSIX_UTC_ESTIMATE","precision_qualified":false,
            "kind":missing ? 1 : 2,"frame":3,"boot":"123456789abcdef0","usb_epoch":1,"capture_epoch":1,"sequence":sequence,
            "tick":missing ? "61000" : "63000","current_tick":"63000","valid":!missing,
            "until_tick":"16063000","active_lo_tick":"62000","active_hi_tick":"16063000",
            "utc_ns":missing ? "0" : String(Int64(source+1_437_500)+offset),"utc_offset_ns":String(offset),
            "root":"0000000000000001","root_boot":"1111111111111111","source_epoch":1,"map_generation":1,
            "bound_ns":UInt32.max,"rate_bound_ppb":UInt32.max,"capture_bound_ns":UInt32.max,"reason":missing ? 7 : 0,
            "map_ppb":0,"map_phase_ns":0,"path_scope":String(repeating:"ab",count:16),
            "anchor_tick":"40000","anchor_ns":String(source),"effective_tick":"60000",
            "source_issued_ns":String(source),"source_expires_ns":String(source+2_000_000_000)]
        if missing {p["sof_map_metadata"] = ["magic":"USM1","schema":1,"bytes":24,"flags":1,"reserved":0,"current_source_ns":String(source+1_437_500)] as [String:Any]}
        return p
    }
    func normalise(_ p: [String:Any]) -> [String:Any] {try! JSONSerialization.jsonObject(with:JSONSerialization.data(withJSONObject:p)) as! [String:Any]}
    func record(_ p: [String:Any]) -> USBSourceRecord? {USBSourceRecord(normalise(p))}
    let valid = record(packet())!, missing = record(packet(missing:true,sequence:2))!, precise = PreciseSourceClock()
    precise.ingest(valid,permissionUntil:1_500_000_000)
    func failure(_ p: [String:Any],now: UInt64 = 1_100_000_000) -> String? {
        guard let r = record(p) else {return "RECORD_REJECTED"}
        return precise.missingSOFRetentionFailure(r,scope:valid.scope,originalDeadline:1_500_000_000,requestBefore:1_000_000_000,now:now)
    }
    check(USBMissingSOFObservation(missing) != nil && USBImmutableSourceMap(missing) == nil,"proof is typed separately and cannot become a source map")
    check(failure(packet(missing:true,sequence:2)) == nil,"coherent missing historical SOF keeps unexpired existing training")
    for now in [UInt64(1_500_000_000),1_500_000_001] {check(failure(packet(missing:true),now:now) != nil,"exact original deadline never receives a grace period")}
    for (key,value) in [("magic","BAD1" as Any),("schema",2),("bytes",23),("flags",0),("reserved",1),("current_source_ns","1001437501")] {
        var p = packet(missing:true),m = p["sof_map_metadata"] as! [String:Any];m[key] = value;p["sof_map_metadata"] = m
        check(failure(p) != nil,"reject unsupported metadata \(key)")
    }
    for value: Any in [true,1.5,-1,UInt64.max] {
        var p = packet(missing:true),m = p["sof_map_metadata"] as! [String:Any];m["flags"] = value;p["sof_map_metadata"] = m
        check(failure(p) != nil,"extension integer must have exact unsigned integer type/range")
    }
    var clippedValid = packet();clippedValid["source_expires_ns"] = String(source+10_000_000)
    func clippedUntil(_ current: UInt64) -> UInt64 {current+min(16_063_000-current,(10_000_000-(current-40_000)*125/2)/1001*16)}
    clippedValid["until_tick"] = String(clippedUntil(63000))
    let clippedClock = PreciseSourceClock();clippedClock.ingest(record(clippedValid)!,permissionUntil:1_008_000_000)
    var clippedMissing = packet(missing:true);clippedMissing["source_expires_ns"] = String(source+10_000_000)
    clippedMissing["current_tick"] = "63001";clippedMissing["until_tick"] = String(clippedUntil(63001))
    var cm = clippedMissing["sof_map_metadata"] as! [String:Any];cm["current_source_ns"] = String(source+1_437_562);clippedMissing["sof_map_metadata"] = cm
    check(clippedUntil(63000) != clippedUntil(63001),"real source-expiry clipping changes rounded wire-until between current ticks")
    check(clippedClock.missingSOFRetentionFailure(record(clippedMissing)!,scope:valid.scope,originalDeadline:1_008_000_000,requestBefore:1_000_000_000,now:1_001_000_000) == nil,"same-generation deterministic expiry clipping preserves training without renewing deadline")
    var huge = packet();huge["until_tick"] = String(UInt64.max)
    let hugeClock = USBSourceClock();hugeClock.ingest(normalise(huge),requestBefore:1_000_000_000,received:1_000_000_100)
    check(hugeClock.reportPermissionUntil == 1_500_000_000,"legacy deadline cap precedes conversion for huge untrusted until without trapping")
    for (key,value) in [("boot","2222222222222222" as Any),("root","2222222222222222"),("root_boot","2222222222222222"),("usb_epoch",2),("capture_epoch",2),("source_epoch",2),("path_scope",String(repeating:"cd",count:16)),("utc_offset_ns",String(offset+1)),("valid",true),("kind",2),("utc_ns","1"),("current_tick","16063000"),("tick","63000"),("source_expires_ns","1001437500"),("map_generation",0),("anchor_ns",String(source+1)),("bound_ns",1),("until_tick",String(UInt64.max)),("frame",65535)] {
        var p = packet(missing:true);p[key] = value
        check(failure(p) != nil,"reject changed or malformed proof \(key)")
    }
    var renewal = packet(missing:true);renewal["map_generation"] = 2
    check(failure(renewal) == nil,"coherent overlapping next generation keeps calibration without installing metadata")
    var jump = renewal;jump["anchor_ns"] = String(source+2000)
    var m = jump["sof_map_metadata"] as! [String:Any];m["current_source_ns"] = String(source+1_439_500);jump["sof_map_metadata"] = m
    check(failure(jump) == "SOURCE_COORDINATE_DISCONTINUITY","overlap coordinate jump fences")
    check(precise.map?.record.sequence == valid.sequence && precise.accepted == 1,"proof validation cannot install its map or train calibration")

    func instrument() -> USBInstrument {
        let i = try! USBInstrument(USBOptions(arguments:["--no-chrony","--no-interactive"]))
        return i
    }
    func deliver(_ i: USBInstrument,_ p: [String:Any],key: String = "device_source") {
        let now = tick(),absolute = now/UInt64(clockScale.numer)*UInt64(clockScale.denom)+now%UInt64(clockScale.numer)*UInt64(clockScale.denom)/UInt64(clockScale.numer)
        i.accept(["type":"usb_raw","identity_verified":true,"helper":["event":"sample",key:normalise(p),key+"_continuous_before":String(absolute)]])
    }
    let live = instrument();deliver(live,packet());let deadline = live.authorityDeadline, mapSequence = live.precise.map!.record.sequence
    deliver(live,packet(missing:true,sequence:2),key:"precision_source")
    check(live.classifiedSOFPauses == 1 && live.refusedSOFPauses == 0 && !live.authorityRevoked,"actual owner records classified pause without authority recycle")
    check(live.clock.reportPermissionUntil == 0 && live.clock.reportedUTC(tick()) == nil && live.authorityDeadline == deadline,"classified pause withholds output and never refreshes original grant")
    check(live.precise.map?.record.sequence == mapSequence,"actual owner never installs flags0 map")
    deliver(live,packet(missing:true,sequence:3),key:"precision_source")
    check(live.authorityDeadline == deadline && live.classifiedSOFPauses == 2,"repeated flags0 cannot renew deadline")
    deliver(live,packet(sequence:4));check(!live.authorityRevoked && live.clock.reportedUTC(tick()) != nil,"next genuine valid current record recovers normally")
    for field in ["metadata","sequence","offset","boot","expired","valid","unknown"] {
        let i = instrument();deliver(i,packet());var bad = packet(missing:true,sequence:2)
        switch field {
        case "metadata":bad["sof_map_metadata"] = ["magic":"BAD1"]
        case "sequence":bad["sequence"] = 1
        case "offset":bad["utc_offset_ns"] = String(offset+1)
        case "boot":bad["boot"] = "2222222222222222"
        case "expired":i.authorityDeadline = tick()
        case "valid":bad["valid"] = true
        default:bad["reason"] = 1;bad.removeValue(forKey:"sof_map_metadata")
        }
        deliver(i,bad,key:"precision_source")
        check(i.authorityRevoked && i.precise.map == nil && i.clock.reportPermissionUntil == 0,"actual live owner immediately fences \(field)")
    }
    let wire = instrument();deliver(wire,packet());wire.accept(["type":"usb_raw","identity_verified":true,"helper":["event":"sample","precision_source":NSNull(),"precision_source_invalid_reason7":true]])
    check(wire.authorityRevoked && wire.clock.reportPermissionUntil == 0 && wire.refusedSOFPauses == 1,"malformed reason7 attributed despite decoder returning nil")
    let owner = DispatchQueue(label:"sof-map-queue-test"),blocked = DispatchSemaphore(value:0)
    let feeder = ChronyRefclock(socket:"/tmp/not-opened-sof-map.sock",commandSocket:"/tmp/not-opened-sof-map-command.sock",binary:"/nonexistent",owner:owner,epochIsolated:true,record:{_ in})
    let queued = instrument();deliver(queued,packet());queued.feeder = feeder
    feeder.worker.async {blocked.wait()}
    let now = tick(),wall = MacWallSample(before:now-1000,after:now,instant:SourceInstant(ns:Int64(source)+offset))
    feeder.ingestFine(FineChronySample(utc:wall.instant,wall:wall,record:valid,deadline:now+400_000_000,sourceNS:Int64(source),absolute:0))
    check(feeder.busy && feeder.mayHaveFed,"queued feed is admitted before pause")
    deliver(queued,packet(missing:true,sequence:2),key:"precision_source");blocked.signal();feeder.wait();owner.sync {}
    check(feeder.sent == 0 && !feeder.busy && queued.classifiedSOFPauses == 1,"classified pause cancels already queued sample through existing admission generation")
    feeder.stop()
    return count
}
