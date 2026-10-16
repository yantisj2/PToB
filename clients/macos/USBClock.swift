import Foundation
import Darwin
import CoreFoundation

let toolVersion = "ptob-mac-cli/0.21-sof-map-gap"
let tc04Silicon = "0000000000000004"
let clockScale: mach_timebase_info_data_t = { var info = mach_timebase_info_data_t(); mach_timebase_info(&info); return info }()
func tick() -> UInt64 { let t = mach_continuous_time(), n = UInt64(clockScale.numer), d = UInt64(clockScale.denom); return t/d*n+t%d*n/d }
func utcReadout(_ seconds: Double?) -> String {
    guard let s = seconds, s.isFinite, s >= 0, s < Double(UInt32.max) else { return "unavailable" }
    let f = DateFormatter(); f.locale = Locale(identifier:"en_US_POSIX"); f.timeZone = TimeZone(secondsFromGMT:0); f.dateFormat = "yyyy-MM-dd HH:mm:ss.SSS 'UTC'"
    return f.string(from:Date(timeIntervalSince1970:s))
}

// USM1 extension integers must not accept JSON booleans or fractional values.
func usbExactUInt32(_ value: Any?) -> UInt32? {
    guard let n = value as? NSNumber,CFGetTypeID(n) != CFBooleanGetTypeID(),
          n.doubleValue >= 0,n.doubleValue <= Double(UInt32.max),
          n.doubleValue.rounded(.towardZero) == n.doubleValue else {return nil}
    return n.uint32Value
}
func usbSerialSilicon(_ serial: String) -> String? {
    guard serial.utf8.count >= 16, serial.utf8.count <= 128,
          serial.utf8.allSatisfy({$0 >= 33 && $0 <= 126}) else {return nil}
    let suffix = String(serial.suffix(16)).lowercased()
    return usbSiliconValid(suffix) ? suffix : nil
}
func usbSiliconValid(_ silicon: String) -> Bool {
    silicon.utf8.count == 16 && silicon.utf8.allSatisfy({($0 >= 48 && $0 <= 57) || ($0 >= 97 && $0 <= 102)}) && silicon != "0000000000000000"
}
func usbDeviceLabel(_ serial: String?) -> String {
    guard let serial = serial else {return "USB relay"}
    if let id = usbSerialSilicon(serial), let alias = displayRegistry[id] { return alias }
    let prefix = String(serial.dropLast(16)).trimmingCharacters(in:CharacterSet(charactersIn:"-_:"))
    return prefix.isEmpty ? String(serial.suffix(16)) : prefix
}

/// A device UTC report is evidence at its captured counter tick. Until frame
/// identity is independently established, it is never a served Mach shadow.
struct USBSourceRecord {
    let silicon: String, kind: Int, boot: String, usbEpoch: UInt32, captureEpoch: UInt32, sequence: UInt32
    let counter: UInt64, current: UInt64, utc: SourceInstant?, root: String, rootBoot: String
    let sourceEpoch: UInt32, generation: UInt32, until: UInt64, lo: UInt64, hi: UInt64
    let bound: UInt32, rateBound: UInt32, captureBound: UInt32, reason: UInt32, ppb: Int32
    let raw: [String:Any]
    let pathScope: String
    init?(_ d: [String:Any]) {
        func u(_ key: String) -> UInt64? { (d[key] as? String).flatMap {UInt64($0)} }
        guard let silicon = d["silicon"] as? String, usbSiliconValid(silicon), d["version"] as? Int == 1,
              d["timescale"] as? String == "POSIX_UTC_ESTIMATE", d["precision_qualified"] as? Bool == false,
              let kind = d["kind"] as? Int, [1,2].contains(kind),
              let boot = d["boot"] as? String, boot.count == 16, boot != "0000000000000000",
              let ue = d["usb_epoch"] as? UInt32, ue != 0, let ce = d["capture_epoch"] as? UInt32, ce != 0,
              let seq = d["sequence"] as? UInt32, seq != 0,
              let counter = u("tick"), let current = u("current_tick"), current >= counter,
              let valid = d["valid"] as? Bool, let until = u("until_tick"), let lo = u("active_lo_tick"), let hi = u("active_hi_tick"),
              let utcNS = u("utc_ns"), utcNS <= UInt64(Int64.max),
              let root = d["root"] as? String, let rootBoot = d["root_boot"] as? String,
              let se = d["source_epoch"] as? UInt32, let generation = d["map_generation"] as? UInt32,
              let bound = d["bound_ns"] as? UInt32, let rb = d["rate_bound_ppb"] as? UInt32,
              let cb = d["capture_bound_ns"] as? UInt32, let reason = d["reason"] as? UInt32,
              let ppb = d["map_ppb"] as? Int32,
              let path = d["path_scope"] as? String, path.count == 32,
              path.allSatisfy({$0.isHexDigit}) else { return nil }
        // New observation metadata can never masquerade as a valid UTC sample.
        if reason == 7 && (valid || kind != 1 || utcNS != 0) {return nil}
        if d["sof_map_metadata"] != nil && reason != 7 {return nil}
        if valid {
            guard root != "0000000000000000", rootBoot != "0000000000000000", se != 0, generation != 0,
                  counter >= lo, counter < hi, current < until, utcNS > 0,
                  utcNS < UInt64(UInt32.max)*1_000_000_000 else { return nil }
        }
        self.silicon = silicon; self.kind = kind; self.boot = boot; usbEpoch = ue; captureEpoch = ce; sequence = seq
        self.counter = counter; self.current = current; utc = valid ? SourceInstant(ns:Int64(utcNS)) : nil
        self.root = root; self.rootBoot = rootBoot; sourceEpoch = se; self.generation = generation
        self.until = until; self.lo = lo; self.hi = hi; self.bound = bound; rateBound = rb; captureBound = cb
        self.reason = reason; self.ppb = ppb; raw = d; pathScope = path
    }
    var scope: String { "\(silicon):\(boot):\(usbEpoch):\(captureEpoch):\(root):\(rootBoot):\(sourceEpoch):\(pathScope)" }
}

