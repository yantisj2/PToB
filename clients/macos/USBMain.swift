import Foundation
import Darwin

struct USBOptions {
    var duration = 600.0, rate = 32
    var continuous = false, chronyOnly = false
    var chronySocket: String?, chronyCommandSocket: String?, chronyc: String?
    var managedChrony = true, chronyd: String?, deviceSerial: String?
    var experimentalFineChrony = true
    var initialPage = InstrumentPage.main
    var interactive = isatty(STDIN_FILENO) == 1 && isatty(STDOUT_FILENO) == 1
    var checkNTP = true, ntpServer: String?, json: String?, report: String?, reportOut: String?, precisionReplay: String?, test = false, preview: String?, columns = 80, rows = 24
    init(arguments: [String] = Array(CommandLine.arguments.dropFirst())) throws {
        var args = arguments
        var requestedFineChrony: Bool?
        while !args.isEmpty {
            let key = args.removeFirst()
            if key == "--help" { print("PToB USB / chrony clock instrument 0.21\nUSB discovers compatible relay clocks on any port; direct Mac BLE is disabled and not linked.\nUsage: ptob [--duration 600|--continuous] [--device SERIAL] [--no-chrony|--coarse-chrony] [--rate 16|32|64] [--page main|chrony|details|help] [--jsonl NEW_PATH] [--interactive|--no-interactive] [--ntp-server HOST] [--no-ntp]\n       ptob --chrony-command-socket PATH --chronyc ABSOLUTE_BINARY [--chrony-only]\n       ptob --chrony-sock PATH --chrony-command-socket PATH --chronyc ABSOLUTE_BINARY\n       ptob --report CAPTURE.jsonl [--report-out NEW_DIRECTORY]\n       ptob --precision-replay CAPTURE.jsonl\n       ptob --self-test\nAutomatic USB selection stays on a healthy source and tries the next identity after 15s without permitted UTC. --device pins an exact discovered USB serial.\nNormal managed launch feeds an unqualified fine UTC estimate from the causal hardware map and retained published offset. --coarse-chrony selects the USB request-midpoint baseline for diagnostics; --experimental-fine-chrony remains a compatibility alias for fine mode. Initial UTC and host/PHY bias remain unknown; verified absolute UTC stays gated. Normal launch starts a private chronyd -x automatically; --no-chrony keeps USB-only mode. Chrony supplies a separate experimental user UTC clock, without setting macOS time. --chrony-sock feeds coarse USB UTC to an external instance; its filter isolation is unverified, so source-bound user UTC is withheld. Managed chrony recreates its private daemon on real authority changes. --chrony-only observes a local instance without USB access. Captured UTC, precision shadow and chrony UTC remain separate. No UTC accuracy qualification. 1 main, c chrony statistics, d details, h help, r reacquire, q finish."); exit(0) }
            if key == "--self-test" { test = true; continue }
            if key == "--interactive" { interactive = true; continue }
            if key == "--no-interactive" { interactive = false; continue }
            if key == "--check-ntp" { checkNTP = true; continue }
            if key == "--no-ntp" { checkNTP = false; continue }
            if key == "--continuous" {continuous = true;continue}
            if key == "--chrony-only" {chronyOnly = true;continue}
            if key == "--no-chrony" {managedChrony = false;continue}
            if key == "--experimental-fine-chrony" {requestedFineChrony = true;continue}
            if key == "--coarse-chrony" {requestedFineChrony = false;continue}
            guard !args.isEmpty else { throw NSError(domain:"Missing value for \(key)",code:1) }; let value = args.removeFirst()
            switch key {
            case "--device": guard usbSerialSilicon(value) != nil else {throw NSError(domain:"--device requires a full USB serial ending in its 16-digit silicon identity",code:1)};deviceSerial = value
            case "--duration": guard let n = Double(value), n.isFinite, n > 0, n <= 1800 else { throw NSError(domain:"Duration must be 0..1800s",code:1) }; duration = n
            case "--rate": guard let n = Int(value), [16,32,64].contains(n) else { throw NSError(domain:"USB precision rate must be16,32 or64Hz",code:1) }; rate = n
            case "--jsonl": json = value
            case "--ntp-server": guard LocalNTPMonitor.validServer(value) else { throw NSError(domain:"Invalid NTP server",code:1) }; ntpServer = value; checkNTP = true
            case "--instrument-preview": preview = value
            case "--columns": guard let n = Int(value), n >= 20, n <= 240 else {throw NSError(domain:"Invalid columns",code:1)}; columns = n
            case "--rows": guard let n = Int(value), n >= 10, n <= 100 else {throw NSError(domain:"Invalid rows",code:1)}; rows = n
            case "--report": report = value
            case "--precision-replay": precisionReplay = value
            case "--report-out": reportOut = value
            case "--page": guard let page = InstrumentPage(rawValue:value) else {throw NSError(domain:"Page must be main, chrony, details or help",code:1)};initialPage = page
            case "--chrony-sock": _ = try chronySocketAddress(value);chronySocket = value
            case "--chrony-command-socket": _ = try chronySocketAddress(value);chronyCommandSocket = value
            case "--chronyc": guard value.hasPrefix("/"),FileManager.default.isExecutableFile(atPath:value) else {throw NSError(domain:"--chronyc requires an absolute executable path",code:1)};chronyc = value
            case "--chronyd": guard value.hasPrefix("/"),FileManager.default.isExecutableFile(atPath:value) else {throw NSError(domain:"--chronyd requires an absolute executable path",code:1)};chronyd = value
            default: throw NSError(domain:"Unsupported USB-only option \(key); there is no direct BLE transport",code:1)
            }
        }
        guard chronyCommandSocket == nil || chronyc != nil,
              chronyc == nil || chronyCommandSocket != nil || chronyd != nil,
              chronySocket == nil || chronyCommandSocket != nil,
              chronyd == nil || (chronyc != nil && chronyCommandSocket == nil && managedChrony),
              (managedChrony || (chronyCommandSocket == nil && chronySocket == nil && chronyc == nil && !chronyOnly)),
              !chronyOnly || (chronyCommandSocket != nil && chronySocket == nil) else {
            throw NSError(domain:"Chrony requires --chronyc and --chrony-command-socket; --chrony-only cannot feed USB",code:1)
        }
        if chronyCommandSocket != nil {managedChrony = false}
        let supportsFineChrony = managedChrony && !chronyOnly
        guard requestedFineChrony != true || supportsFineChrony else {
            throw NSError(domain:"Experimental fine chrony requires the managed USB session",code:1)
        }
        experimentalFineChrony = supportsFineChrony && (requestedFineChrony ?? true)
        interactive = interactive && isatty(STDIN_FILENO) == 1 && isatty(STDOUT_FILENO) == 1
    }
}

