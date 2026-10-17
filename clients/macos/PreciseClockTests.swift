import Foundation

func preciseClockTests() -> Int {
    var checks = 0
    func check(_ ok: Bool,_ why: String) {if !ok {fputs("Precise clock test failed: \(why)\n",stderr);exit(1)};checks += 1}
    let epoch: Int64 = 1_790_000_000_000_000_000, counter: UInt64 = 9_000_000_000
    let mono: UInt64 = 7_000_000_000
    func packet(_ offset: Int64 = 1_789_999_993_000_000_000, ppb: Int32 = 0, phase: Int32 = 0) -> [String:Any] {
        ["silicon":tc04Silicon,"version":1,"timescale":"POSIX_UTC_ESTIMATE","precision_qualified":false,
         "kind":1,"boot":"123456789abcdef0","usb_epoch":1,"capture_epoch":1,"sequence":1,
         "tick":String(counter),"current_tick":String(counter),"valid":true,
         "until_tick":String(counter+16_000_000),"active_lo_tick":String(counter),"active_hi_tick":String(counter+16_000_000),
         "utc_ns":String(Int64(mono)+offset),"utc_offset_ns":String(offset),"root":"0000000000000001",
         "root_boot":"1111111111111111","source_epoch":1,"map_generation":1,
         "bound_ns":UInt32.max,"rate_bound_ppb":UInt32.max,"capture_bound_ns":UInt32.max,"reason":0,
         "map_ppb":ppb,"map_phase_ns":phase,"path_scope":"29f6bcc80dccc04d425072fb6ac3a7dd",
         "anchor_tick":String(counter),"anchor_ns":String(mono),"effective_tick":String(counter),
         "source_issued_ns":String(mono),"source_expires_ns":String(mono+2_000_000_000)]
    }
    func record(_ p: [String:Any]) -> USBSourceRecord {
        let json = try! JSONSerialization.data(withJSONObject:p)
        let decoded = try! JSONSerialization.jsonObject(with:json) as! [String:Any]
        guard let value = USBSourceRecord(decoded) else {
            fputs("Rejected precise-clock fixture: \(decoded)\n",stderr);exit(1)
        }
        return value
    }
    let r = record(packet()), map = USBImmutableSourceMap(r)!
    var dc = packet()
    dc["root"] = tc04Silicon; dc["root_boot"] = dc["boot"]
    dc["path_scope"] = "44432d474e53532d5553422d76310000"
    let dcRecord = record(dc), dcMap = USBImmutableSourceMap(dcRecord)
    check(dcRecord.root == dcRecord.silicon && dcRecord.rootBoot == dcRecord.boot && dcMap != nil,"DC uses local GNSS root with no relay")
    check(dcMap?.project(counter+2) == Int64(mono)+125 && dcMap?.project(counter+16_000_000) == nil,"DC retains integer projection and exact expiry")
    check(map.project(counter) == Int64(mono),"immutable integer origin")
    check(map.project(counter+2) == Int64(mono)+125,"exact two-tick conversion")
    check(map.project(counter+1) == Int64(mono)+62,"firmware integer half-tick truncation")
    check(map.project(counter+16_000_000) == nil && map.project(counter-1) == nil,"original half-open interval cannot extend")
    check(map.project(counter,fraction:0.5) == Int64(mono)+31,"fractional interpolation retains epoch precision")
    var bad = packet();bad["utc_ns"] = String(epoch+1)
    check(USBImmutableSourceMap(record(bad)) == nil,"coarse translation must agree exactly with exported map")
    bad = packet();bad["map_ppb"] = -1_000_000_000
    check(USBImmutableSourceMap(record(bad)) == nil,"invalid rate denominator rejected")
    bad = packet();bad["source_expires_ns"] = String(mono)
    check(USBImmutableSourceMap(record(bad)) == nil,"empty source authority interval rejected")
    // Independent local arithmetic oracle, including both slew signs and rate
    // signs. Delta is under one second, so floating oracle error is <1ns.
    for rate: Int32 in [-250000,0,250000] {
        for phase: Int32 in [-900,0,900] {
            let m = USBImmutableSourceMap(record(packet(ppb:rate,phase:phase)))!
            for dt: UInt64 in [16000,2_000_000,7_999_998] {
                let elapsed = Double(dt)*62.5/(1+Double(rate)/1e9)
                let expected = Int64(mono)+Int64(elapsed.rounded(.down))+Int64((min(elapsed,1e9)*Double(phase)/1e9).rounded(.towardZero))
                check(abs(m.project(counter+dt)!-expected) <= 1,"rate and phase slew match independent oracle")
            }
        }
    }
    // A frozen hardware model with 5ppm counter drift. The source map has its
    // own rate; coarse UTC changes below must never change this relation.
    let bridge = USBFrameBridge()
    bridge.directFullSpeedXHCI = true;bridge.layoutChecks = 8;bridge.anchors = 16
    bridge.lowerIdentity = 0;bridge.upperIdentity = 0
    bridge.firstReference = 1_000_000;bridge.firstMicroframe = 0;bridge.firstCounter = counter
    bridge.lastCounter = counter;bridge.lastElapsed = 0;bridge.scope = "\(r.silicon):\(r.boot):1:1"
    for i in 0..<64 {bridge.counterWindowFit.add(Double(i)*1e6,Double(i)*5)}
    let fine = PreciseSourceClock();fine.ingest(r,permissionUntil:1_500_000_000)
    func absolute(_ delta: UInt64) -> UInt64 {bridge.firstReference+delta*UInt64(clockScale.denom)/UInt64(clockScale.numer)}
    for delta: UInt64 in [31_250_000,125_000_000,250_000_000,499_000_000] {
        let expected = Int64(mono)+Int64((Double(delta)*1.000005).rounded())
        let value = fine.sourceNS(bridge:bridge,absolute:absolute(delta),now:1_000_000_000+delta)!
        check(abs(value-expected) <= 1,"actual composed clock forecasts between updates without training on next sample")
        check(fine.utc(bridge:bridge,absolute:absolute(delta),now:1_000_000_000+delta) == nil,"fine relative performance cannot invent UTC")
    }
    let baseline = fine.sourceNS(bridge:bridge,absolute:absolute(125_000_000),now:1_125_000_000)
    fine.ingest(record(packet(1_789_999_994_234_567_890)),permissionUntil:1_500_000_000)
    check(fine.sourceNS(bridge:bridge,absolute:absolute(125_000_000),now:1_125_000_000) == baseline,"noisy coarse UTC cannot perturb fine output")
    check(fine.sourceNS(bridge:bridge,absolute:absolute(125_000_000),now:1_500_000_000) == nil,"exact host permission deadline withholds output")
    let anchor = VerifiedUTCAnchor(sourceScope:r.scope,associationGeneration:1,sourceNS:Int64(mono),utc:SourceInstant(ns:epoch),sourceExpiresNS:Int64(mono)+1_000_000_000,uncertaintyNS:100,association:.verifiedCapturedPPSToUTCSecond)
    check(fine.install(anchor),"explicit verified PPS/UTC association installs in matching source scope")
    check(fine.utc(bridge:bridge,absolute:absolute(125_000_000),now:1_125_000_000)?.ns == epoch+baseline!-Int64(mono),"UTC composition preserves fine phase using integer epoch subtraction")
    fine.withhold(reason:"PUBLICATION_BUSY")
    check(fine.utc(bridge:bridge,absolute:absolute(125_000_000),now:1_125_000_000) == nil && fine.map != nil,"busy withholds permission and retains independent calibration")
    fine.ingest(r,permissionUntil:1_500_000_000)
    check(fine.utc(bridge:bridge,absolute:absolute(125_000_000),now:1_125_000_000) != nil,"same immutable map recovers without losing its valid association")
    fine.revoke("ROOT_CHANGED")
    check(fine.map == nil && fine.anchor == nil,"revocation destroys absolute association and source permission")
    fine.ingest(r,permissionUntil:1_500_000_000)
    check(fine.utc(bridge:bridge,absolute:absolute(125_000_000),now:1_125_000_000) == nil,"reacquisition cannot inherit old UTC anchor")
    check(anchor.project(Int64(mono)+1_000_000_000,scope:r.scope) == nil && anchor.project(Int64(mono),scope:r.scope+"x") == nil,"anchor original expiry and source scope enforced")
    check(fine.install(anchor),"restore verified fixture anchor before mutation")
    var mutation = packet();mutation["anchor_ns"] = String(mono+1000);mutation["utc_ns"] = String(epoch+1000)
    fine.ingest(record(mutation),permissionUntil:1_500_000_000)
    check(fine.anchor == nil && fine.reason == "IMMUTABLE_MAP_MUTATED" && fine.sourceNS(bridge:bridge,absolute:absolute(125_000_000),now:1_125_000_000) == nil,"same-generation mutation cannot carry clock or absolute anchor")
    let wrong = VerifiedUTCAnchor(sourceScope:r.scope,associationGeneration:1,sourceNS:Int64(mono),utc:SourceInstant(ns:epoch+1),sourceExpiresNS:Int64(mono)+1_000_000_000,uncertaintyNS:100,association:.verifiedCapturedPPSToUTCSecond)
    check(!fine.install(wrong),"coarse fractional timestamp cannot impersonate a verified top-of-second label")
    bridge.directFullSpeedXHCI = false
    check(fine.sourceNS(bridge:bridge,absolute:absolute(125_000_000),now:1_125_000_000) == nil,"topology requalification is mandatory")
    // Cancel an admitted first send before its worker runs. Its canceled owner
    // completion must not erase the irreversible contamination possibility.
    var periodic = packet();periodic["kind"] = 2
    let owner = DispatchQueue(label:"precision-test-owner"), blocker = DispatchSemaphore(value:0)
    let feeder = ChronyRefclock(socket:"/tmp/not-opened.sock",commandSocket:"/tmp/not-opened-command.sock",binary:"/nonexistent",owner:owner,epochIsolated:true,record:{_ in})
    feeder.worker.async {blocker.wait()}
    let now = tick()
    feeder.ingest(record(periodic),before:now-1000,after:now,wallBefore:epoch-1000,wallAfter:epoch,deadline:now+500_000_000)
    check(feeder.mayHaveFed && feeder.busy && feeder.sent == 0,"first admitted work irrevocably marks possible daemon contamination")
    feeder.pausePending();blocker.signal();feeder.wait();owner.sync {}
    check(feeder.mayHaveFed && !feeder.busy && feeder.sent == 0,"canceled first completion cannot hide possibly pending filter data")
    feeder.invalidate()
    check(feeder.mayHaveFed,"source fence cannot use cleared counters to infer empty daemon")
    feeder.stop()
    return checks
}
