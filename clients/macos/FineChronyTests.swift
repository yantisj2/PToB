import Foundation

func fineChronyTests() -> Int {
    var checks = 0
    func check(_ ok: Bool,_ why: String) {
        if !ok {fputs("Fine chrony test failed: \(why)\n",stderr);exit(1)};checks += 1
    }
    let counter: UInt64 = 9_000_000_000, mono: UInt64 = 7_000_000_000
    let offset: Int64 = 1_789_999_993_000_000_000
    func packet(_ delta: Int64 = 0, epoch: UInt32 = 1) -> [String:Any] {
        ["silicon":tc04Silicon,"version":1,"timescale":"POSIX_UTC_ESTIMATE","precision_qualified":false,
         "kind":1,"boot":"123456789abcdef0","usb_epoch":1,"capture_epoch":1,"sequence":1,
         "tick":String(counter),"current_tick":String(counter),"valid":true,
         "until_tick":String(counter+16_000_000),"active_lo_tick":String(counter),"active_hi_tick":String(counter+16_000_000),
         "utc_ns":String(Int64(mono)+offset+delta),"utc_offset_ns":String(offset+delta),
         "root":"0000000000000001","root_boot":"1111111111111111","source_epoch":epoch,"map_generation":1,
         "bound_ns":UInt32.max,"rate_bound_ppb":UInt32.max,"capture_bound_ns":UInt32.max,"reason":0,
         "map_ppb":0,"map_phase_ns":0,"path_scope":"29f6bcc80dccc04d425072fb6ac3a7dd",
         "anchor_tick":String(counter),"anchor_ns":String(mono),"effective_tick":String(counter),
         "source_issued_ns":String(mono),"source_expires_ns":String(mono+2_000_000_000)]
    }
    func record(_ p: [String:Any]) -> USBSourceRecord {
        USBSourceRecord(try! JSONSerialization.jsonObject(with:JSONSerialization.data(withJSONObject:p)) as! [String:Any])!
    }
    func map(_ p: [String:Any]) -> USBImmutableSourceMap {USBImmutableSourceMap(record(p))!}
    let r = record(packet()), original = map(packet()), translation = FineChronyClock()
    check(translation.estimate(Int64(mono),map:original)?.ns == Int64(mono)+offset,"integer published UTC estimate")
    check(translation.estimate(Int64(mono)+1,map:original)?.ns == Int64(mono)+offset+1,"nanosecond differences survive the full UTC epoch")
    check(translation.json["absolute_UTC_qualified"] as? Bool == false,"fine estimate never claims absolute qualification")
    check(translation.estimate(Int64(mono),map:map(packet(1))) == nil && translation.quarantined,"one-nanosecond same-epoch offset mutation quarantines")
    check(translation.estimate(Int64(mono),map:original) == nil,"return to old offset cannot clear quarantine")
    var reconnect = packet();reconnect["boot"] = "2222222222222222";reconnect["usb_epoch"] = 2
    check(translation.estimate(Int64(mono),map:map(reconnect)) == nil,"USB reconnect cannot erase source interpretation quarantine")
    check(translation.estimate(Int64(mono),map:map(packet(1,epoch:2))) != nil && !translation.quarantined,"new source epoch permits a new unqualified translation")
    let continuity = FineChronyClock();check(continuity.observe(original),"observe initial coherent map")
    var jump = packet();jump["map_generation"] = 2;jump["anchor_ns"] = String(mono+2000);jump["utc_ns"] = String(Int64(mono)+offset+2000)
    check(!continuity.observe(map(jump)) && continuity.quarantined,"overlapping source-coordinate jump cannot silently train chrony")

    let bridge = USBFrameBridge()
    bridge.directFullSpeedXHCI = true;bridge.layoutChecks = 8;bridge.anchors = 16
    bridge.lowerIdentity = 0;bridge.upperIdentity = 0
    bridge.firstReference = 1_000_000;bridge.firstMicroframe = 0;bridge.firstCounter = counter
    bridge.lastCounter = counter;bridge.lastElapsed = 0;bridge.scope = "\(r.silicon):\(r.boot):1:1"
    for i in 0..<64 {bridge.counterWindowFit.add(Double(i)*1e6,Double(i)*5)}
    let precise = PreciseSourceClock();precise.ingest(r,permissionUntil:1_500_000_000)
    let fine = FineChronyClock(), delta: UInt64 = 125_000_000
    let absolute = bridge.firstReference+delta*UInt64(clockScale.denom)/UInt64(clockScale.numer)
    let wall = MacWallSample(before:1_125_000_000,after:1_125_000_200,instant:SourceInstant(ns:1_790_000_000_125_000_000))
    let sample = fine.project(precise:precise,bridge:bridge,wall:wall,absolute:absolute,deadline:1_500_000_000,now:wall.after)!
    let expected = Int64(mono)+Int64((Double(delta)*1.000005).rounded())+offset
    check(abs(sample.utc.ns-expected) <= 1,"fine feed composes the frozen hardware slope and integer UTC translation")
    check(sample.wall.instant == wall.instant && sample.absolute == absolute,"wall and projection share the supplied common instant")
    check(sample.deadline == 1_500_000_000 && sample.record.scope == r.scope,"original deadline and full scope accompany queued feed")
    check(precise.anchor == nil && precise.utc(bridge:bridge,absolute:absolute,now:wall.after) == nil,"unqualified experiment cannot construct VerifiedUTCAnchor or enable verified UTC")
    let wide = MacWallSample(before:wall.before,after:wall.before+2001,instant:wall.instant)
    check(fine.project(precise:precise,bridge:bridge,wall:wide,absolute:absolute,deadline:1_500_000_000,now:wide.after) == nil,"preempted wall bracket is rejected")
    check(fine.project(precise:precise,bridge:bridge,wall:wall,absolute:absolute,deadline:wall.after,now:wall.after) == nil,"exact host deadline cannot feed")
    precise.withhold(reason:"PUBLICATION_BUSY")
    check(fine.project(precise:precise,bridge:bridge,wall:wall,absolute:absolute,deadline:1_500_000_000,now:wall.after) == nil,"busy source withholds fine UTC without granting authority")
    precise.ingest(r,permissionUntil:1_500_000_000);bridge.directFullSpeedXHCI = false
    check(fine.project(precise:precise,bridge:bridge,wall:wall,absolute:absolute,deadline:1_500_000_000,now:wall.after) == nil,"unidentified USB topology cannot feed fine UTC")

    let owner = DispatchQueue(label:"fine-chrony-test-owner"), blocker = DispatchSemaphore(value:0)
    let feeder = ChronyRefclock(socket:"/tmp/not-opened-fine.sock",commandSocket:"/tmp/not-opened-fine-command.sock",binary:"/nonexistent",owner:owner,epochIsolated:true,record:{_ in})
    feeder.worker.async {blocker.wait()}
    let now = tick(), freshWall = MacWallSample(before:now-1000,after:now,instant:wall.instant)
    let queued = FineChronySample(utc:sample.utc,wall:freshWall,record:r,deadline:now+500_000_000,sourceNS:Int64(mono),absolute:absolute)
    feeder.ingestFine(queued)
    check(feeder.mayHaveFed && feeder.busy && feeder.inputMethod == "EXPERIMENTAL_FINE_UTC_ESTIMATE","fine send marks possible filter contamination before worker execution")
    feeder.pausePending();blocker.signal();feeder.wait();owner.sync {}
    check(feeder.sent == 0 && !feeder.busy && feeder.mayHaveFed,"revoked queued fine sample cannot reach daemon or erase contamination evidence")
    feeder.stop()
    var comparison = FineChronyComparison()
    let truth = SourceInstant(ns:1_790_000_000_000_000_000)
    comparison.observe(reference:truth,delivered:SourceInstant(ns:truth.ns+2000),scope:"one")
    comparison.observe(reference:truth,delivered:SourceInstant(ns:truth.ns+2000),scope:"one")
    check(comparison.json["mean_ns"] as? Double == 2000 && comparison.json["rms_ns"] as? Double == 2000,"direct comparison retains constant delivered bias")
    check(comparison.json["jitter_stddev_ns"] as? Double == 0,"quiet fit cannot hide constant delivered error")
    comparison.observe(reference:truth,delivered:nil,scope:"one")
    check(comparison.count == 2 && comparison.missing == 1,"missing delivered UTC is never a zero-error sample")
    comparison.observe(reference:truth,delivered:SourceInstant(ns:truth.ns+1),scope:"two")
    check(comparison.scopeChanges == 1 && comparison.json["absolute_UTC_qualified"] as? Bool == false,"direct relative comparison records scope changes without qualifying UTC")
    return checks
}
