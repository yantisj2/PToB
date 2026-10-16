import Foundation
import Darwin

/// Exact port of rc_boundary.c map_value. All epoch arithmetic stays integer;
/// a fractional counter tick is applied only as a bounded local interpolation.
struct USBMapGeometry {
    let record: USBSourceRecord
    let anchorTick: UInt64, anchorNS: UInt64, effective: UInt64, issued: UInt64, expires: UInt64
    let phase: Int32
    init?(_ record: USBSourceRecord) {
        func u(_ key: String) -> UInt64? {(record.raw[key] as? String).flatMap(UInt64.init)}
        guard let at = u("anchor_tick"), let ns = u("anchor_ns"), ns <= UInt64(Int64.max),
              let effective = u("effective_tick"), let issued = u("source_issued_ns"),
              let expires = u("source_expires_ns"), issued < expires, expires <= UInt64(Int64.max),
              let phase = record.raw["map_phase_ns"] as? Int32, abs(Int64(record.ppb)) <= 300000,
              abs(Int64(phase)) <= 300000, effective <= record.lo, record.lo < record.hi else {return nil}
        self.record = record;anchorTick = at;anchorNS = ns;self.effective = effective
        self.issued = issued;self.expires = expires;self.phase = phase

    }
    func project(_ tick: UInt64, fraction: Double = 0) -> Int64? {
        guard tick >= effective, tick >= record.lo, tick < record.hi, tick < record.until,
              tick >= anchorTick, tick-anchorTick <= UInt64.max/125,
              fraction.isFinite, fraction >= 0, fraction < 1 else {return nil}
        let nominal = (tick-anchorTick)*125/2
        let denominator = UInt64(1_000_000_000+Int64(record.ppb))
        let part = nominal%denominator*1_000_000_000/denominator, whole = nominal/denominator
        guard whole <= (UInt64.max-part)/1_000_000_000 else {return nil}
        let elapsed = whole*1_000_000_000+part
        guard elapsed <= UInt64(Int64.max), anchorNS <= UInt64(Int64.max)-elapsed else {return nil}
        let slew = Int64(min(elapsed,1_000_000_000))*Int64(phase)/1_000_000_000
        let base = Int64(anchorNS+elapsed).addingReportingOverflow(slew)
        guard !base.overflow else {return nil}
        let fractionalNS = fraction*62.5*1e9/Double(denominator)*(elapsed < 1_000_000_000 ? 1+Double(phase)/1e9 : 1)
        let result = base.partialValue.addingReportingOverflow(Int64(fractionalNS.rounded()))
        guard !result.overflow, result.partialValue >= 0, UInt64(result.partialValue) >= issued,
              UInt64(result.partialValue) < expires else {return nil}
        return result.partialValue
    }
}

/// A valid captured/current UTC sample is required to construct an installable
/// source map. Flags0 observation provenance uses USBMapGeometry separately.
struct USBImmutableSourceMap {
    let geometry: USBMapGeometry
    var record: USBSourceRecord {geometry.record}
    var anchorTick: UInt64 {geometry.anchorTick};var anchorNS: UInt64 {geometry.anchorNS}
    var effective: UInt64 {geometry.effective};var issued: UInt64 {geometry.issued};var expires: UInt64 {geometry.expires}
    var phase: Int32 {geometry.phase}
    init?(_ record: USBSourceRecord) {
        guard let utc = record.utc, let g = USBMapGeometry(record),
              let value = g.project(record.counter), let offset = (record.raw["utc_offset_ns"] as? String).flatMap(Int64.init),
              !value.addingReportingOverflow(offset).overflow, value+offset == utc.ns else {return nil}
        geometry = g
    }
    func project(_ tick: UInt64, fraction: Double = 0) -> Int64? {geometry.project(tick,fraction:fraction)}
}

