import Foundation
import Darwin

// The SOCK ABI is native, not a network packet. Darwin LP64 timeval has an
// 8-byte seconds field and 4-byte microseconds field with 4 bytes of padding.
// Keep padding zero; chrony's refclock_sock.c expects a 40-byte sock_sample.
func chronySOCKSample(wallNS: Int64, offset: Double) -> Data {
    precondition(MemoryLayout<timeval>.size == 16 && MemoryLayout<Double>.size == 8)
    var bytes = Data(repeating:0,count:40)
    func put<T>(_ value: T, at: Int) { var v = value; withUnsafeBytes(of:&v) {bytes.replaceSubrange(at..<at+$0.count,with:$0)} }
    put(wallNS/1_000_000_000,at:0); put(Int32(wallNS%1_000_000_000/1000),at:8)
    put(offset,at:16); put(Int32(0x534f434b),at:36)
    return bytes
}

func chronySocketAddress(_ path: String) throws -> sockaddr_un {
    var address = sockaddr_un(); address.sun_family = sa_family_t(AF_UNIX)
    let bytes = Array(path.utf8)+[0]
    guard path.hasPrefix("/"), !path.contains("\0"), bytes.count <= MemoryLayout.size(ofValue:address.sun_path) else {
        throw NSError(domain:"Chrony socket requires an absolute path shorter than104 UTF-8 bytes",code:1)
    }
    address.sun_len = UInt8(MemoryLayout<sockaddr_un>.size)
    withUnsafeMutableBytes(of:&address.sun_path) { $0.copyBytes(from:bytes) }
    return address
}

/// No shell and no daemon mutation except the explicit feeder's reset sources.
/// The caller supplies only fixed commands. Kill/reap on timeout and cap output.
func chronyCommand(binary: String, socket: String, command: [String]) throws -> String {
    let task = Process(), pipe = Pipe(), exited = DispatchSemaphore(value:0)
    task.terminationHandler = {_ in exited.signal()}
    task.executableURL = URL(fileURLWithPath:binary)
    task.arguments = ["-n","-c","-h",socket]+command
    task.standardInput = FileHandle.nullDevice; task.standardOutput = pipe; task.standardError = pipe
    var environment = ProcessInfo.processInfo.environment; environment["LC_ALL"] = "C"; task.environment = environment
    try task.run()
    let timeout = DispatchSource.makeTimerSource(queue:DispatchQueue.global(qos:.utility))
    timeout.schedule(deadline:.now()+1.5)
    timeout.setEventHandler { if task.isRunning { kill(task.processIdentifier,SIGKILL) } }; timeout.resume()
    var output = Data()
    while true {
        let chunk = pipe.fileHandleForReading.readData(ofLength:4096)
        if chunk.isEmpty {break}; if output.count < 32768 {output.append(chunk.prefix(32768-output.count))}
    }
    let complete = exited.wait(timeout:.now()+2) == .success
    timeout.cancel(); try? pipe.fileHandleForReading.close()
    guard complete, task.terminationStatus == 0, let text = String(data:output,encoding:.utf8) else {
        throw NSError(domain:"chronyc failed (exit \(complete ? task.terminationStatus : -1)): \(String(data:output,encoding:.utf8) ?? "invalid UTF-8")",code:1)
    }
    return text.trimmingCharacters(in:.whitespacesAndNewlines)
}

