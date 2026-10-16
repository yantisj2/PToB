import Foundation
import Darwin

/// Lifetime statistics survive source changes. These are comparisons with the
/// local wall clock, never calibration inputs to the source shadow clock.
struct LocalClockComparison {
    var count = 0, missing = 0, scopeChanges = 0, wallSteps = 0
    var sum = 0.0, squares = 0.0, maximum = 0.0
    var runningMean = 0.0, centeredSquares = 0.0
    var minimum: Double?, largest: Double?
    var previousWall: MacWallSample?, previousScope: String?
    var gapStart: UInt64?, longestGap: UInt64 = 0
    var first: UInt64?, last: UInt64?
    mutating func observe(wall: MacWallSample, served: SourceInstant?, scope: String) {
        if first == nil { first = wall.tick }; last = wall.tick
        if let previous = previousWall, wall.tick >= previous.tick {
            let change = wall.instant.distance(from:previous.instant)-Double(wall.tick-previous.tick)/1e9
            if abs(change) > 0.005 { wallSteps += 1 }
        }
        previousWall = wall
        if !scope.isEmpty { if let old = previousScope, old != scope { scopeChanges += 1 }; previousScope = scope }
        guard let served = served else {
            missing += 1; if gapStart == nil { gapStart = wall.tick }; return
        }
        if let begin = gapStart { longestGap = max(longestGap,wall.tick-begin); gapStart = nil }
        let error = served.distance(from:wall.instant)
        count += 1; sum += error; squares += error*error; maximum = max(maximum,abs(error))
        let delta = error-runningMean; runningMean += delta/Double(count); centeredSquares += delta*(error-runningMean)
        minimum = min(minimum ?? error,error); largest = max(largest ?? error,error)
    }
    func json(_ now: UInt64) -> [String:Any] {
        let gap = gapStart.map { now >= $0 ? now-$0 : 0 } ?? 0
        return ["valid_status_samples":count,"unavailable_status_samples":missing,
            "mean_ns":count > 0 ? sum/Double(count)*1e9 as Any : NSNull(),
            "rms_ns":count > 0 ? sqrt(squares/Double(count))*1e9 as Any : NSNull(),
            "jitter_stddev_ns":count > 0 ? sqrt(max(0,centeredSquares/Double(count)))*1e9 as Any : NSNull(),
            "max_abs_ns":count > 0 ? maximum*1e9 as Any : NSNull(),
            "minimum_ns":minimum.map {$0*1e9} as Any? ?? NSNull(),"maximum_ns":largest.map {$0*1e9} as Any? ?? NSNull(),
            "scope_changes":scopeChanges,"local_wall_step_observations":wallSteps,
            "longest_observed_unavailable_ns":String(max(longestGap,gap)),
            "observation_span_ns":String((last ?? 0)-(first ?? 0)),
            "population":"1Hz status observations across the entire run, including source changes; gaps are point-sampled, not continuous coverage",
            "definition":"served source shadow minus bracketed local macOS CLOCK_REALTIME; no local wall feedback"]
    }
    var line: String {
        guard count > 0 else { return "vs local macOS: unavailable · gaps \(missing)" }
        return String(format:"Run bias %+.3f ms · RMS %.3f ms · max %.3f ms · samples %d · gaps %d",sum/Double(count)*1e3,sqrt(squares/Double(count))*1e3,maximum*1e3,count,missing)
    }
}

struct NTPCheck {
    let tickNS: UInt64, server: String, address: String, offset: Double, uncertainty: Double
    // Apple's read-only sntp reports server minus local. Keep that sign explicit.
    static func parse(_ text: String, at: UInt64, server: String, exitCode: Int32) -> NTPCheck? {
        guard exitCode == 0, let regex = try? NSRegularExpression(pattern:"(?m)^([+-][0-9]+\\.[0-9]+) \\+/- ([0-9]+\\.[0-9]+) ([^\\s]+) ([^\\s]+)$"),
              let match = regex.firstMatch(in:text,range:NSRange(text.startIndex...,in:text)),
              let o = Range(match.range(at:1),in:text), let u = Range(match.range(at:2),in:text),
              let h = Range(match.range(at:3),in:text), let a = Range(match.range(at:4),in:text),
              String(text[h]) == server, let offset = Double(text[o]), let uncertainty = Double(text[u]),
              offset.isFinite, uncertainty.isFinite, uncertainty >= 0 else { return nil }
        return NTPCheck(tickNS:at,server:server,address:String(text[a]),offset:offset,uncertainty:uncertainty)
    }
    var json: [String:Any] { ["server":server,"address":address,"server_minus_local_ns":offset*1e9,
        "reported_uncertainty_ns":uncertainty*1e9,"tick_ns":String(tickNS),"authenticated":false,
        "supports_100us_check":abs(offset)+uncertainty <= 0.0001,
        "definition":"Apple sntp read-only observation; reported uncertainty retained; remote path asymmetry and server trust uncalibrated"] }
}

