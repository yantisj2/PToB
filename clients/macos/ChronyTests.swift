import Foundation

func chronyTests() -> Int {
    var count = 0
    func check(_ ok: Bool,_ label: String) {if !ok {fputs("Chrony test failed: \(label)\n",stderr);exit(1)};count += 1}
    let csv = "424C4500,BLE,1,1790000000.0,0.025,0.001,0.002,10.0,0,0.2,0.2,0.05,1.0,Normal"
    let t = ChronyTracking.parse(csv)!
    let waiting = "00000000,,0,0.000000000,0.000000000,0.000000000,0.000000000,0.000,0.000,0.000,1.000000000,1.000000000,0.0,Not synchronised"
    check(ChronyTracking.unsynchronisedReply(waiting) && ChronyTracking.parse(waiting) == nil,"daemon connection without synchronization cannot grant UTC")
    check(!ChronyTracking.unsynchronisedReply(waiting.replacingOccurrences(of:",0.0,Not",with:",nan,Not")),"malformed unsynchronized telemetry rejected")
    check(t.correction == 0.025 && t.stratum == 1 && t.refid == "424C4500","strict tracking CSV")
    let wall = MacWallSample(before:1_000_000_000,after:1_000_000_000,instant:SourceInstant(ns:1_790_000_000_000_000_000))
    let anchor = ChronyAnchor(tracking:t,wall:wall,queryNS:1000)
    check(anchor.utc(wall)?.ns == wall.instant.ns+25_000_000,"positive correction adds when local is slow")
    let neg = ChronyTracking.parse(csv.replacingOccurrences(of:",0.025,",with:",-0.025,"))!
    check(ChronyAnchor(tracking:neg,wall:wall,queryNS:0).utc(wall)?.ns == wall.instant.ns-25_000_000,"negative correction subtracts when local is fast")
    let later = MacWallSample(before:2_000_000_000,after:2_000_000_000,instant:SourceInstant(ns:wall.instant.ns+1_000_000_000))
    check(anchor.utc(later)?.ns == wall.instant.ns+1_024_990_000,"fast frequency reduces projected rate")
    let slewed = MacWallSample(before:later.before,after:later.after,instant:SourceInstant(ns:later.instant.ns+500_000))
    check(anchor.utc(slewed)?.ns == wall.instant.ns+1_025_489_995,"raw-wall slew is included exactly as chrony sys_null evolves correction")
    let slowWall = MacWallSample(before:later.before,after:later.after,instant:SourceInstant(ns:later.instant.ns-500_000))
    check(anchor.utc(slowWall)?.ns == wall.instant.ns+1_024_490_005,"opposite wall slew uses same raw-time coordinate")
    let stepped = MacWallSample(before:later.before,after:later.after,instant:later.instant.adding(0.01))
    check(anchor.utc(stepped) == nil,"macOS wall steps invalidate the virtual clock")
    let stale = MacWallSample(before:4_000_000_000,after:4_000_000_000,instant:wall.instant.adding(3))
    check(anchor.utc(stale) == nil,"stale query cannot project indefinitely")
    let old = ChronyTracking.parse(csv.replacingOccurrences(of:"1790000000.0",with:"1789999990.0"))!
    check(ChronyAnchor(tracking:old,wall:wall,queryNS:0).utc(wall) == nil,"stale reference cannot project despite recent query")
    for bad in [csv.replacingOccurrences(of:",Normal",with:",Not synchronised"),
                csv.replacingOccurrences(of:",0.025,",with:",nan,"),
                csv.replacingOccurrences(of:",0.2,0.05,",with:",-0.2,0.05,"),
                csv.replacingOccurrences(of:"424C4500",with:"7F7F0101"),
                csv.replacingOccurrences(of:",BLE,1,",with:",BLE,0,"),csv+",0",csv+"\n"+csv] {
        check(ChronyTracking.parse(bad) == nil,"reject unsynchronised/nonfinite/local/malformed tracking")
    }
    let bytes = chronySOCKSample(wallNS:1_790_000_000_123_456_789,offset:0.025)
    func value<T>(_ at: Int,_ type: T.Type) -> T {bytes.withUnsafeBytes {$0.loadUnaligned(fromByteOffset:at,as:type)}}
    check(bytes.count == 40 && value(0,Int64.self) == 1_790_000_000 && value(8,Int32.self) == 123456,"native Darwin timeval")
    check(value(12,Int32.self) == 0 && value(16,Double.self) == 0.025,"zero timeval padding and exact offset")
    check(value(24,Int32.self) == 0 && value(28,Int32.self) == 0 && value(32,Int32.self) == 0 && value(36,Int32.self) == 0x534f434b,"SOCK full UTC, normal leap, no PPS fabrication")
    check((try? chronySocketAddress("relative.sock")) == nil,"socket paths must be absolute")
    check((try? chronySocketAddress("/"+String(repeating:"x",count:104))) == nil,"overlong Unix socket path rejected")
    check((try? chronySocketAddress("/tmp/test.sock")) != nil,"valid Unix socket path")
    let monitor = ChronyMonitor(binary:"/nonexistent",socket:"/tmp/test.sock",owner:DispatchQueue(label:"test"),record:{_ in})
    monitor.anchor = anchor
    check(monitor.utc(wall)?.ns == wall.instant.ns+25_000_000,"attached chrony instance supplies separate UTC")
    monitor.invalidate();check(monitor.utc(wall) == nil,"scope invalidation fences previous anchor")
    monitor.anchor = anchor;monitor.stop();check(monitor.utc(wall) == nil,"stop clears user UTC")
    let config = managedChronyConfiguration("/private/tmp/fixture")
    check(config.contains("poll -2 filter 4") && config.contains("maxsamples 64"),"DC and RC use quarter-second chrony updates with the existing filter")
    check(config.contains("port 0\ncmdport 0") && config.contains("maxsamples 64") && config.contains("refid BLE stratum 0") && !config.contains("local stratum"),"managed config has no network ports or fabricated local authority")
    return count
}