struct ChronyTracking {
    let refid: String, name: String, stratum: Int, referenceUTC: Double, correction: Double
    let lastOffset: Double, residualFrequency: Double
    let rms: Double, frequency: Double, skew: Double, delay: Double, dispersion: Double, interval: Double
    let leap: String
    static func unsynchronisedReply(_ text: String) -> Bool {
        let f = text.split(separator:",",omittingEmptySubsequences:false).map(String.init)
        guard !text.contains("\n"), f.count == 14, f[0].count == 8, f[0].allSatisfy({$0.isHexDigit}),
              f[2] == "0", f[13] == "Not synchronised" else {return false}
        return f[3...12].allSatisfy {Double($0).map {$0.isFinite} ?? false}
    }
    static func parse(_ text: String) -> ChronyTracking? {
        let f = text.split(separator:",",omittingEmptySubsequences:false).map(String.init)
        // chronyc -c tracking: refid,name,stratum,reftime,correction,last,rms,
        // frequency,residual,skew,rootdelay,rootdispersion,interval,leap.
        guard !text.contains("\n"), f.count == 14, f[0].count == 8, f[0].allSatisfy({$0.isHexDigit}),
              let stratum = Int(f[2]), (1...15).contains(stratum),
              let ref = Double(f[3]), let correction = Double(f[4]), let last = Double(f[5]),
              let rms = Double(f[6]), let frequency = Double(f[7]), let residual = Double(f[8]),
              let skew = Double(f[9]), let delay = Double(f[10]), let dispersion = Double(f[11]), let interval = Double(f[12]),
              [ref,correction,last,rms,frequency,residual,skew,delay,dispersion,interval].allSatisfy({$0.isFinite}),
              ref > 0, abs(correction) < 86400, abs(frequency) <= 100000, rms >= 0, skew >= 0,
              delay >= 0, dispersion >= 0, interval >= 0,
              ["Normal","Insert second","Delete second"].contains(f[13]),
              !["00000000","7F7F0101"].contains(f[0].uppercased()) else {return nil}
        return ChronyTracking(refid:f[0].uppercased(),name:f[1],stratum:stratum,referenceUTC:ref,correction:correction,
            lastOffset:last,residualFrequency:residual,
            rms:rms,frequency:frequency,skew:skew,delay:delay,dispersion:dispersion,interval:interval,leap:f[13])
    }
    var json: [String:Any] { ["refid":refid,"reference_name":name,"stratum":stratum,"reference_utc_s":referenceUTC,
        "last_offset_s":lastOffset,"residual_frequency_ppm":residualFrequency,
        "ntp_minus_system_s":correction,"rms_offset_s":rms,"frequency_ppm_fast":frequency,"skew_ppm":skew,
        "root_delay_s":delay,"root_dispersion_s":dispersion,"update_interval_s":interval,"leap":leap] }
}

struct ChronyAnchor {
    let tracking: ChronyTracking, wall: MacWallSample, queryNS: UInt64
    func utc(_ now: MacWallSample, maxReferenceAge: Double = 3, freeRunning: Bool = true) -> SourceInstant? {
        guard now.tick >= wall.tick, now.tick-wall.tick < 2_500_000_000,
              abs(now.instant.distance(from:wall.instant)-Double(now.tick-wall.tick)/1e9) <= 0.005 else {return nil}
        // current_correction is NTP minus raw system time. Positive means the
        // system is slow. A positive frequency means fast; sys_null.c subtracts
        // frequency*elapsed from its correction when chronyd runs with -x.
        // sys_null.c evaluates correction against raw system time, including
        // macOS slews. Mach remains the independent freshness/step guard.
        let elapsed = now.instant.distance(from:wall.instant)
        let utc = wall.instant.adding(tracking.correction+elapsed*(freeRunning ? 1-tracking.frequency*1e-6 : 1))
        let referenceAge = utc.seconds-tracking.referenceUTC
        guard referenceAge >= -0.1, referenceAge < maxReferenceAge else {return nil}
        return utc
    }
}