/// Bounded background checks. No root privileges, clock set/slew, daemon control
/// or NTP feedback into the source shadow. Failures and stale checks stay visible.
final class LocalNTPMonitor {
    let server: String
    let worker = DispatchQueue(label:"org.ptob.ntp-check",qos:.utility)
    let owner: DispatchQueue
    let record: ([String:Any]) -> Void
    var next: UInt64 = 0, running = false, stopped = false
    var checks = 0, failures = 0
    var latest: NTPCheck?, lastError: String?
    var process: Process?
    private let launchLock = NSLock()
    private var cancelled = false
    init(server: String, owner: DispatchQueue, record: @escaping ([String:Any]) -> Void) { self.server = server; self.owner = owner; self.record = record }
    static func configuredServer() -> String {
        if let text = try? String(contentsOfFile:"/etc/ntp.conf",encoding:.utf8) {
            for line in text.split(separator:"\n") {
                let words = line.split(whereSeparator:{$0 == " " || $0 == "\t"})
                if words.count >= 2 && (words[0] == "server" || words[0] == "pool"), validServer(String(words[1])) { return String(words[1]) }
            }
        }
        return "time.apple.com"
    }
    static func validServer(_ server: String) -> Bool {
        !server.isEmpty && server.count <= 253 && !server.hasPrefix("-") && server.unicodeScalars.allSatisfy { CharacterSet(charactersIn:"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:").contains($0) }
    }
    func poll(_ now: UInt64) {
        guard !stopped, !running, now >= next else { return }
        running = true; next = now+60_000_000_000
        let task = Process(), pipe = Pipe()
        task.executableURL = URL(fileURLWithPath:"/usr/bin/sntp")
        task.arguments = ["-t","3","-n","1",server]
        task.standardOutput = pipe; task.standardError = pipe; process = task
        worker.async {
            var output = Data(), failure: String?, status: Int32 = -1
            do {
                self.launchLock.lock()
                if self.cancelled { self.launchLock.unlock(); return }
                do { try task.run() } catch { self.launchLock.unlock(); throw error }
                self.launchLock.unlock()
                let limit = DispatchSource.makeTimerSource(queue:DispatchQueue.global(qos:.utility))
                limit.schedule(deadline:.now()+5); limit.setEventHandler { if task.isRunning { task.terminate(); DispatchQueue.global().asyncAfter(deadline:.now()+1) { if task.isRunning { kill(task.processIdentifier,SIGKILL) } } } }; limit.resume()
                // sntp -n 1 has bounded diagnostic output; retain a capped copy.
                while true { let data = pipe.fileHandleForReading.readData(ofLength:4096); if data.isEmpty { break }; if output.count < 32768 { output.append(data.prefix(32768-output.count)) } }
                task.waitUntilExit(); limit.cancel(); status = task.terminationStatus
            } catch { failure = String(describing:error) }
            try? pipe.fileHandleForReading.close()
            let resultText = String(data:output,encoding:.utf8) ?? "invalid UTF-8"
            let observed = tick()
            self.owner.async {
                self.running = false; self.process = nil
                guard !self.stopped else { return }
                self.checks += 1
                let parsed = NTPCheck.parse(resultText,at:observed,server:self.server,exitCode:status)
                if let parsed = parsed { self.latest = parsed; self.lastError = nil }
                else { self.failures += 1; self.lastError = failure ?? "sntp failed or response invalid (exit \(status))" }
                self.record(["type":"ntp_check","tick_ns":String(observed),"result":parsed?.json as Any? ?? NSNull(),"exit_code":status,"error":self.lastError as Any? ?? NSNull(),"raw_output":resultText,
                    "os_sync_configuration":"UNVERIFIED: privileged timed state not required or read","method":"/usr/bin/sntp -t 3 -n 1; no set or slew flags"])
            }
        }
    }
    func snapshot(_ now: UInt64) -> [String:Any] {
        let fresh = latest.map { now >= $0.tickNS && now-$0.tickNS < 120_000_000_000 } ?? false
        return ["enabled":true,"server":server,"checks":checks,"failures":failures,"fresh":fresh,
            "last_error":lastError as Any? ?? NSNull(),"latest":latest?.json as Any? ?? NSNull(),
            "os_sync_enabled":"UNVERIFIED","independent_utc_accuracy_qualified":false]
    }
    func line(_ now: UInt64) -> String {
        guard let sample = latest else { return "NTP check: \(lastError ?? "checking \(server)…")" }
        let fresh = now >= sample.tickNS && now-sample.tickNS < 120_000_000_000
        return String(format:"Local − NTP %+.3f ± %.3f ms · %@ · %@",-sample.offset*1e3,sample.uncertainty*1e3,fresh ? "recent" : "STALE",abs(sample.offset)+sample.uncertainty <= 0.0001 ? "100µs consistency only" : "100µs unverified") + (lastError.map {" · latest check failed: \($0)"} ?? "")
    }
    func stop() { stopped = true; launchLock.lock(); cancelled = true; if let task = process, task.isRunning { task.terminate() }; launchLock.unlock() }
}
