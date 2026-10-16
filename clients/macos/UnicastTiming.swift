import Foundation
import Darwin

enum ClockFitPolicy: String {
    case all, delay
}

/// A phase deadline remains on its grid, but a delayed write cannot be followed
/// by a catch-up write inside half a period. Credit and grant stay unchanged.
func ucNextProbe(planned: UInt64, sent: UInt64, period: UInt64) -> (deadline: UInt64, skipped: UInt64) {
    precondition(period > 1 && sent < UInt64.max-period && planned < UInt64.max-period)
    let earliest = sent+period/2
    var next = planned+period
    let skipped = next < earliest ? (earliest-next+period-1)/period : 0
    next += skipped*period
    return (next,skipped)
}

/// Wall time is only a comparison, never an input to shadow discipline. Integer
/// epoch nanoseconds and the continuous-clock read bracket survive replay.
struct MacWallSample {
    let before: UInt64; let after: UInt64; let instant: SourceInstant
    var tick: UInt64 { before+(after-before)/2 }
    var json: [String:Any] {
        ["host_wall_ns":String(instant.ns),"wall_sample_before_ns":String(before),"wall_sample_after_ns":String(after)]
    }
    static func read() -> MacWallSample {
        let before = tickNS()
        let wall = clock_gettime_nsec_np(CLOCK_REALTIME)
        let after = tickNS()
        return MacWallSample(before:before,after:after,instant:SourceInstant(ns:Int64(wall)))
    }
    static func decode(_ row: [String:Any]) throws -> MacWallSample? {
        let names = ["host_wall_ns","wall_sample_before_ns","wall_sample_after_ns"]
        if names.allSatisfy({row[$0] == nil || row[$0] is NSNull}) { return nil }
        guard let wall = (row[names[0]] as? String).flatMap({Int64($0)}), wall > 0,
              let before = (row[names[1]] as? String).flatMap({UInt64($0)}),
              let after = (row[names[2]] as? String).flatMap({UInt64($0)}), after >= before else {
            throw NSError(domain:"Invalid UCS1 wall-clock bracket",code:1)
        }
        return MacWallSample(before:before,after:after,instant:SourceInstant(ns:wall))
    }
}
// Avoid shadowing the global clock function with MacWallSample.tick.
private func tickNS() -> UInt64 { tick() }