/// An attached instance is observed read-only. The software clock is kept
/// separate from captured USB UTC and from the gated precision Mach shadow.
final class ChronyMonitor {
    let binary: String, socket: String, owner: DispatchQueue, record: ([String:Any])->Void
    let worker = DispatchQueue(label:"org.ptob.chrony.monitor",qos:.utility)
    var anchor: ChronyAnchor?, error: String?, checks = 0, failures = 0, generation = 0
    var lastObservation: ChronyAnchor?
    var busy = false, stopped = false, next: UInt64 = 0
    var connected = false, synchronized = false, waitingChecks = 0
    init(binary: String, socket: String, owner: DispatchQueue, record: @escaping ([String:Any])->Void) {
        self.binary = binary; self.socket = socket; self.owner = owner; self.record = record
    }
    func invalidate() {generation += 1;anchor = nil;next = 0}
    func poll(_ now: UInt64) {
        guard !stopped, !busy, now >= next else {return}; busy = true;next = now+1_000_000_000
        let token = generation
        worker.async {
            let before = MacWallSample.read()
            var text = "", result: ChronyAnchor?, failure: String?, waiting = false
            do {
                text = try chronyCommand(binary:self.binary,socket:self.socket,command:["tracking"])
                let after = MacWallSample.read()
                if ChronyTracking.unsynchronisedReply(text) {waiting = true}
                else {
                guard let tracking = ChronyTracking.parse(text), after.tick >= before.tick,
                      after.tick-before.tick < 250_000_000,
                      abs(after.instant.distance(from:before.instant)-Double(after.tick-before.tick)/1e9) < 0.005 else {
                    throw NSError(domain:"Unsynchronised, malformed or delayed chrony tracking",code:1)
                }
                let middle = MacWallSample(before:before.tick,after:after.tick,
                    instant:SourceInstant(ns:before.instant.ns+(after.instant.ns-before.instant.ns)/2))
                result = ChronyAnchor(tracking:tracking,wall:middle,queryNS:after.tick-before.tick)
                }
            } catch {failure = String(describing:error)}
            let sample = result, diagnostic = failure, raw = text, awaitingSource = waiting
            self.owner.async {
                self.busy = false;guard !self.stopped, token == self.generation else {return};self.checks += 1
                self.anchor = sample;self.error = diagnostic
                self.connected = sample != nil || awaitingSource;self.synchronized = sample != nil
                if awaitingSource {self.waitingChecks += 1}
                else if sample == nil {self.failures += 1}
                if let sample = sample {self.lastObservation = sample}
                self.record(["type":"chrony_tracking","tick_ns":String(tick()),"tracking":sample?.tracking.json as Any? ?? NSNull(),
                    "query_span_ns":sample.map {String($0.queryNS)} as Any? ?? NSNull(),"raw_csv":raw,"error":diagnostic as Any? ?? NSNull(),
                    "connected":self.connected,"synchronized":self.synchronized,"waiting_for_source":awaitingSource])
            }
        }
    }
    func utc(_ wall: MacWallSample, feeder: ChronyRefclock? = nil, source: USBSourceClock? = nil) -> SourceInstant? {
        guard let anchor = anchor else {return nil}
        if let feeder = feeder {
            guard !feeder.busy, feeder.ready, let scope = feeder.scope, source?.latest?.scope == scope,
                  source?.reportedUTC(wall.tick) != nil, anchor.tracking.refid == "424C4500", anchor.tracking.stratum == 1,
                  anchor.wall.before >= feeder.fenceTick,
                  feeder.epochIsolated,
                  let firstUTC = feeder.firstUTC, anchor.tracking.referenceUTC >= firstUTC-1.1 else {return nil}
        }
        return anchor.utc(wall,maxReferenceAge:feeder == nil ? min(180,max(3,anchor.tracking.interval*2.5)) : 3,freeRunning:feeder != nil)
    }
    func snapshot(_ wall: MacWallSample, feeder: ChronyRefclock? = nil, source: USBSourceClock? = nil) -> [String:Any] {
        let served = utc(wall,feeder:feeder,source:source)
        let observation = anchor ?? lastObservation
        let age = observation.flatMap {wall.tick >= $0.wall.tick ? String(wall.tick-$0.wall.tick) : nil}
        let referenceAge = observation.map {wall.instant.seconds+$0.tracking.correction-$0.tracking.referenceUTC}
        return ["enabled":true,"command_socket":socket,"checks":checks,"failures":failures,"fresh":served != nil,
            "connected":connected,"synchronized":synchronized,"waiting_for_source_checks":waitingChecks,
            "leap":connected && !synchronized ? "Not synchronised" : observation?.tracking.leap as Any? ?? NSNull(),
            "user_utc_ns":served.map {String($0.ns)} as Any? ?? NSNull(),"tracking":anchor?.tracking.json as Any? ?? NSNull(),
            "last_tracking":observation?.tracking.json as Any? ?? NSNull(),"query_age_ns":age as Any? ?? NSNull(),
            "reference_age_s":referenceAge as Any? ?? NSNull(),
            "query_span_ns":observation.map {String($0.queryNS)} as Any? ?? NSNull(),
            "error":error as Any? ?? (feeder != nil && feeder?.epochIsolated == false ? "External refclock filter isolation unverified; use managed chrony for source-bound UTC." as Any : NSNull()),
            "clock":"chronyd virtual NTP clock; signed correction evolves in raw wall coordinates; Mach freshness guard",
            "projection_rate":feeder == nil ? "nominal between queries; attached daemon clock-control mode unknown" : "chronyd -x frequency correction",
            "precision_qualified":false,"independent_reference":feeder == nil ? "UNKNOWN" : "NO: this instance consumes the same USB source"]
    }
    func stop() {stopped = true;connected = false;synchronized = false;invalidate()}
    func wait() {worker.sync {}}
}