/// Proof about the current publication, NEVER a source time sample or map.
struct USBMissingSOFObservation {
    let geometry: USBMapGeometry
    let offset: Int64
    init?(_ r: USBSourceRecord) {
        guard r.reason == 7, r.kind == 1, r.utc == nil,
              (r.raw["utc_ns"] as? String) == "0",
              let frame = usbExactUInt32(r.raw["frame"]),frame < 2048,r.current-r.counter < 16000,
              let p = r.raw["sof_map_metadata"] as? [String:Any],
              Set(p.keys) == Set(["magic","schema","bytes","current_source_ns","flags","reserved"]),
              p["magic"] as? String == "USM1",usbExactUInt32(p["schema"]) == 1,usbExactUInt32(p["bytes"]) == 24,
              usbExactUInt32(p["flags"]) == 1,usbExactUInt32(p["reserved"]) == 0,
              let source = (p["current_source_ns"] as? String).flatMap(Int64.init),
              let g = USBMapGeometry(r),let current = g.project(r.current),current == source,
              r.counter < r.lo || r.counter >= r.hi,
              let offset = (r.raw["utc_offset_ns"] as? String).flatMap(Int64.init),
              !current.addingReportingOverflow(offset).overflow,current+offset > 0,
              current+offset < Int64(UInt32.max)*1_000_000_000 else {return nil}
        // Match the existing firmware's conservative source-expiry clipping.
        // This field can move by rounding between reads; it is not an immutable
        // map parameter and can never refresh the old host authority grant.
        let remaining = (g.expires-UInt64(current))/1001*16
        guard r.until == r.current+min(r.hi-r.current,remaining) else {return nil}
        geometry = g;self.offset = offset
    }
}

/// Future source handoff, deliberately separate from legacy coarse UTS1 UTC.
/// Construction requires an independently verified PPS label, not rounding a
/// source second or retaining a UART-arrival offset. No current wire decoder
/// creates this evidence. A firmware/protocol commissioning step is still needed.
struct VerifiedUTCAnchor {
    enum Association {case verifiedCapturedPPSToUTCSecond}
    let sourceScope: String, associationGeneration: UInt32
    let sourceNS: Int64, utc: SourceInstant, sourceExpiresNS: Int64, uncertaintyNS: UInt32
    let association: Association
    func project(_ source: Int64, scope: String) -> SourceInstant? {
        guard scope == sourceScope, associationGeneration != 0, uncertaintyNS != UInt32.max,
              utc.ns > 0, utc.ns%1_000_000_000 == 0, sourceNS >= 0,
              source >= sourceNS, source < sourceExpiresNS else {return nil}
        let value = utc.ns.addingReportingOverflow(source-sourceNS)
        return value.overflow ? nil : SourceInstant(ns:value.partialValue)
    }
}

