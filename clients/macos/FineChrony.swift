import Foundation
import Darwin

/// An experimental UTC estimate, never a VerifiedUTCAnchor. The published
/// fractional epoch can have unknown bias; it cannot train the hardware fit.
final class FineChronyClock {
    private var interpretation: String?, offsetNS: Int64?, previous: USBImmutableSourceMap?
    private(set) var quarantined = false, accepted = 0, rejected = 0, withheld = 0
    private(set) var reason = "WAITING_FOR_FINE_MAP"

    func observe(_ map: USBImmutableSourceMap) -> Bool {
        let r = map.record, key = "\(r.root):\(r.rootBoot):\(r.sourceEpoch)"
        guard let offset = (r.raw["utc_offset_ns"] as? String).flatMap(Int64.init) else {
            rejected += 1;reason = "UTC_TRANSLATION_MISSING";return false
        }
        if interpretation != key {
            interpretation = key;offsetNS = offset;previous = nil;quarantined = false
        }
        guard !quarantined else {return false}
        guard offsetNS == offset else {
            quarantined = true;rejected += 1;reason = "UTC_OFFSET_CHANGED_WITHIN_SOURCE_EPOCH";return false
        }
        if let old = previous, old.record.scope == r.scope,
           old.record.generation != r.generation,
           let a = old.project(r.lo), let b = map.project(r.lo), abs(a-b) > 1000 {
            quarantined = true;rejected += 1;reason = "SOURCE_COORDINATE_DISCONTINUITY";return false
        }
        previous = map;reason = "UNQUALIFIED_FINE_UTC_ESTIMATE";return true
    }

    func estimate(_ source: Int64, map: USBImmutableSourceMap) -> SourceInstant? {
        guard observe(map), let offset = offsetNS else {return nil}
        let sum = source.addingReportingOverflow(offset)
        guard !sum.overflow, sum.partialValue > 0 else {rejected += 1;reason = "UTC_OVERFLOW";return nil}
        accepted += 1;return SourceInstant(ns:sum.partialValue)
    }

    /// Both wall UTC and the Mach projection refer to the same short enclosing
    /// read bracket. USB request timing supplies no phase or epoch observation.
    func sample(precise: PreciseSourceClock, bridge: USBFrameBridge, deadline: UInt64) -> FineChronySample? {
        let before = tick(), absoluteBefore = mach_absolute_time()
        let wallNS = clock_gettime_nsec_np(CLOCK_REALTIME)
        let absoluteAfter = mach_absolute_time(), after = tick()
        guard after >= before, absoluteAfter >= absoluteBefore, after-before <= 2000,
              wallNS <= UInt64(Int64.max), after < deadline else {
            withheld += 1;if !quarantined {reason = after >= deadline ? "SOURCE_PERMISSION_EXPIRED" : "HOST_WALL_BRACKET_REJECTED"};return nil
        }
        let absolute = absoluteBefore+(absoluteAfter-absoluteBefore)/2
        return project(precise:precise,bridge:bridge,wall:MacWallSample(before:before,after:after,
            instant:SourceInstant(ns:Int64(wallNS))),absolute:absolute,deadline:deadline,now:after)
    }

    func project(precise: PreciseSourceClock, bridge: USBFrameBridge, wall: MacWallSample,
                 absolute: UInt64, deadline: UInt64, now: UInt64) -> FineChronySample? {
        guard wall.after >= wall.before, wall.after-wall.before <= 2000,
              now >= wall.after, now-wall.after <= 100_000_000, now < deadline else {
            withheld += 1;if !quarantined {reason = "WALL_SAMPLE_OR_PERMISSION_INVALID"};return nil
        }
        guard let source = precise.sourceNS(bridge:bridge,absolute:absolute,now:now),
              let map = precise.map else {
            withheld += 1;if !quarantined {reason = "FINE_MAP_OR_FRAME_UNAVAILABLE"};return nil
        }
        guard let utc = estimate(source,map:map) else {return nil}
        return FineChronySample(utc:utc,wall:wall,record:map.record,deadline:deadline,
            sourceNS:source,absolute:absolute)
    }

    var json: [String:Any] {
        ["mode":"EXPERIMENTAL_UNQUALIFIED_FINE_UTC","state":reason,
         "source_interpretation":interpretation as Any? ?? NSNull(),
         "retained_utc_offset_ns":offsetNS.map(String.init) as Any? ?? NSNull(),
         "quarantined":quarantined,"accepted":accepted,"rejected":rejected,"withheld":withheld,
         "absolute_UTC_qualified":false,"initial_UTC_bias":"UNKNOWN",
         "host_PHY_timestamp_bias":"UNKNOWN","coarse_UTC_trains_fine_fit":false]
    }
}

struct FineChronySample {
    let utc: SourceInstant, wall: MacWallSample, record: USBSourceRecord
    let deadline: UInt64, sourceNS: Int64, absolute: UInt64
}

/// Delivered chrony clock minus its fine source estimate, evaluated at the
/// same wall/Mach bracket. This is a relative delivery check, not UTC truth.
struct FineChronyComparison {
    private(set) var count = 0, missing = 0, scopeChanges = 0
    private var mean = 0.0, m2 = 0.0, squares = 0.0, maximum = 0.0
    private var scope: String?
    mutating func observe(reference: SourceInstant?, delivered: SourceInstant?, scope: String?) {
        if let next = scope {if let old = self.scope, old != next {scopeChanges += 1};self.scope = next}
        guard let reference = reference, let delivered = delivered else {missing += 1;return}
        let error = Double(delivered.ns-reference.ns)
        count += 1;let delta = error-mean;mean += delta/Double(count);m2 += delta*(error-mean)
        squares += error*error;maximum = max(maximum,abs(error))
    }
    var json: [String:Any] {
        ["valid_status_samples":count,"unavailable_status_samples":missing,"scope_changes":scopeChanges,
         "mean_ns":count > 0 ? mean as Any : NSNull(),
         "rms_ns":count > 0 ? sqrt(squares/Double(count)) as Any : NSNull(),
         "jitter_stddev_ns":count > 0 ? sqrt(max(0,m2/Double(count))) as Any : NSNull(),
         "max_abs_ns":count > 0 ? maximum as Any : NSNull(),
         "definition":"delivered chrony UTC minus unqualified fine source estimate at the same host wall/Mach instant; all valid status observations including startup",
         "independent_UTC_reference":false,"absolute_UTC_qualified":false]
    }
}
