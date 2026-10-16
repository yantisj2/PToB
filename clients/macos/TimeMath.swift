import Foundation

/// Wire epochs fit in signed 64-bit nanoseconds. Subtract before converting to
/// floating point: Double POSIX seconds currently has about 238 ns spacing.
struct SourceInstant: Equatable, Comparable {
    let ns: Int64
    init(ns: Int64) { self.ns = ns }
    init(seconds: UInt32, fraction: UInt32 = 0) { ns = Int64(seconds)*1_000_000_000 + Int64(fraction) }
    var seconds: Double { Double(ns / 1_000_000_000) + Double(ns % 1_000_000_000)/1e9 }
    func distance(from other: SourceInstant) -> Double { Double(ns-other.ns)/1e9 }
    func adding(_ seconds: Double) -> SourceInstant { SourceInstant(ns:ns + Int64((seconds*1e9).rounded())) }
    static func < (a: SourceInstant, b: SourceInstant) -> Bool { a.ns < b.ns }
}

struct DurationStats {
    var count = 0; var total = 0.0; var maximum: UInt64 = 0
    mutating func add(_ ns: UInt64) { count += 1; total += Double(ns); maximum = max(maximum,ns) }
    var json: [String:Any] { ["n":count,"mean_ns":count == 0 ? 0 : total/Double(count),"max_ns":maximum] }
}

func signedSeconds(_ a: UInt64, _ b: UInt64) -> Double {
    a >= b ? Double(a-b)/1e9 : -Double(b-a)/1e9
}