/// Fine source output composes the independently measured hardware bridge with
/// the immutable authenticated source map. Coarse UTC never trains either fit.
final class PreciseSourceClock {
    private(set) var map: USBImmutableSourceMap?, anchor: VerifiedUTCAnchor?
    private(set) var accepted = 0, rejected = 0, revocations = 0
    private(set) var reason = "WAITING_FOR_SOURCE_MAP"
    private var deadline: UInt64 = 0
    func ingest(_ r: USBSourceRecord, permissionUntil: UInt64) {
        guard r.utc != nil else {withhold(reason:r.reason == 4 ? "PUBLICATION_BUSY" : "SOURCE_UNAVAILABLE");return}
        guard let next = USBImmutableSourceMap(r) else {rejected += 1;withhold(reason:"SOURCE_MAP_INVALID");return}
        if let previous = map, previous.record.scope != r.scope {anchor = nil;revocations += 1}
        if let previous = map, previous.record.scope == r.scope {
            // A historical SOF can legitimately carry an older publication;
            // retain it as evidence without moving the live clock backwards.
            if next.effective < previous.effective {rejected += 1;return}
            if r.generation < previous.record.generation {rejected += 1;anchor = nil;withhold(reason:"MAP_GENERATION_ROLLBACK");return}
            if previous.record.generation == r.generation &&
                (previous.anchorTick != next.anchorTick || previous.anchorNS != next.anchorNS ||
                 previous.effective != next.effective || previous.phase != next.phase || previous.record.ppb != r.ppb) {
                rejected += 1;anchor = nil;withhold(reason:"IMMUTABLE_MAP_MUTATED");return
            }
        }
        // Same-scope identity alone is insufficient to carry an absolute anchor
        // across a discontinuous renewal. Compare both maps at the new boundary
        // whenever their original permitted intervals overlap.
        if let previous = map, previous.record.scope == r.scope, previous.record.generation != r.generation {
            if let old = previous.project(r.lo), let new = next.project(r.lo), abs(new-old) <= 1000 {} else {anchor = nil}
        }
        map = next;deadline = permissionUntil;accepted += 1;reason = "UTC_ASSOCIATION_REQUIRED"
    }
    /// Validate only against the existing grant. Successful proof neither
    /// installs its geometry nor changes this clock's deadline/map/training.
    func missingSOFRetentionFailure(_ r: USBSourceRecord, scope: String?, originalDeadline: UInt64,
                                    requestBefore: UInt64, now: UInt64) -> String? {
        guard originalDeadline > 0, now < originalDeadline else {return "ORIGINAL_PERMISSION_EXPIRED"}
        guard let old = map,scope == r.scope,old.record.scope == r.scope else {return "SOURCE_SCOPE_CHANGED_OR_MISSING"}
        guard let proof = USBMissingSOFObservation(r) else {return "MALFORMED_CURRENT_PROJECTION_PROVENANCE"}
        let next = proof.geometry
        guard (old.record.raw["utc_offset_ns"] as? String).flatMap(Int64.init) == proof.offset else {return "UTC_TRANSLATION_CHANGED"}
        let remaining = UInt64(min(500_000_000,Double(r.until-r.current)*62.5/1.02))
        guard now >= requestBefore, requestBefore <= UInt64.max-remaining, now < requestBefore+remaining else {return "CURRENT_PERMISSION_EXPIRED_IN_FLIGHT"}
        guard r.generation >= old.record.generation,next.effective >= old.effective else {return "MAP_GENERATION_ROLLBACK"}
        if r.generation == old.record.generation {
            guard next.anchorTick == old.anchorTick,next.anchorNS == old.anchorNS,next.effective == old.effective,
                  next.phase == old.phase,r.ppb == old.record.ppb,r.lo == old.record.lo,r.hi == old.record.hi,
                  next.issued == old.issued,next.expires == old.expires,
                  r.bound == old.record.bound,r.rateBound == old.record.rateBound,r.captureBound == old.record.captureBound else {return "IMMUTABLE_MAP_MUTATED"}
        } else {
            let overlap = max(r.lo,old.record.lo)
            if overlap < min(r.hi,old.record.hi) {
                guard let a = old.project(overlap),let b = next.project(overlap),abs(a-b) <= 1000 else {return "SOURCE_COORDINATE_DISCONTINUITY"}
            }
        }
        return nil
    }
    func withhold(reason: String) {deadline = 0;self.reason = reason}
    func revoke(_ why: String) {map = nil;anchor = nil;deadline = 0;reason = why;revocations += 1}
    func install(_ verified: VerifiedUTCAnchor) -> Bool {
        guard let map = map, verified.sourceScope == map.record.scope,
              verified.sourceExpiresNS <= Int64(map.expires),
              verified.project(verified.sourceNS,scope:map.record.scope) != nil else {return false}
        anchor = verified;return true
    }
    func sourceNS(bridge: USBFrameBridge, absolute: UInt64, now: UInt64) -> Int64? {
        guard now < deadline, let map = map,
              bridge.scope == "\(map.record.silicon):\(map.record.boot):\(map.record.usbEpoch):\(map.record.captureEpoch)",
              let counter = bridge.counter(atMachAbsolute:absolute) else {return nil}
        return map.project(counter.whole,fraction:counter.fraction)
    }
    func utc(bridge: USBFrameBridge, absolute: UInt64, now: UInt64) -> SourceInstant? {
        guard let source = sourceNS(bridge:bridge,absolute:absolute,now:now),let scope = map?.record.scope else {return nil}
        return anchor?.project(source,scope:scope)
    }
    func snapshot(bridge: USBFrameBridge, absolute: UInt64, now: UInt64) -> [String:Any] {
        let source = sourceNS(bridge:bridge,absolute:absolute,now:now)
        let utc = source.flatMap {value in map.flatMap {anchor?.project(value,scope:$0.record.scope)}}
        return ["state":source == nil ? reason : (utc == nil ? "FINE_RELATIVE_READY_UTC_ASSOCIATION_REQUIRED" : "VERIFIED_ASSOCIATION_COMPOSED"),
            "source_monotonic_ns":source.map(String.init) as Any? ?? NSNull(),"utc_ns":utc.map {String($0.ns)} as Any? ?? NSNull(),
            "source_scope":map?.record.scope as Any? ?? NSNull(),"map_generation":map?.record.generation as Any? ?? NSNull(),
            "accepted_maps":accepted,"rejected_maps":rejected,"revocations":revocations,
            "UTC_association":"Legacy UTS1 is coarse; captured PPS/UTC second not verified",
            "coarse_UTC_trains_fine_clock":false,"absolute_UTC_qualified":false,
            "method":"frozen causal SOF/Mach fit -> device counter -> exact immutable source map; original expiry",
            "absolute_timestamp_bias":"UNKNOWN"]
    }
}
