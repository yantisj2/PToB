import Foundation
import Darwin
import MachO

/// argv[0] may be just "ptob-cli" when a shell resolves it through PATH.
/// Ask dyld which executable is running, then resolve all symlinks before
/// locating the packaged helper. Never search the caller's working directory.
func usbHelperExecutableURL() throws -> URL {
    var size: UInt32 = 0
    _ = _NSGetExecutablePath(nil,&size)
    guard size > 0, size <= 1_048_576 else {throw NSError(domain:"Cannot resolve the running CLI executable",code:1)}
    var path = [CChar](repeating:0,count:Int(size))
    guard _NSGetExecutablePath(&path,&size) == 0 else {throw NSError(domain:"Cannot resolve the running CLI executable",code:1)}
    return URL(fileURLWithPath:String(cString:path)).resolvingSymlinksInPath().deletingLastPathComponent().appendingPathComponent("usb/reference-probe")
}

/// Raw USB is a separate source of timing evidence. It cannot serve UTC until
/// frame identity has passed its measurement gate. Source maps are independent evidence.
final class USBObservation {
    var task = Process()
    let owner: DispatchQueue
    private let readers = DispatchGroup()
    let reader = DispatchQueue(label:"org.ptob.usb-observer",qos:.utility)
    let record: ([String:Any]) -> Void
    var valid = 0, unavailable = 0, requestsFailed = 0, referencesFailed = 0
    var lastAt: UInt64 = 0, lastEpoch: String?, epochs = 0
    var running = false, error: String?, stopping = false, sourceRecordsEnabled = false
    var helperPath: String?, diagnostic: String?
    var selectedSerial: String?, selectedSilicon: String?, previousSerial: String?, pinnedSerial: String?, requestedSerial: String?
    var candidates: [[String:Any]] = [], identityRejections = 0
    var startedAt: UInt64 = 0, lastSourceAt: UInt64 = 0, rotating = false, identityFailed = false
    var sourceRequestFailures = 0, sourceResponseCount = 0
    var transportFailure: String?, transportFailureSince: UInt64?, transportErrorCode: Int?
    var reconnecting = false, transportReconnects = 0
    var reconnectSerial: String?, reconnectUntil: UInt64 = 0
    var captureAge = DurationStats(), requestRTT = DurationStats()
    var lastSequence: UInt64?, sequenceGaps: UInt64 = 0
    var backlog = 0; let admission = NSLock()
    init(owner: DispatchQueue, record: @escaping ([String:Any]) -> Void) { self.owner = owner; self.record = record }
    func start(serial: String?, duration: Double, sourceRecords: Bool = false, rate: Int = 8) {
        let task = Process();self.task = task
        error = nil;diagnostic = nil;selectedSerial = nil;selectedSilicon = nil;pinnedSerial = serial
        candidates = [];startedAt = tick();lastSourceAt = 0;rotating = false;identityFailed = false
        transportFailure = nil;transportFailureSince = nil;transportErrorCode = nil
        reconnecting = false
        sourceRecordsEnabled = sourceRecords
        let binary: URL
        do {binary = try usbHelperExecutableURL();helperPath = binary.path}
        catch {self.error = "Cannot locate USB helper; rebuild the CLI. Retrying.";diagnostic = String(describing:error);record(["type":"usb_observer_error","tick_ns":String(tick()),"error":self.error!,"diagnostic":diagnostic!]);return}
        let out = Pipe(), err = Pipe(), exited = DispatchSemaphore(value:0)
        task.terminationHandler = {_ in exited.signal()}
        task.executableURL = binary
        // Eight samples/second are diagnostic snapshots. Missed 1ms frames are
        // counted by the device and never presented as continuous capture.
        let seconds = min(7200,max(1,Int(ceil(duration))))
        task.arguments = ["--discover","--vid","0x2fe3","--pid","0x0100",sourceRecords ? "--device-source" : "--device-sof","--samples",String(min(460800,seconds*rate)),"--interval-us",String(1_000_000/rate),"--duration-seconds",String(seconds)]
        requestedSerial = helperSerial(serial,at:tick())
        if let serial = requestedSerial {task.arguments?.append(contentsOf:["--serial",serial])}
        else if let previous = previousSerial {task.arguments?.append(contentsOf:["--after-serial",previous])}
        if sourceRecords {task.arguments?.append(contentsOf:["--phase-paced","--frame-anchor"])}
        task.standardInput = FileHandle.nullDevice; task.standardOutput = out; task.standardError = err
        do { try task.run(); running = true;record(["type":"usb_helper_start","tick_ns":String(tick()),"pid":task.processIdentifier,"arguments":task.arguments!]) }
        catch {
            self.error = FileManager.default.isExecutableFile(atPath:binary.path) ? "USB helper could not start; press d for details. Retrying." : "USB helper missing; rebuild the CLI. Retrying."
            diagnostic = String(describing:error)
            record(["type":"usb_observer_error","tick_ns":String(tick()),"error":self.error!,"diagnostic":diagnostic!,"helper_path":binary.path]);return
        }
        readers.enter()
        reader.async {
            defer {self.readers.leave()}
            var pending = Data(), bad: String?
            while true {
                // Do not wait for an 8192-byte batch: sparse source records can
                // otherwise sit in the pipe until their permission has expired.
                let data = out.fileHandleForReading.availableData; if data.isEmpty { break }
                pending.append(data)
                while let newline = pending.firstIndex(of:10) {
                    let line = pending[..<newline]; pending.removeSubrange(...newline)
                    guard line.count <= 32768, let row = try? JSONSerialization.jsonObject(with:Data(line)) as? [String:Any] else { bad = "Invalid USB helper row"; continue }
                    self.admission.lock()
                    let allowed = self.backlog < 64
                    if allowed { self.backlog += 1 }; self.admission.unlock()
                    if !allowed { bad = "USB observer queue overflow"; continue }
                    let arrived = tick()
                    self.owner.async {
                        self.admission.lock(); self.backlog -= 1; self.admission.unlock()
                        if self.stopping {
                            self.record(["type":"usb_raw","tick_ns":String(arrived),"helper":row,"ignored_after_stop":true])
                            return
                        }
                        self.ingest(row,at:arrived)
                    }
                }
                if pending.count > 32768 { bad = "Oversized USB helper row"; pending.removeAll() }
            }
            let exitComplete = exited.wait(timeout:.now()+3) == .success
            let diagnostic = String(data:err.fileHandleForReading.readDataToEndOfFile().prefix(8192),encoding:.utf8) ?? ""
            try? out.fileHandleForReading.close(); try? err.fileHandleForReading.close()
            let invalid = bad ?? (!pending.isEmpty ? "Truncated USB helper output" : !exitComplete ? "USB helper exit notification incomplete" : nil)
            self.owner.async {
                self.running = false
                self.diagnostic = diagnostic.isEmpty ? (invalid ?? self.diagnostic) : diagnostic
                if !self.stopping {
                    if let invalid = invalid {self.error = invalid+"; retrying."}
                    else if diagnostic.contains("No eligible relay timing interface") {self.error = "No compatible USB relay available; rescanning."}
                    else if self.reconnecting {self.error = "USB timing connection ended; rediscovering the same relay."}
                    else if self.rotating {self.error = self.transportFailure ?? "Source unavailable for 15 seconds; trying the next USB relay."}
                    else if self.error != nil {}
                    else if exitComplete && task.terminationStatus != 0 {self.error = "USB helper exited \(task.terminationStatus); press d for details. Retrying."}
                    else {self.error = "USB capture ended; retrying."}
                }
                self.record(["type":"usb_observer_end","tick_ns":String(tick()),"exit_code":exitComplete ? task.terminationStatus : -1,"exit_notification_complete":exitComplete,"error":self.error as Any? ?? NSNull(),"parser_error":invalid as Any? ?? NSNull(),"stderr":diagnostic,"helper_path":binary.path,"stopped_by_client":self.stopping,"source_rotation":self.rotating])
            }
        }
    }
    func ingest(_ row: [String:Any], at: UInt64) {
        let event = row["event"] as? String
        if event == "discovery" {candidates = row["candidates"] as? [[String:Any]] ?? []}
        var verified = false
        if !identityFailed, event == "manifest", selectedSerial == nil, let serial = row["serial"] as? String,
           let silicon = usbSerialSilicon(serial), row["silicon"] as? String == silicon,
           (requestedSerial ?? pinnedSerial) == nil || (requestedSerial ?? pinnedSerial) == serial {
            selectedSerial = serial;selectedSilicon = silicon;previousSerial = serial;verified = true
            record(["type":"usb_device_selected","tick_ns":String(at),"serial":serial,"silicon":silicon,
                "policy":pinnedSerial == nil ? "AUTOMATIC_STICKY_WITH_SOURCE_GAP_ROTATION" : "EXPLICIT_SERIAL",
                "candidates":candidates,"registry_id":row["registry_id"] as Any? ?? NSNull()])
        } else if !identityFailed, event == "sample", let silicon = selectedSilicon {
            let packets = ["precision_source","device_source","device_sof","frame_anchor","local_estimate"].compactMap {row[$0] as? [String:Any]}
            verified = packets.allSatisfy {$0["silicon"] as? String == silicon}
        }
        if (event == "sample" || event == "manifest") && !verified {
            identityRejections += 1;identityFailed = true;error = "USB identity mismatch; UTC withheld. Rescanning."
            record(["type":"usb_identity_rejection","tick_ns":String(at),"selected_serial":selectedSerial as Any? ?? NSNull()])
            terminateOwnedTask()
        }
        record(["type":"usb_raw","tick_ns":String(at),"helper":row,"identity_verified":verified])
        if event == "transport_end", row["serial"] as? String == selectedSerial,
           row["reason"] as? String == "STALE_INTERFACE" {
            requestTransportReconnect(at,reason:"STALE_INTERFACE")
        }
        guard event == "sample", verified else { return }
        lastAt = at
        observeSourceTransport(row,at:at)
        if let estimate=row["local_estimate"] as? [String:Any],
           estimate["counter_valid"] as? Bool == true, estimate["estimate_present"] as? Bool == true,
           let utc=(estimate["estimate_utc_ns"] as? String).flatMap(Int64.init), utc>0,
           let root=estimate["root"] as? String, root != "0000000000000000" {
            // Retain an acquired clock during quality holdover. A cold peer
            // exposing only its counter must still allow source discovery.
            lastSourceAt=at
        }
        if row["reference_valid"] as? Bool != true { referencesFailed += 1 }
        guard let device = (row["device_sof"] as? [String:Any]) ?? (row["precision_source"] as? [String:Any]) else { requestsFailed += 1; return }
        let precisionFrame = device["kind"] as? Int == 1 && (device["frame"] as? Int ?? 65535) < 2048
        if device["valid"] as? Bool == true || precisionFrame {
            valid += 1
            let epoch = "\(device["silicon"] ?? ""):\(device["boot"] ?? ""):\(device["usb_epoch"] ?? ""):\(device["capture_epoch"] ?? "")"
            if lastEpoch != epoch { epochs += 1; lastEpoch = epoch; lastSequence = nil }
            if row["device_sof"] is [String:Any], let sequence = device["sequence"] as? UInt64 { if let old = lastSequence, sequence > old+1 { sequenceGaps += sequence-old-1 }; lastSequence = sequence }
            if let a = (device["tick"] as? String).flatMap({UInt64($0)}), let b = (device["current_tick"] as? String).flatMap({UInt64($0)}), b >= a, b-a < 16000 { captureAge.add((b-a)*125/2) }
        } else { unavailable += 1 }
        if let a = (row["device_request_continuous_before"] as? String).flatMap({UInt64($0)}), let b = (row["device_request_continuous_after"] as? String).flatMap({UInt64($0)}), b >= a { requestRTT.add((b-a)*UInt64(clockScale.numer)/UInt64(clockScale.denom)) }
    }
    func observeSourceTransport(_ row: [String:Any], at: UInt64) {
        for name in ["precision_source","device_source"] {
            guard let response = row[name] else {continue}
            if response is [String:Any] {
                sourceResponseCount += 1
                if !reconnecting {reconnectSerial = nil;reconnectUntil = 0}
                if let since = transportFailureSince {
                    record(["type":"usb_transport_recovered","tick_ns":String(at),"outage_ns":String(at >= since ? at-since : 0),"request":name])
                    if error == transportFailure {error = nil;diagnostic = nil}
                    transportFailure = nil;transportFailureSince = nil;transportErrorCode = nil
                }
            } else if response is NSNull {
                sourceRequestFailures += 1
                let code = row[name+"_error_code"] as? Int
                let detail = row[name+"_error"] as? String ?? "No timing response or invalid packet"
                let timeout = code == -536870186 || detail.contains("Code=-536870186")
                let message = timeout ? "USB control timeout; timing requests unanswered. Reconnect USB if persistent." : "USB timing request failed; press d for details. Retrying."
                if transportFailure == nil || transportErrorCode != code {
                    record(["type":"usb_transport_error","tick_ns":String(at),"request":name,"error":message,
                            "error_code":code as Any? ?? NSNull(),"error_domain":row[name+"_error_domain"] ?? NSNull(),"diagnostic":detail,
                            "source_permission": "NO_RESPONSE;SOURCE_STATE_UNKNOWN"])
                }
                if transportFailureSince == nil {transportFailureSince = at}
                transportFailure = message;transportErrorCode = code;error = message;diagnostic = detail
            }
        }
    }
    func snapshot(_ now: UInt64) -> [String:Any] { ["running":running,"selected_serial":selectedSerial as Any? ?? NSNull(),"selected_silicon":selectedSilicon as Any? ?? NSNull(),"discovered_devices":candidates,"identity_rejections":identityRejections,"raw_valid":valid,"raw_unavailable":unavailable,"request_failures":requestsFailed,"source_request_failures":sourceRequestFailures,"source_responses":sourceResponseCount,"transport_error":transportFailure as Any? ?? NSNull(),"transport_error_code":transportErrorCode as Any? ?? NSNull(),"transport_reconnects":transportReconnects,"reconnecting":reconnecting,"reference_failures":referencesFailed,"epochs":epochs,"sequence_gaps":sequenceGaps,"capture_age_nominal_ns":captureAge.json,"request_roundtrip_ns":requestRTT.json,"last_observation_age_ns":lastAt > 0 && now >= lastAt ? String(now-lastAt) as Any : NSNull(),"error":error as Any? ?? NSNull(),"helper_path":helperPath as Any? ?? NSNull(),"diagnostic":diagnostic as Any? ?? NSNull(),"frame_identity":"UNRESOLVED","source_map_export":sourceRecordsEnabled ? "CAUSAL_UTS1_V1" : "NOT_REQUESTED","served_utc":false,"precision_qualified":false] }
    var line: String { "USB raw SOF \(valid) · unavailable \(unavailable) · epochs \(epochs) · precision shadow gated: frame identity" + (error.map {" · \($0)"} ?? "") }
    func helperSerial(_ userSerial: String?, at now: UInt64) -> String? {
        userSerial ?? (now < reconnectUntil ? reconnectSerial : nil)
    }
    func requestTransportReconnect(_ now: UInt64, reason: String) {
        guard !stopping, !reconnecting, let serial = selectedSerial else {return}
        reconnecting = true;transportReconnects += 1
        reconnectSerial = serial;reconnectUntil = now+30_000_000_000
        record(["type":"usb_transport_reconnect","tick_ns":String(now),"serial":serial,"reason":reason,
                "error_code":transportErrorCode as Any? ?? NSNull(),"policy":"SAME_SERIAL_REDISCOVERY_WITH_FRESH_BUFFERS",
                "device_reset_requested":false])
        terminateOwnedTask()
    }
    func rotateIfUnavailable(_ now: UInt64) {
        // Pinned clients need transport recovery too. A decoder miss is not a
        // positive device fault, but sustained absent replies still end only
        // our helper and obtain a new service/buffer set after the usual backoff.
        if running, let since = transportFailureSince, now >= since,
           now-since >= 2_000_000_000 {
            requestTransportReconnect(now,reason:"TIMING_RESPONSES_ABSENT_FOR_2_SECONDS")
        }
        guard running, !rotating, !reconnecting, pinnedSerial == nil, let serial = selectedSerial,
              now >= max(startedAt,lastSourceAt), now-max(startedAt,lastSourceAt) >= 15_000_000_000 else {return}
        rotating = true
        record(["type":"usb_source_rotation","tick_ns":String(now),"serial":serial,"reason":transportFailure == nil ? "NO_PERMITTED_UTC_FOR_15_SECONDS" : "USB_TIMING_REQUESTS_UNANSWERED",
                "transport_error":transportFailure as Any? ?? NSNull(),"error_code":transportErrorCode as Any? ?? NSNull()])
        terminateOwnedTask()
    }
    private func terminateOwnedTask() {
        let owned = task
        if owned.isRunning {
            record(["type":"usb_helper_signal","tick_ns":String(tick()),"pid":owned.processIdentifier,"signal":"SIGTERM"])
            _ = kill(owned.processIdentifier,SIGTERM)
            DispatchQueue.global().asyncAfter(deadline:.now()+2) {
                if owned.isRunning {
                    let result = kill(owned.processIdentifier,SIGKILL)
                    self.owner.async {self.record(["type":"usb_helper_signal","tick_ns":String(tick()),"pid":owned.processIdentifier,"signal":"SIGKILL","result":result])}
                }
            }
        }
    }
    func stop() {stopping = true;terminateOwnedTask()}
    func wait() {
        // Foundation's Process exit notifications need the caller's run loop
        // after reconnects too. Keep it alive while the owned reader drains.
        while readers.wait(timeout:.now()) != .success {RunLoop.current.run(until:Date(timeIntervalSinceNow:0.025))}
    }
}