final class USBInstrument {
    let options: USBOptions, owner = DispatchQueue(label:"org.ptob.usb-clock",qos:.userInitiated)
    let evidence = ObserverQueue("org.ptob.usb-clock.evidence",capacity:4096), display = ObserverQueue("org.ptob.usb-clock.display",capacity:2)
    let clock = USBSourceClock(), bridge = USBFrameBridge(), precise = PreciseSourceClock(), done = DispatchSemaphore(value:0)
    let fineChrony = FineChronyClock()
    var nextFineFeed: UInt64 = 0
    var fineComparison = FineChronyComparison()
    var usb: USBObservation?, ntp: LocalNTPMonitor?, log: FileHandle?
    var chrony: ChronyMonitor?, feeder: ChronyRefclock?, chronyRun = LocalClockComparison(), nextUSB: UInt64 = 0
    var managed: ManagedChronySession?, managedConnected = false
    var classifiedSOFPauses = 0, refusedSOFPauses = 0
    var authorityScope: String?, authorityDeadline: UInt64 = 0, authorityRevoked = false
    var timer: DispatchSourceTimer?, signals: [DispatchSourceSignal] = [], activity: NSObjectProtocol?
    var savedTerm: termios?, savedFlags: Int32 = 0, stopped = false
    var page = InstrumentPage.main
    var start = tick(), nextStatus: UInt64 = 0, nextDraw: UInt64 = 0
    var run = LocalClockComparison(), sourceValid = 0, sourceMissing = 0
    var intervals = USBWallIntervals(), displayHealth = DisplayHealth()
    var readoutHold = MainReadoutHold()
    var snapshot: [String:Any] = [:], errors: [String] = []
    init(_ options: USBOptions) throws {
        self.options = options
        page = options.initialPage
        if let path = options.json { let fd = open(path,O_WRONLY|O_CREAT|O_EXCL,0o600); guard fd >= 0 else { throw NSError(domain:"Create a fresh capture path; parent must exist",code:Int(errno)) }; log = FileHandle(fileDescriptor:fd,closeOnDealloc:true) }
        if let socket = options.chronyCommandSocket, let binary = options.chronyc {
            chrony = ChronyMonitor(binary:binary,socket:socket,owner:owner,record:{self.record($0)})
            if let input = options.chronySocket {feeder = ChronyRefclock(socket:input,commandSocket:socket,binary:binary,owner:owner,record:{self.record($0)})}
        }
        if options.managedChrony {managed = ManagedChronySession(daemon:options.chronyd,client:options.chronyc,record:{self.record($0)})}
    }
    func chronySnapshot(_ wall: MacWallSample) -> [String:Any] {
        var value = chrony?.snapshot(wall,feeder:feeder,source:clock) ?? ["enabled":managed != nil,"fresh":false,"connected":false]
        value["selected_usb_serial"] = usb?.selectedSerial as Any? ?? NSNull()
        if let service = managed {
            value["service"] = service.snapshot
            if let error = service.error {value["error"] = error;value["fresh"] = false;value["user_utc_ns"] = NSNull()}
        }
        return value
    }
    func pollManagedChrony(_ now: UInt64) {
        guard let service = managed else {return}
        service.poll(now)
        if managedConnected && service.state != .running {
            feeder?.stop();chrony?.stop();managedConnected = false
        }
        if service.state == .idle || service.state == .failed {_ = service.start(now)}
        if service.state == .running && !managedConnected, let command = service.commandSocket,
           let refclock = service.refclockSocket, let binary = service.clientPath {
            chrony = ChronyMonitor(binary:binary,socket:command,owner:owner,record:{self.record($0)})
            feeder = ChronyRefclock(socket:refclock,commandSocket:command,binary:binary,owner:owner,epochIsolated:true,fineInput:options.experimentalFineChrony,record:{self.record($0)})
            managedConnected = true
        }
    }
    func fenceSource(_ reason: String) {
        guard !authorityRevoked else {return}
        precise.revoke(reason)
        authorityRevoked = true
        recycleChrony(reason)
    }
    func recycleChrony(_ reason: String) {
        let contaminated = feeder?.mayHaveFed == true
        feeder?.invalidate();chrony?.invalidate()
        record(["type":"source_authority_fence","tick_ns":String(tick()),"reason":reason,
            "scope":authorityScope as Any? ?? NSNull(),"old_chrony_training_present":contaminated])
        if contaminated, let service = managed {
            feeder?.stop();chrony?.stop();managedConnected = false
            service.recycle(reason:reason,now:tick())
        }
    }
    func refuseMissingSOF(_ reason: String, now: UInt64) {
        refusedSOFPauses += 1
        feeder?.pausePending();chrony?.invalidate();clock.reportPermissionUntil = 0
        precise.withhold(reason:reason)
        record(["type":"source_observation_refused","tick_ns":String(now),"reason":reason,
            "original_authority_deadline_ns":String(authorityDeadline),"authority_deadline_refreshed":false,
            "remaining_original_permission_ns":String(now < authorityDeadline ? authorityDeadline-now : 0)])
        fenceSource(reason)
    }
    func pauseMissingSOF(_ current: USBSourceRecord, requestBefore: UInt64, now: UInt64) {
        feeder?.pausePending();chrony?.invalidate();precise.withhold(reason:"CAPTURED_SOF_MAP_UNAVAILABLE")
        let failure = authorityRevoked ? "AUTHORITY_ALREADY_REVOKED" : precise.missingSOFRetentionFailure(current,
            scope:authorityScope,originalDeadline:authorityDeadline,requestBefore:requestBefore,now:now)
        if let failure = failure {refuseMissingSOF(failure,now:now);return}
        classifiedSOFPauses += 1
        record(["type":"source_observation_pause","tick_ns":String(now),"reason":"CAPTURED_SOF_MAP_UNAVAILABLE",
            "scope":current.scope,"source_sequence":current.sequence,"map_generation":current.generation,
            "original_authority_deadline_ns":String(authorityDeadline),"authority_deadline_refreshed":false,
            "remaining_original_permission_ns":String(authorityDeadline-now),"UTC_sample_valid":false,
            "private_training_retained":true])
    }
    func record(_ row: [String:Any]) {
        guard let file = log else {return}
        // Indefinite healthy service has a finite diagnostic window. Ending
        // that window closes logging only; it never shuts down the USB source.
        if options.continuous && tick()-start >= UInt64(options.duration*1e9) {
            log = nil
            let end: [String:Any] = ["type":"usb_end","tick_ns":String(tick()),"diagnostic_window_closed":true,
                "service_continues":true,"precision_qualified":false,"chrony_comparison":chronyRun.json(tick()),
                "chrony_refclock":feeder?.json as Any? ?? NSNull()]
            evidence.submit {var data = try JSONSerialization.data(withJSONObject:end,options:[.sortedKeys]);data.append(10);try file.write(contentsOf:data);try file.synchronize();try file.close()}
            return
        }
        evidence.submit { var data = try JSONSerialization.data(withJSONObject:row,options:[.sortedKeys]); data.append(10); try file.write(contentsOf:data) }
    }
    func accept(_ row: [String:Any]) {
        record(row)
        if row["type"] as? String == "usb_identity_rejection" {displayHealth.noteLoss("USB identity mismatch",at:tick())}
        if !stopped, ["usb_observer_end","usb_identity_rejection"].contains(row["type"] as? String ?? "") {fenceSource("USB_SESSION_ENDED");clock.reset();bridge.reset();nextUSB = tick()+5_000_000_000}
        guard !stopped, row["type"] as? String == "usb_raw", let helper = row["helper"] as? [String:Any] else {return}
        guard row["identity_verified"] as? Bool == true else {return}
        if helper["event"] as? String == "manifest" {bridge.configure(helper);return}
        guard helper["event"] as? String == "sample" else {return}
        if helper["reference_valid"] as? Bool == false {displayHealth.noteLoss("USB frame reference unavailable",at:tick())}
        bridge.ingest(helper)
        for key in ["precision_source","device_source"] {
            if helper[key+"_invalid_reason7"] as? Bool == true {
                refuseMissingSOF("MALFORMED_SOF_OBSERVATION_WIRE",now:tick())
                continue
            }
            if helper[key] is NSNull {displayHealth.noteLoss("USB \(key == "device_source" ? "UTC" : "timing") request unavailable",at:tick())}
            guard let packet = helper[key] as? [String:Any], let raw = helper[key+"_continuous_before"] as? String, let ticks = UInt64(raw) else { continue }
            let before = ticks/UInt64(clockScale.denom)*UInt64(clockScale.numer)+ticks%UInt64(clockScale.denom)*UInt64(clockScale.numer)/UInt64(clockScale.denom)
            let unavailable = clock.unavailable, rejected = clock.rejected
            let accepted = clock.accepted, scope = clock.latest?.scope
            clock.ingest(packet,requestBefore:before,received:tick())
            if clock.unavailable > unavailable {displayHealth.noteLoss("Source sample unavailable / reason \(packet["reason"] ?? "unknown")",at:tick())}
            if clock.rejected > rejected {
                displayHealth.noteLoss("USB source record rejected",at:tick())
                if packet["reason"] as? UInt32 == 7 || packet["sof_map_metadata"] != nil {
                    refuseMissingSOF("REJECTED_SOF_OBSERVATION_RECORD",now:tick())
                }
            }
            if clock.accepted > accepted {usb?.lastSourceAt = tick()}
            if clock.accepted > accepted, let current = clock.latest {
                if let old = authorityScope, old != current.scope {fenceSource("SOURCE_SCOPE_CHANGED")}
                if authorityDeadline > 0 && tick() >= authorityDeadline {fenceSource("ORIGINAL_PERMISSION_EXPIRED")}
                authorityScope = current.scope;authorityDeadline = clock.reportPermissionUntil;authorityRevoked = false
                precise.ingest(current,permissionUntil:clock.reportPermissionUntil)
                if options.experimentalFineChrony, let map = precise.map, map.record.sequence == current.sequence {
                    let wasQuarantined = fineChrony.quarantined
                    if !fineChrony.observe(map), !wasQuarantined, fineChrony.quarantined {
                        fenceSource("FINE_TRANSLATION_QUARANTINED")
                    }
                }
            } else if clock.unavailable > unavailable, let current = clock.latest {
                feeder?.pausePending();chrony?.invalidate()
                precise.withhold(reason:current.reason == 4 ? "PUBLICATION_BUSY" : "SOURCE_UNAVAILABLE")
                if current.reason == 7 {pauseMissingSOF(current,requestBefore:before,now:tick())}
                else if ![2,4].contains(current.reason) {fenceSource("SOURCE_PERMISSION_WITHDRAWN_OR_UNKNOWN")}
            }
            if scope != clock.latest?.scope {chrony?.invalidate()}
            if key == "device_source", clock.accepted > accepted, let source = USBSourceRecord(packet),
               let a = (helper[key+"_wall_before_ns"] as? String).flatMap({Int64($0)}),
               let b = (helper[key+"_wall_after_ns"] as? String).flatMap({Int64($0)}),
               let end = (helper[key+"_continuous_after"] as? String).flatMap({UInt64($0)}), end >= ticks {
                let dt = end-ticks, span = dt/UInt64(clockScale.denom)*UInt64(clockScale.numer)+dt%UInt64(clockScale.denom)*UInt64(clockScale.numer)/UInt64(clockScale.denom)
                intervals.add(record:source,wallBefore:a,wallAfter:b,monotonicSpanNS:span)
                if !options.experimentalFineChrony, span <= 100_000_000, b >= a, abs(Double(b-a)-Double(span)) <= 5_000_000,
                   let previous = feeder?.previousWall {
                    let middle = before+span/2
                    if middle <= previous.tick || abs(Double(a+(b-a)/2-previous.instant.ns)-Double(middle-previous.tick)) > 5_000_000 {
                        recycleChrony("HOST_WALL_STEP")
                    }
                }
                if !options.experimentalFineChrony && clock.accepted > accepted {feeder?.ingest(source,before:before,after:before+span,wallBefore:a,wallAfter:b,deadline:clock.reportPermissionUntil)}
            }
        }
    }
    func updateDisplayHealth(_ now: UInt64) {
        let wall = MacWallSample.read()
        let available = managed != nil || chrony != nil ? chrony?.utc(wall,feeder:feeder,source:clock) != nil : clock.reportedUTC(now) != nil
        let reason = usb?.error ?? managed?.error ?? chrony?.error ?? (chrony != nil ? "Chrony UTC/source unavailable" : "Source UTC permission expired")
        displayHealth.update(available:available,reason:reason,at:now)
    }
    func status(_ now: UInt64) {
        let wall = MacWallSample.read(), served = clock.shadowUTC(wall.tick), reported = clock.reportedUTC(wall.tick)
        run.observe(wall:wall,served:served,scope:clock.latest?.scope ?? "")
        let chronyUTC = chrony?.utc(wall,feeder:feeder,source:clock)
        var fineObservation: [String:Any] = [:]
        if options.experimentalFineChrony {
            let sample = fineChrony.sample(precise:precise,bridge:bridge,deadline:clock.reportPermissionUntil)
            let delivered = sample.flatMap {chrony?.utc($0.wall,feeder:feeder,source:clock)}
            fineComparison.observe(reference:sample?.utc,delivered:delivered,scope:sample?.record.scope)
            if let sample = sample {
                fineObservation = ["fine_estimate_utc_ns":String(sample.utc.ns),
                    "delivered_chrony_utc_ns":delivered.map {String($0.ns)} as Any? ?? NSNull(),
                    "chrony_minus_fine_ns":delivered.map {String($0.ns-sample.utc.ns)} as Any? ?? NSNull(),
                    "source_scope":sample.record.scope,"source_monotonic_ns":String(sample.sourceNS),
                    "projection_mach_absolute":String(sample.absolute),"original_permission_deadline_ns":String(sample.deadline),
                    "host_wall_ns":String(sample.wall.instant.ns),"wall_before_ns":String(sample.wall.before),"wall_after_ns":String(sample.wall.after),
                    "absolute_UTC_qualified":false,"independent_UTC_reference":false]
            }
        }
        if chrony != nil {chronyRun.observe(wall:wall,served:chronyUTC,scope:chrony?.anchor?.tracking.refid ?? "")}
        if reported != nil { sourceValid += 1 } else { sourceMissing += 1 }
        snapshot = ["type":"usb_status","tick_ns":String(wall.tick),"transport":"USB","source_label":usbDeviceLabel(usb?.selectedSerial),"serial":usb?.selectedSerial as Any? ?? NSNull(),
            "scope":clock.latest?.scope ?? "","shadow_ns":served.map {String($0.ns)} as Any? ?? NSNull(),
            "reported_device_utc_ns":reported.map {String($0.ns)} as Any? ?? NSNull(),"source_snapshot":clock.latest?.raw as Any? ?? NSNull(),"last_valid_source_snapshot":clock.lastValidRecord?.raw as Any? ?? NSNull(),
            "source_observation_classification":["retained_pauses":classifiedSOFPauses,"refused":refusedSOFPauses,"original_authority_deadline_ns":String(authorityDeadline)],
            "source_info":clock.periodic?.raw as Any? ?? NSNull(),"source_valid_status_samples":sourceValid,"source_unavailable_status_samples":sourceMissing,
            "source_packets_accepted":clock.accepted,"source_packets_unavailable":clock.unavailable,"source_packets_rejected":clock.rejected,
            "source_permission_remaining_ns":String(wall.tick < clock.reportPermissionUntil ? clock.reportPermissionUntil-wall.tick : 0),
            "frame_identity":bridge.identity,"precision_qualified":false,"shadow_gate":bridge.identity == "FRAME_TO_MACH_CAUSALLY_IDENTIFIED" ? "SOURCE_PPS_UTC_ASSOCIATION_UNVERIFIED" : "FRAME_IDENTITY_UNRESOLVED",
            "boundary_bridge":bridge.json,"precise_clock":precise.snapshot(bridge:bridge,absolute:mach_absolute_time(),now:wall.tick),"periodic_utc_offset_intervals":intervals.json,
            "chrony_input_mode":options.experimentalFineChrony ? "EXPERIMENTAL_FINE_UTC_ESTIMATE" : "COARSE_USB_REQUEST_MIDPOINT",
            "fine_chrony_estimate":fineChrony.json,
            "chrony_vs_fine_estimate":fineComparison.json,"fine_estimate_observation":fineObservation,
            "run_comparison":run.json(wall.tick),"ntp_check":ntp?.snapshot(wall.tick) as Any? ?? ["enabled":false],"usb_observation":usb?.snapshot(wall.tick) as Any? ?? NSNull(),"served_valid":served != nil,
            "display_health":displayHealth.json(now),"chrony":chronySnapshot(wall),"chrony_refclock":feeder?.json as Any? ?? ["enabled":false],
            "chrony_user_utc_ns":chronyUTC.map {String($0.ns)} as Any? ?? NSNull(),"chrony_comparison":chronyRun.json(wall.tick)]
        if options.chronyOnly {snapshot["transport"] = "CHRONY";snapshot["source_label"] = "local chronyd"}
        snapshot.merge(wall.json) {_,new in new}; record(snapshot)
        if !options.interactive {
            let data = ChronyDisplaySnapshot.read(chronySnapshot(wall),feeder:feeder?.json ?? [:],comparison:chronyRun.json(wall.tick),now:wall.tick)
            let text = (options.chronyOnly ? "Local chronyd" : "\(usbDeviceLabel(usb?.selectedSerial)) USB · captured UTC \(utcReadout(reported?.seconds)) · source \(reported == nil ? "UNAVAILABLE" : "VALID") · boundary \(bridge.identity) · SOFs \(usb?.valid ?? 0) · source packets \(clock.accepted)")+(usb?.error.map {"\nUSB ERROR  "+$0} ?? "")+(!data.enabled ? "" : "\nChrony \(data.state) · user UTC \(utcReadout(chronyUTC?.seconds)) · \(data.selected)"+(managed?.error.map {" · "+$0} ?? ""))+"\n"+(ntp?.line(wall.tick) ?? "NTP checks off")+"\n"
            display.submit {try FileHandle.standardOutput.write(contentsOf:Data(text.utf8))}
        }
    }
    func draw(_ now: UInt64) {
        guard options.interactive else { return }
        let wall = MacWallSample.read(); var model = InstrumentSnapshot.read(snapshot,origin:start,duration:options.duration,source:usbDeviceLabel(usb?.selectedSerial))
        if options.continuous {model.duration = 0}
        model.utcTitle = "DEVICE UTC / CAPTURED"; model.transport = "USB"; model.rate = options.rate
        model.chrony = ChronyDisplaySnapshot.read(chronySnapshot(wall),feeder:feeder?.json ?? [:],comparison:chronyRun.json(wall.tick),now:wall.tick)
        model.outageWarning = displayHealth.warning(now);model.lossIndicator = displayHealth.indicator(now);model.chrony.outageWarning = model.outageWarning
        model.usbError = usb?.error;model.usbRunning = usb?.running ?? false
        model.referenceLabel = "USB SOF → Mach";model.usbGateLabel = bridge.identity == "FRAME_TO_MACH_CAUSALLY_IDENTIFIED" ? "FRAME MAPPED" : "FRAME ID PENDING"
        let sourceMetadata = clock.latest?.utc != nil ? clock.latest : clock.lastValidRecord
        let rootName = displayName(sourceMetadata?.root)
        model.source = usbDeviceLabel(usb?.selectedSerial)+" ← "+rootName+(clock.latest?.utc == nil && sourceMetadata != nil ? " (last)" : ""); model.missing = sourceMissing; model.gapLabel = "SOURCE GAPS"
        model.utc = clock.reportedUTC(wall.tick); model.local = wall.instant; model.offsetNS = nil
        model.state = model.utc == nil ? "SOURCE UNAVAILABLE" : "SOURCE VALID / SHADOW GATED"
        model.qualification = options.experimentalFineChrony ? "EXPERIMENTAL FINE ESTIMATE / absolute UTC unqualified" : (bridge.identity == "FRAME_TO_MACH_CAUSALLY_IDENTIFIED" ? "FINE UTC GATED / PPS-to-UTC association unverified" : "USB SHADOW GATED / frame identity unverified / no BLE")
        if intervals.count > 0 {
            model.rmsTitle = "UTC RMS UPPER"; model.rmsNS = intervals.json["rms_upper_ns"] as? Double; model.valid = intervals.count
            model.biasNS = nil; model.biasText = String(format:"[%+.3f,%+.3f] ms",intervals.lowSum/Double(intervals.count)/1e6,intervals.highSum/Double(intervals.count)/1e6); model.maximumNS = intervals.maximumAbs
            model.jitterNS = intervals.json["midpoint_jitter_diagnostic_ns"] as? Double
            if let lo = intervals.latestLow, let hi = intervals.latestHigh {model.offsetText = String(format:"[%+.3f,%+.3f] ms",Double(lo)/1e6,Double(hi)/1e6)}
            model.qualification = "UTC request bounds / jitter includes USB / shadow gated"
        }
        if bridge.counter.n > 2 {
            let sourceFresh = clock.reportedUTC(wall.tick) != nil && bridge.source.n > 2
            model.rmsTitle = sourceFresh ? "SOURCE JITTER σ" : "COUNTER JITTER σ"
            model.rmsNS = sourceFresh ? bridge.source.detrendedSigma : bridge.counter.detrendedSigma
            model.valid = sourceFresh ? bridge.source.n : bridge.counter.n
            model.rateLabel = sourceFresh ? "SOURCE DRIFT" : "COUNTER DRIFT"
            model.ratePPM = (sourceFresh ? bridge.source.slope : bridge.counter.slope)*1e6
            model.jitterNS = bridge.counter.detrendedSigma;model.jitterLabel = "COUNTER σ"
            model.qualification = "SOF boundary detrended jitter / UTC accuracy unqualified"
        }
        model.permissionMS = wall.tick < clock.reportPermissionUntil ? Double(clock.reportPermissionUntil-wall.tick)/1e6 : 0
        if bridge.counter.n <= 2 {model.ratePPM = clock.latest.map {Double($0.ppb)/1000}}; model.mapPhaseNS = clock.latest?.raw["map_phase_ns"] as? Double
        if bridge.counter.n <= 2 {model.rateLabel = "MAP RATE"}; model.phaseLabel = "MAP PHASE"
        if let latest = clock.latest, let text = latest.raw["anchor_tick"] as? String, let anchor = UInt64(text), latest.current >= anchor { model.mapAgeS = Double(latest.current-anchor)*62.5/1e9+Double(latest.raw["map_age_us"] as? UInt32 ?? 0)/1e6 }
        model.details = ["USB relay / " + (usb?.selectedSerial ?? "discovering"),
            "Selection: " + (options.deviceSerial ?? "automatic / healthy source retained"),
            "Compatible discovered interfaces: \(usb?.candidates.count ?? 0)",
            "Root \(clock.latest?.root ?? "unknown") / source epoch \(clock.latest?.sourceEpoch ?? 0)",
            "Map generation \(clock.latest?.generation ?? 0) / capture epoch \(clock.latest?.captureEpoch ?? 0)",
            "Source records accepted \(clock.accepted) unavailable \(clock.unavailable) rejected \(clock.rejected)",
            "Frame identity \(bridge.identity); markers \(bridge.anchors)",
            "Source mono σ \(bridge.source.detrendedSigma) ns; drift \(bridge.source.slope*1e6) ppm",
            "Exported UTC offset varies independently of source mono.",
            "Device UTC is a captured value; no arrival-time projection.",
            "Precision Mach shadow requires source UTC qualification.","RMS upper uses full USB request intervals.","Total source/USB/host uncertainty remains UNKNOWN.",usb?.line ?? "USB not opened",ntp?.line(wall.tick) ?? "NTP off"]
        if let chrony = chrony {
            model.local = wall.instant;model.applyChronyClock()
            model.details = ["Chrony virtual UTC = local wall + CSV signed correction",
                "No macOS clock or sync setting changes.","Command socket: \(chrony.socket)",
                "Refid \(chrony.anchor?.tracking.refid ?? "unknown") / stratum \(chrony.anchor?.tracking.stratum ?? 0)",
                "Fresh correction, reference age and local wall continuity required.",
                "Checks \(chrony.checks) / failures \(chrony.failures)","Latest error: \(chrony.error ?? "none")",
                "Refclock sent \(feeder?.sent ?? 0) / reset \(feeder?.resets ?? 0) / failed \(feeder?.failures ?? 0)",
                "BLE UTC is bound to the selected USB silicon identity.","USB UTC source/transport uncertainty remains unqualified.",
                "A same-source chrony comparison is not independent accuracy.","USB captured UTC: \(utcReadout(clock.reportedUTC(wall.tick)?.seconds))"]
        }
        if let service = managed {
            model.details = ["Managed chrony: \(service.state.rawValue)","Service error: \(service.error ?? "none")",
                "Chronyd: \(service.daemonPath ?? "pending")","Runtime: \(service.directory ?? "none")",
                "Daemon diagnostic: \(service.snapshot["diagnostic"] as? String ?? "none")"]+model.details
        }
        if let usb = usb, let error = usb.error {model.details = ["USB: \(error)","Helper: \(usb.helperPath ?? "unresolved")","Diagnostic: \(usb.diagnostic ?? "none")","Retry: automatic after 5 seconds; q closes safely."]+model.details}
        let fine = precise.snapshot(bridge:bridge,absolute:mach_absolute_time(),now:wall.tick)
        model.details = ["Fine clock: \(fine["state"] ?? "unavailable")",
            "Causal bridge RMS: \(bridge.counterInnovations.n > 0 ? String(format:"%.3f us",bridge.counterInnovations.rmse/1000) : "warming") / \(bridge.counterInnovations.n) predictions",
            "Fine UTC requires verified captured PPS/UTC association."]+model.details
        if let indicator = model.lossIndicator {model.details = [indicator,"Amber requires 500ms continuous outage; reasons hold at least 500ms."]+model.details}
        let displayScope = options.chronyOnly ? "chrony:\(chrony?.socket ?? "")" : "\(usb?.selectedSerial ?? ""):\(clock.latest.map {"\($0.boot):\($0.usbEpoch):\($0.captureEpoch)"} ?? ""):\(authorityScope ?? "")"
        readoutHold.apply(to:&model,scope:displayScope,now:now,unavailableSince:displayHealth.unavailableSince)
        var size = winsize(); _ = ioctl(STDOUT_FILENO,TIOCGWINSZ,&size)
        let text = InstrumentFace.render(model,width:Int(size.ws_col > 0 ? size.ws_col : 80),height:Int(size.ws_row > 0 ? size.ws_row : 24),details:page == .diagnostics,help:page == .help,chronyPage:page == .chrony)
        display.submit { try FileHandle.standardOutput.write(contentsOf:Data(text.utf8)) }
    }
    func pulse() {
        guard !stopped else { return }; let now = tick()
        if savedTerm != nil {
            var bytes = [UInt8](repeating:0,count:32);let n = read(STDIN_FILENO,&bytes,bytes.count)
            if n > 0 {for key in bytes.prefix(n) {
                if key == 113 || key == 3 {finish();return}
                let previous = page;page.navigate(key)
                if page != previous {record(["type":"instrument_page","tick_ns":String(now),"from":previous.rawValue,"to":page.rawValue])}
                if key == 114 {fenceSource("OPERATOR_REACQUIRE");clock.reset()}
            }}
        }
        if !options.continuous && now-start >= UInt64(options.duration*1e9) { finish(); return }
        pollManagedChrony(now)
        if authorityScope != nil && authorityDeadline > 0 && now >= authorityDeadline && !authorityRevoked {fenceSource("ORIGINAL_PERMISSION_EXPIRED")}
        if let usb = usb, !usb.running, now >= nextUSB {startUSB()}
        usb?.rotateIfUnavailable(now)
        chrony?.poll(now)
        if options.experimentalFineChrony && now >= nextFineFeed {
            nextFineFeed = now+125_000_000
            if let sample = fineChrony.sample(precise:precise,bridge:bridge,deadline:clock.reportPermissionUntil) {
                if let previous = feeder?.previousWall,
                   sample.wall.tick <= previous.tick || abs(sample.wall.instant.distance(from:previous.instant)-Double(sample.wall.tick-previous.tick)/1e9) > 0.005 {
                    recycleChrony("HOST_WALL_STEP")
                } else {feeder?.ingestFine(sample)}
            }
        }
        ntp?.poll(now)
        updateDisplayHealth(now)
        if now >= nextStatus { nextStatus = now+1_000_000_000; status(now) }
        if now >= nextDraw { nextDraw = now+100_000_000; draw(now) }
    }
    func finish() {
        guard !stopped else { return }; stopped = true; timer?.cancel(); ntp?.stop();usb?.stop();feeder?.stop();chrony?.stop();clock.reset()
        status(tick())
        done.signal()
    }
    func endRecord() {
        record(["type":"usb_end","tick_ns":String(tick()),"serial":usb?.selectedSerial as Any? ?? NSNull(),"run_comparison":run.json(tick()),"boundary_bridge":bridge.json,"periodic_utc_offset_intervals":intervals.json,"source_valid_status_samples":sourceValid,"source_unavailable_status_samples":sourceMissing,"frame_identity":bridge.identity,"precision_qualified":false,"chrony_comparison":chronyRun.json(tick()),"chrony_refclock":feeder?.json as Any? ?? NSNull(),"fine_chrony_estimate":fineChrony.json,"chrony_vs_fine_estimate":fineComparison.json])
    }
    func startUSB() {
        guard !stopped, let usb = usb else {return}
        fenceSource("USB_REACQUIRE");clock.reset();bridge.reset();nextUSB = tick()+5_000_000_000
        usb.start(serial:options.deviceSerial,duration:options.continuous ? 1800 : max(1,options.duration-Double(tick()-start)/1e9),sourceRecords:true,rate:options.rate)
    }
    func live() {
        start = tick(); activity = ProcessInfo.processInfo.beginActivity(options:.userInitiated,reason:"USB relay clock evidence")
        if options.interactive { var t = termios(); if tcgetattr(STDIN_FILENO,&t) == 0 { savedTerm = t; savedFlags = fcntl(STDIN_FILENO,F_GETFL); t.c_lflag &= ~tcflag_t(ICANON|ECHO); tcsetattr(STDIN_FILENO,TCSANOW,&t); _ = fcntl(STDIN_FILENO,F_SETFL,savedFlags|O_NONBLOCK); try? FileHandle.standardOutput.write(contentsOf:Data(InstrumentFace.enter.utf8)) } }
        record(["type":"usb_start","schema":1,"tick_ns":String(start),"tool":toolVersion,"source_sha256":buildSourceSHA256,"registry_sha256":displayRegistrySHA256,"serial":usb?.selectedSerial as Any? ?? NSNull(),"transport":options.chronyOnly ? "CHRONY" : "USB_ONLY","BLE":"DISABLED_NOT_LINKED","precision_stream_requested_hz":options.rate,"periodic_source_requested_hz":1,"frame_identity":"UNRESOLVED","precision_qualified":false,"continuous":options.continuous,"chrony_enabled":chrony != nil || managed != nil,"chrony_input_mode":options.experimentalFineChrony ? "EXPERIMENTAL_FINE_UTC_ESTIMATE" : "COARSE_USB_REQUEST_MIDPOINT","requested_serial":options.deviceSerial as Any? ?? NSNull()])
        if !options.chronyOnly {usb = USBObservation(owner:owner,record:{self.accept($0)});startUSB()}
        if options.checkNTP { ntp = LocalNTPMonitor(server:options.ntpServer ?? LocalNTPMonitor.configuredServer(),owner:owner,record:{self.record($0)}) }
        for number in [SIGINT,SIGTERM] { signal(number,SIG_IGN); let s = DispatchSource.makeSignalSource(signal:number,queue:owner); s.setEventHandler {self.finish()}; s.resume(); signals.append(s) }
        let t = DispatchSource.makeTimerSource(queue:owner); t.schedule(deadline:.now(),repeating:.milliseconds(25),leeway:.milliseconds(1)); t.setEventHandler {self.pulse()}; t.resume(); timer = t
        while done.wait(timeout:.now()) != .success { RunLoop.main.run(until:Date(timeIntervalSinceNow:0.05)) }
        usb?.wait();feeder?.wait();chrony?.wait();owner.sync {};managed?.stop(); for s in signals {s.cancel()}; display.waitUntilIdle()
        endRecord()
        if var t = savedTerm { tcsetattr(STDIN_FILENO,TCSANOW,&t); _ = fcntl(STDIN_FILENO,F_SETFL,savedFlags); try? FileHandle.standardOutput.write(contentsOf:Data(InstrumentFace.leave.utf8)) }
        if let activity = activity { ProcessInfo.processInfo.endActivity(activity) }
        evidence.submit {try self.log?.synchronize();try self.log?.close()}; evidence.waitUntilIdle()
        print("USB relay capture closed. Source-valid status observations: \(sourceValid); unavailable: \(sourceMissing). Boundary: \(bridge.identity). Absolute UTC accuracy remains unqualified.")
    }
}