final class USBSourceClock {
    var latest: USBSourceRecord?, periodic: USBSourceRecord?, lastValidRecord: USBSourceRecord?
    var accepted = 0, unavailable = 0, rejected = 0, epochs = 0
    var reportPermissionUntil: UInt64 = 0, lastRecordAt: UInt64 = 0
    private var sequence: UInt32 = 0, boot = ""
    // No arrival-time fit or guessed frame offset is permitted here.
    let frameIdentity = "UNRESOLVED"
    func ingest(_ d: [String:Any], requestBefore: UInt64, received: UInt64) {
        guard let r = USBSourceRecord(d), received >= requestBefore else { rejected += 1; return }
        if "\(r.silicon):\(r.boot):\(r.usbEpoch):\(r.captureEpoch)" != boot { boot = "\(r.silicon):\(r.boot):\(r.usbEpoch):\(r.captureEpoch)"; sequence = 0; epochs += 1; latest = nil; periodic = nil;lastValidRecord = nil; reportPermissionUntil = 0 }
        guard r.sequence > sequence else { rejected += 1; return }; sequence = r.sequence
        if let previous = latest, previous.scope != r.scope { epochs += 1; reportPermissionUntil = 0 }
        if r.kind == 2 { periodic = r }
        latest = r; lastRecordAt = received
        guard r.utc != nil, r.until > r.current else { unavailable += 1; reportPermissionUntil = 0; return }
        // A conservative request-start deadline gates display of captured UTC.
        // It does not associate that UTC with Mach or train a source shadow.
        let remaining = UInt64(min(500_000_000,Double(r.until-r.current)*62.5/1.02))
        guard requestBefore <= UInt64.max-remaining, received < requestBefore+remaining else { unavailable += 1; reportPermissionUntil = 0; return }
        reportPermissionUntil = requestBefore+remaining;lastValidRecord = r; accepted += 1
    }
    func reportedUTC(_ now: UInt64) -> SourceInstant? { now < reportPermissionUntil ? latest?.utc : nil }
    func shadowUTC(_ now: UInt64) -> SourceInstant? { nil } // explicit first-milestone gate
    func reset() { latest = nil; periodic = nil; lastValidRecord = nil; sequence = 0; boot = ""; reportPermissionUntil = 0 }
}

/// Periodic current-counter UTC is created inside its USB request. Local wall
/// reads bracket that event. Keep the whole offset interval; no midpoint trains
/// a clock and no fit residual substitutes for this measurement uncertainty.
struct USBWallIntervals {
    var count = 0, rejected = 0
    var lowSum = 0.0, highSum = 0.0, lowerSquares = 0.0, upperSquares = 0.0
    var latestLow: Int64?, latestHigh: Int64?, maximumWidth: Int64 = 0, maximumAbs = 0.0
    var midpointMean = 0.0, midpointSquares = 0.0
    mutating func add(record: USBSourceRecord, wallBefore: Int64, wallAfter: Int64, monotonicSpanNS: UInt64) {
        guard record.kind == 2, record.counter == record.current, let utc = record.utc,
              wallAfter >= wallBefore, wallBefore > 0,
              abs(Double(wallAfter-wallBefore)-Double(monotonicSpanNS)) <= 5_000_000,
              monotonicSpanNS <= 100_000_000 else { rejected += 1; return }
        let lo = utc.ns-wallAfter, hi = utc.ns-wallBefore
        let l = Double(lo), h = Double(hi), minimum = l <= 0 && h >= 0 ? 0 : min(abs(l),abs(h)), maximum = max(abs(l),abs(h))
        count += 1; lowSum += l; highSum += h; lowerSquares += minimum*minimum; upperSquares += maximum*maximum
        latestLow = lo; latestHigh = hi; maximumWidth = max(maximumWidth,hi-lo); maximumAbs = max(maximumAbs,maximum)
        let midpoint = l+(h-l)/2, delta = midpoint-midpointMean
        midpointMean += delta/Double(count); midpointSquares += delta*(midpoint-midpointMean)
    }
    var json: [String:Any] { ["n":count,"rejected":rejected,"latest_low_ns":latestLow.map {String($0)} as Any? ?? NSNull(),"latest_high_ns":latestHigh.map {String($0)} as Any? ?? NSNull(),
        "bias_low_ns":count > 0 ? lowSum/Double(count) as Any : NSNull(),"bias_high_ns":count > 0 ? highSum/Double(count) as Any : NSNull(),
        "rms_lower_ns":count > 0 ? sqrt(lowerSquares/Double(count)) as Any : NSNull(),"rms_upper_ns":count > 0 ? sqrt(upperSquares/Double(count)) as Any : NSNull(),
        "midpoint_jitter_diagnostic_ns":count > 0 ? sqrt(max(0,midpointSquares/Double(count))) as Any : NSNull(),
        "max_abs_upper_ns":count > 0 ? maximumAbs as Any : NSNull(),"max_interval_width_ns":String(maximumWidth),
        "definition":"captured current-counter UTC minus bracketed local wall; whole USB request interval retained; midpoint jitter includes transport uncertainty; not served shadow accuracy",
        "precision_qualified":false] }
}