/// Full UTC samples, pulse=0. This is an experimental coarse UTC refclock;
/// neither hardware SOF fit nor packet arrival is used as a precise UTC anchor.
final class ChronyRefclock {
    let socket: String, commandSocket: String, binary: String, owner: DispatchQueue, record: ([String:Any])->Void
    let worker = DispatchQueue(label:"org.ptob.chrony.refclock",qos:.utility)
    var sent = 0, rejected = 0, failures = 0, resets = 0, busy = false, stopped = false, ready = false
    var scope: String?, firstUTC: Double?, lastAt: UInt64 = 0, fenceTick: UInt64 = 0, error: String?
    var previousWall: MacWallSample?
    private(set) var inputMethod = "COARSE_USB_REQUEST_MIDPOINT"
    // Conservative irreversible latch: a canceled owner callback may follow a
    // successful sendto. Counters/busy alone cannot prove an empty daemon.
    private(set) var mayHaveFed = false
    private let admission = NSLock()
    private var generation = 0
    let epochIsolated: Bool
    init(socket: String, commandSocket: String, binary: String, owner: DispatchQueue, epochIsolated: Bool = false, fineInput: Bool = false, record: @escaping ([String:Any])->Void) {
        self.socket = socket; self.commandSocket = commandSocket;self.binary = binary;self.owner = owner;self.epochIsolated = epochIsolated;self.record = record
        inputMethod = fineInput ? "EXPERIMENTAL_FINE_UTC_ESTIMATE" : "COARSE_USB_REQUEST_MIDPOINT"
    }
    // Cancel this feeder's generation. Complete filter isolation additionally
    // requires the owner to replace its private chronyd, not just reset sources.
    func invalidate() {admission.lock();generation += 1;admission.unlock();ready = false;scope = nil;firstUTC = nil;fenceTick = 0}
    /// Cancel queued sends on a missing/busy publication without destroying
    /// calibration. UTC remains withheld by the independent source gate.
    func pausePending() {admission.lock();generation += 1;admission.unlock()}
    func ingest(_ r: USBSourceRecord, before: UInt64, after: UInt64, wallBefore: Int64, wallAfter: Int64, deadline: UInt64) {
        let now = tick()
        guard !stopped, !busy, r.kind == 2, r.counter == r.current, let utc = r.utc,
              after >= before, after-before <= 100_000_000, now >= after, now < deadline,
              wallBefore > 0, wallAfter >= wallBefore,
              abs(Double(wallAfter-wallBefore)-Double(after-before)) <= 5_000_000 else {rejected += 1;return}
        let wall = MacWallSample(before:before,after:after,instant:SourceInstant(ns:wallBefore+(wallAfter-wallBefore)/2))
        submit(r,utc:utc,wall:wall,deadline:deadline,
            method:"COARSE_USB_REQUEST_MIDPOINT",details:["request_half_width_ns":String((after-before)/2)])
    }
    func ingestFine(_ sample: FineChronySample) {
        let now = tick(), wall = sample.wall
        guard !stopped, !busy, wall.after >= wall.before, wall.after-wall.before <= 2000,
              now >= wall.after, now-wall.after <= 100_000_000, now < sample.deadline else {rejected += 1;return}
        submit(sample.record,utc:sample.utc,wall:wall,deadline:sample.deadline,
            method:"EXPERIMENTAL_FINE_UTC_ESTIMATE",details:[
                "host_wall_bracket_ns":String(wall.after-wall.before),
                "source_monotonic_ns":String(sample.sourceNS),
                "projection_mach_absolute":String(sample.absolute),
                "initial_UTC_bias":"UNKNOWN","host_PHY_timestamp_bias":"UNKNOWN"])
    }
    private func submit(_ r: USBSourceRecord, utc: SourceInstant, wall: MacWallSample,
                        deadline: UInt64, method: String, details: [String:Any]) {
        let now = tick()
        let step = previousWall.map { wall.tick <= $0.tick || abs(wall.instant.distance(from:$0.instant)-Double(wall.tick-$0.tick)/1e9) > 0.005 } ?? false
        let reset = !ready || scope != r.scope || step || now-lastAt > 2_000_000_000
        previousWall = wall;busy = true;mayHaveFed = true;inputMethod = method;if reset {ready = false;firstUTC = nil}
        let offset = utc.distance(from:wall.instant)
        admission.lock();let token = generation;admission.unlock()
        worker.async {
            var failure: String?
            do {
                self.admission.lock();let current = self.generation == token;self.admission.unlock()
                guard current, tick() < deadline else {throw NSError(domain:"Source revoked before queued chrony work",code:1)}
                if reset {let reply = try chronyCommand(binary:self.binary,socket:self.commandSocket,command:["reset","sources"])
                    guard reply.hasPrefix("200") else {throw NSError(domain:"Chrony refused reset sources: \(reply)",code:1)} }
                self.admission.lock();defer {self.admission.unlock()}
                guard self.generation == token, tick() < deadline else {throw NSError(domain:"Source permission expired or scope revoked before chrony send",code:1)}
                var address = try chronySocketAddress(self.socket)
                let fd = Darwin.socket(AF_UNIX,SOCK_DGRAM,0);guard fd >= 0 else {throw NSError(domain:"Chrony socket",code:Int(errno))};defer {close(fd)}
                _ = fcntl(fd,F_SETFL,O_NONBLOCK)
                let bytes = chronySOCKSample(wallNS:wall.instant.ns,offset:offset)
                let written = withUnsafePointer(to:&address) { address in address.withMemoryRebound(to:sockaddr.self,capacity:1) {a in
                    bytes.withUnsafeBytes {sendto(fd,$0.baseAddress,$0.count,0,a,socklen_t(MemoryLayout<sockaddr_un>.size))}
                } }
                guard written == bytes.count else {throw NSError(domain:"Chrony SOCK send failed",code:Int(errno))}
            } catch {failure = String(describing:error)}
            let diagnostic = failure
            self.owner.async {
                self.busy = false;guard !self.stopped, self.generation == token else {return}
                self.error = diagnostic
                if diagnostic == nil {self.sent += 1;self.ready = true;self.scope = r.scope;self.lastAt = now
                    if reset {self.resets += 1;self.firstUTC = utc.seconds;self.fenceTick = tick()}
                } else {self.failures += 1;self.invalidate()}
                var event: [String:Any] = ["type":"chrony_refclock_sample","tick_ns":String(now),"scope":r.scope,"sent":diagnostic == nil,"command_socket":self.commandSocket,
                    "source_sequence":r.sequence,"source_record_kind":r.kind,"source_record_reason":r.reason,
                    "source_record_valid":r.utc != nil,"original_permission_deadline_ns":String(deadline),
                    "reset_sources":reset,"error":diagnostic as Any? ?? NSNull(),"source_minus_system_s":offset,
                    "source_bound_ns":r.bound == UInt32.max ? NSNull() : r.bound as Any,
                    "rate_bound_ppb":r.rateBound == UInt32.max ? NSNull() : r.rateBound as Any,
                    "capture_bound_ns":r.captureBound == UInt32.max ? NSNull() : r.captureBound as Any,
                    "precision_qualified":false,"input_method":method,
                    "method":method == "EXPERIMENTAL_FINE_UTC_ESTIMATE" ? "causal SOF/Mach fit -> immutable source map -> retained unqualified UTC offset; common host wall bracket; original expiry" : "periodic current-counter UTC at request midpoint; full request interval retained separately; unknown source error is not bounded by chrony configuration"]
                event.merge(details) {_,new in new};self.record(event)
            }
        }
    }
    var json: [String:Any] { ["enabled":true,"refid":"BLE","socket":socket,"sent":sent,"rejected":rejected,"failures":failures,
        "resets":resets,"ready":ready,"scope":scope as Any? ?? NSNull(),"error":error as Any? ?? NSNull(),
        "epoch_isolated":epochIsolated,"epoch_policy":epochIsolated ? "owned fresh daemon per authority generation" : "external filter cleanliness unverified; user UTC withheld",
        "last_sample_tick_ns":sent > 0 ? String(lastAt) as Any : NSNull(),
        "source":"BLE-derived UTC via identity-bound USB relay","input_method":inputMethod,"precision_qualified":false] }
    func stop() {stopped = true;invalidate()}
    func wait() {worker.sync {}}
}