func usbOptionsTests() -> Int {
    var count = 0
    func check(_ condition: Bool, _ label: String) {
        precondition(condition,label);count += 1
    }
    do {
        let normal = try USBOptions(arguments:[])
        check(normal.managedChrony && normal.experimentalFineChrony,"normal launch uses managed fine input")
        check(!(try USBOptions(arguments:["--coarse-chrony"])).experimentalFineChrony,"coarse baseline requires explicit selection")
        check((try USBOptions(arguments:["--experimental-fine-chrony"])).experimentalFineChrony,"legacy fine flag remains compatible")
        let disabled = try USBOptions(arguments:["--no-chrony"])
        check(!disabled.managedChrony && !disabled.experimentalFineChrony,"USB-only mode starts no fine feeder")
        let externalArgs = ["--chrony-command-socket","/tmp/ptob-option-test.sock","--chronyc","/usr/bin/true"]
        let external = try USBOptions(arguments:externalArgs+["--chrony-sock","/tmp/ptob-option-feed.sock"])
        check(!external.managedChrony && !external.experimentalFineChrony,"external feeding retains the coarse contract")
        let observer = try USBOptions(arguments:externalArgs+["--chrony-only"])
        check(observer.chronyOnly && !observer.experimentalFineChrony,"read-only attachment needs no fine-mode opt-out")
        for args in [["--no-chrony"],externalArgs,externalArgs+["--chrony-only"]] {
            var rejected = false
            do {_ = try USBOptions(arguments:args+["--experimental-fine-chrony"])} catch {rejected = true}
            check(rejected,"explicit fine input still requires an owned managed USB session")
        }
    } catch {preconditionFailure("USB option contract: \(error)")}
    return count
}

@main struct USBClockApp {
    static func main() {
        _ = umask(0o077)
        signal(SIGPIPE,SIG_IGN)
        do {
            let options = try USBOptions()
            if options.test { let n = usbOptionsTests()+usbObservationTests()+usbOnlyClockTests()+usbFrameBridgeTests()+preciseClockTests()+fineChronyTests()+sofMapTests()+clockComparisonTests()+instrumentViewTests()+chronyTests()+chronyViewTests()+displayHealthTests(); print("PASS \(n) USB-only source/clock/permission/instrument/chrony checks; no hardware or BLE access") }
            else if let path = options.precisionReplay {let result = try replayPrecisionCapture(path);let data = try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys,.prettyPrinted]);print(String(decoding:data,as:UTF8.self))}
            else if let path = options.preview {try previewInstrument(path,width:options.columns,height:options.rows,available:true,page:options.initialPage)}
            else if let path = options.report { try exportClockTrial(path,destination:options.reportOut) }
            else { try USBInstrument(options).live() }
        } catch { fputs("Error: \(error)\n",stderr); exit(1) }
    }
}