func usbOnlyClockTests() -> Int {
    var n = 0
    func check(_ ok: Bool,_ label: String) { if !ok { fputs("USB clock test failed: \(label)\n",stderr); exit(1) }; n += 1 }
    var r: [String:Any] = ["silicon":tc04Silicon,"version":1,"timescale":"POSIX_UTC_ESTIMATE","precision_qualified":false,"kind":1,"boot":"123456789abcdef0","usb_epoch":1,"capture_epoch":1,"sequence":1,"tick":"100000","current_tick":"101000","valid":true,"until_tick":"8101000","active_lo_tick":"90000","active_hi_tick":"8101000","utc_ns":"1790000000000000000","root":"0000000000000001","root_boot":"1111111111111111","source_epoch":1,"map_generation":7,"bound_ns":UInt32.max,"rate_bound_ppb":UInt32.max,"capture_bound_ns":UInt32.max,"reason":0,"map_ppb":100]
    func decoded(_ packet: [String:Any]) -> [String:Any] { try! JSONSerialization.jsonObject(with:JSONSerialization.data(withJSONObject:packet)) as! [String:Any] }
    r["path_scope"] = "29f6bcc80dccc04d425072fb6ac3a7dd"
    let c = USBSourceClock(); c.ingest(decoded(r),requestBefore:1_000_000_000,received:1_005_000_000)
    check(c.accepted == 1 && c.reportedUTC(1_006_000_000) != nil,"captured UTC available with original source permission")
    check(c.shadowUTC(1_006_000_000) == nil,"source UTC and smooth hardware frames do not fabricate a Mach shadow")
    check(c.reportedUTC(1_600_000_000) == nil,"captured UTC display expires")
    c.ingest(decoded(r),requestBefore:1_000_000_000,received:1_005_000_000); check(c.rejected == 1,"sequence replay")
    r["silicon"] = "0000000000000002"; check(USBSourceRecord(decoded(r))?.silicon == "0000000000000002","any compatible relay identity is decoded")
    let firstScope = c.latest!.scope
    c.ingest(decoded(r),requestBefore:2_000_000_000,received:2_005_000_000)
    check(c.accepted == 2 && c.latest!.scope != firstScope,"device switch fences sequences even with identical boot tokens")
    r["silicon"] = "0000000000000000";check(USBSourceRecord(decoded(r)) == nil,"zero device identity refused")
    r["silicon"] = "bad";check(USBSourceRecord(decoded(r)) == nil,"malformed device identity refused")
    check(usbSerialSilicon("TC-02-0000000000000002") == "0000000000000002" && usbSerialSilicon("TC-02") == nil,"serial binds full silicon suffix")
    r["silicon"] = tc04Silicon; r["active_lo_tick"] = "100001"; check(USBSourceRecord(decoded(r)) == nil,"later publication cannot map an earlier SOF")
    r["active_lo_tick"] = "90000"; r["kind"] = 2; r["tick"] = "101000"
    var intervals = USBWallIntervals(); let record = USBSourceRecord(decoded(r))!
    intervals.add(record:record,wallBefore:1_789_999_999_999_000_000,wallAfter:1_790_000_000_001_000_000,monotonicSpanNS:2_000_000)
    check(intervals.latestLow == -1_000_000 && intervals.latestHigh == 1_000_000,"offset retains whole request uncertainty")
    check(intervals.json["rms_lower_ns"] as? Double == 0 && intervals.json["rms_upper_ns"] as? Double == 1_000_000,"RMS bounds cannot masquerade as a point error")
    r["path_scope"] = "00000000000000000000000000000001"
    check(USBSourceRecord(decoded(r))!.scope != record.scope,"source path changes fence the scope")
    r["path_scope"] = "bad"; check(USBSourceRecord(decoded(r)) == nil,"malformed source path rejected")
    return n
}
