import Foundation

enum InstrumentPage: String {
    case main, diagnostics = "details", chrony, help
    mutating func navigate(_ key: UInt8) {
        switch key {
        case 49,27: self = .main
        case 99: self = self == .chrony ? .main : .chrony
        case 100: self = self == .diagnostics ? .main : .diagnostics
        case 104: self = self == .help ? .main : .help
        default: break
        }
    }
}

/// Terminal instrument face. Only the readout layer knows about ANSI; logs and
/// clock discipline keep their numeric contracts and original timing evidence.
struct InstrumentSnapshot {
    var source = "SEARCHING", state = "ACQUIRING", utc: SourceInstant?, local: SourceInstant?
    var transport = "BLE", utcTitle = "UTC SHADOW", qualification = "UNQUALIFIED / RMS & jitter vs local / NTP error retained"
    var jitterLabel = "JITTER σ", gapLabel = "GAPS", rateLabel = "RATE", phaseLabel = "MAP Δ"
    var rmsTitle = "RUN RMS", rmsCaption: String?, offsetText: String?, biasText: String?
    var referenceLabel = "local macOS", usbGateLabel = "UTC GATED"
    var elapsed = 0.0, duration = 600.0, rate = 0, replies = 0, timeouts = 0
    var valid = 0, missing = 0, wallSteps = 0
    var rmsNS: Double?, biasNS: Double?, maximumNS: Double?, offsetNS: Double?
    var jitterNS: Double?, ratePPM: Double?, mapPhaseNS: Double?, mapAgeS: Double?, permissionMS = 0.0
    var ntpEnabled = false, ntpFresh = false, ntpOffsetNS: Double?, ntpUncertaintyNS: Double?, ntpError: String?
    var usbEnabled = false, usbRunning = false, usbValid = 0, usbUnavailable = 0, usbError: String?
    var outageWarning: Bool?, lossIndicator: String?
    var heldReadout: HeldMainReadout?
    var details: [String] = []
    var chrony = ChronyDisplaySnapshot()
    var replay = false
    mutating func applyRXSourceQuality(_ quality: [String:Any], sourceFresh: Bool) {
        rmsTitle = "RX USB RMS";valid = quality["n"] as? Int ?? 0
        rmsNS = sourceFresh && quality["available"] as? Bool == true ? quality["rmse_ns"] as? Double : nil
        rmsCaption = "\(valid) predictions / pre-chrony"
        biasText = nil;biasNS = quality["mean_ns"] as? Double;maximumNS = quality["maximum_absolute_ns"] as? Double
        jitterLabel = "RX JITTER σ";jitterNS = quality["sigma_ns"] as? Double
        qualification = "Corrected source / prior-fit RX residual / UTC accuracy unqualified"
    }
    mutating func applyChronyClock() {
        guard chrony.enabled else {return}
        utcTitle = "CHRONY UTC / USER CLOCK";utc = chrony.fresh ? chrony.utc : nil
        transport = usbEnabled ? "USB → chrony" : "CHRONY"
        rate = 1 // nominal tracking-query cadence, separate from raw USB sampling
        state = utc == nil ? "CHRONY UTC WITHHELD" : "CHRONY TRACKING / UNQUALIFIED"
        source = chrony.feedEnabled ? "BLE via "+(chrony.usbRelay ?? "USB relay") : "local chronyd"
        referenceLabel = "chronyd / "+chrony.selected
        rmsTitle = chrony.historical ? "CHRONY RMS (last)" : "CHRONY EST RMS";rmsNS = chrony.estimatorRMSNS
        rmsCaption = chrony.historical ? "last successful estimate" : "chrony tracking estimate"
        biasText = nil;offsetText = nil;offsetNS = utc.flatMap {u in local.map {u.distance(from:$0)*1e9}}
        biasNS = chrony.localBiasNS;maximumNS = chrony.localMaximumNS
        jitterNS = chrony.localJitterNS;jitterLabel = "vs LOCAL σ"
        valid = chrony.localSamples;missing = chrony.localGaps;gapLabel = "CHRONY GAPS"
        rateLabel = "CHRONY FREQ";ratePPM = chrony.frequencyPPM;mapPhaseNS = nil;mapAgeS = chrony.referenceAgeS
        qualification = "Experimental chrony user clock / UTC accuracy unqualified"
    }
    static func read(_ row: [String:Any], origin: UInt64, duration: Double, source: String) -> InstrumentSnapshot {
        var s = InstrumentSnapshot(); s.source = source; s.duration = duration
        s.transport = row["transport"] as? String ?? "BLE"
        s.utcTitle = row["utc_title"] as? String ?? "UTC SHADOW"
        s.qualification = row["qualification_note"] as? String ?? s.qualification
        let now = (row["tick_ns"] as? String).flatMap {UInt64($0)} ?? origin
        s.elapsed = now >= origin ? Double(now-origin)/1e9 : 0
        s.utc = (row["shadow_ns"] as? String).flatMap {Int64($0)}.map {SourceInstant(ns:$0)}
        s.local = (row["host_wall_ns"] as? String).flatMap {Int64($0)}.map {SourceInstant(ns:$0)}
        s.rate = row["granted_hz"] as? Int ?? 0; s.replies = row["responses"] as? Int ?? 0; s.timeouts = row["timeouts"] as? Int ?? 0
        let d = row["shadow_diagnostics"] as? [String:Any]
        s.ratePPM = d?["apparent_rate_ppm"] as? Double; s.mapPhaseNS = d?["served_minus_fit_ns"] as? Double
        if let at = d?["fit_available_ns"] as? UInt64, now >= at { s.mapAgeS = Double(now-at)/1e9 }
        s.permissionMS = (row["source_permission_remaining_ns"] as? String).flatMap {Double($0)}.map {$0/1e6} ?? 0
        s.state = s.utc == nil ? (s.rate > 0 ? "SOURCE UNAVAILABLE" : "ACQUIRING") : (d?["projection"] as? String == "NOMINAL_UNCALIBRATED" ? "ACQUIRING RATE" : "TRACKING")
        let run = row["run_comparison"] as? [String:Any]
        s.valid = run?["valid_status_samples"] as? Int ?? 0; s.missing = run?["unavailable_status_samples"] as? Int ?? 0
        s.rmsNS = run?["rms_ns"] as? Double; s.biasNS = run?["mean_ns"] as? Double; s.maximumNS = run?["max_abs_ns"] as? Double
        s.jitterNS = run?["jitter_stddev_ns"] as? Double
        s.wallSteps = run?["local_wall_step_observations"] as? Int ?? 0
        if let utc = s.utc, let local = s.local { s.offsetNS = utc.distance(from:local)*1e9 }
        let ntp = row["ntp_check"] as? [String:Any], latest = ntp?["latest"] as? [String:Any]
        s.ntpEnabled = ntp?["enabled"] as? Bool ?? false; s.ntpFresh = ntp?["fresh"] as? Bool ?? false
        s.ntpOffsetNS = (latest?["server_minus_local_ns"] as? Double).map {-$0}; s.ntpUncertaintyNS = latest?["reported_uncertainty_ns"] as? Double; s.ntpError = ntp?["last_error"] as? String
        let usb = row["usb_observation"] as? [String:Any]
        s.usbEnabled = usb != nil; s.usbRunning = usb?["running"] as? Bool ?? false
        s.usbValid = usb?["raw_valid"] as? Int ?? 0; s.usbUnavailable = usb?["raw_unavailable"] as? Int ?? 0; s.usbError = usb?["error"] as? String
        let health = row["display_health"] as? [String:Any]
        s.outageWarning = health?["warning"] as? Bool;s.lossIndicator = health?["indicator"] as? String
        s.chrony = ChronyDisplaySnapshot.read(row["chrony"] as? [String:Any] ?? [:],feeder:row["chrony_refclock"] as? [String:Any] ?? [:],comparison:row["chrony_comparison"] as? [String:Any] ?? [:],now:now)
        s.chrony.outageWarning = s.outageWarning
        return s
    }
}

enum InstrumentFace {
    static let background = "\u{001B}[48;5;233m", primary = "\u{001B}[38;5;157m", secondary = "\u{001B}[38;5;108m", warning = "\u{001B}[38;5;179m"
    static let enter = "\u{001B}[?1049h\u{001B}[?25l"+background+"\u{001B}[2J"
    static let leave = "\u{001B}[0m\u{001B}[?25h\u{001B}[?1049l"
    private static let glyphs: [Character:[String]] = [
        "0":["███","█ █","█ █","█ █","███"],"1":[" ██","  █","  █","  █","  █"],
        "2":["███","  █","███","█  ","███"],"3":["███","  █","███","  █","███"],
        "4":["█ █","█ █","███","  █","  █"],"5":["███","█  ","███","  █","███"],
        "6":["███","█  ","███","█ █","███"],"7":["███","  █","  █","  █","  █"],
        "8":["███","█ █","███","█ █","███"],"9":["███","█ █","███","  █","███"],
        ":":[" ","█"," ","█"," "],".":[" "," "," "," ","█"],"-":["   ","   ","███","   ","   "]," ":["   ","   ","   ","   ","   "]]
    static func digits(_ text: String) -> [String] { (0..<5).map { row in text.map {glyphs[$0]?[row] ?? "   "}.joined(separator:" ") } }
    static func elapsed(_ seconds: Double) -> String { let n = max(0,Int(seconds)); return String(format:"%02d:%02d",n/60,n%60) }
    static func measurement(_ ns: Double?, signed: Bool = false) -> String {
        guard let value = ns, value.isFinite else { return "---" }
        let a = abs(value), divisor = a >= 1e9 ? 1e9 : a >= 1e6 ? 1e6 : 1000
        let unit = divisor == 1e9 ? "s" : divisor == 1e6 ? "ms" : "µs"
        return String(format:signed ? "%+.3f %@" : "%.3f %@",value/divisor,unit)
    }
    static func rmsDisplay(_ ns: Double?) -> (text: String, unit: String) {
        guard let ns = ns, ns.isFinite else {return ("---","ms")}
        let divisor = abs(ns) >= 1e9 ? 1e9 : abs(ns) >= 1e6 ? 1e6 : abs(ns) >= 1000 ? 1000.0 : 1.0
        let unit = divisor == 1e9 ? "s" : divisor == 1e6 ? "ms" : divisor == 1000 ? "µs" : "ns"
        return (String(format:divisor == 1 ? "%.0f" : "%.2f",ns/divisor),unit)
    }
    static func rmsMeasurement(_ ns: Double?) -> String {
        guard let ns = ns, ns.isFinite else {return "---"}
        return abs(ns) < 1000 ? String(format:"%.0f ns",ns) : measurement(ns)
    }
    static func render(_ s: InstrumentSnapshot, width: Int, height: Int, details: Bool = false, help: Bool = false, chronyPage: Bool = false, ansi: Bool = true) -> String {
        let columns = max(16,width-1), rows = max(4,height-1)
        func clip(_ text: String) -> String { String(text.filter {!$0.unicodeScalars.contains(where:{CharacterSet.controlCharacters.contains($0)})}.prefix(columns)) }
        func pair(_ left: String, _ right: String) -> String { let room = columns-right.count-2; return String(left.prefix(max(0,room)))+String(repeating:" ",count:max(2,columns-min(left.count,max(0,room))-right.count))+right }
        var lines: [(String,String)] = []
        func line(_ text: String = "", _ color: String = secondary) { lines.append((clip(text),color)) }
        let alert = s.outageWarning ?? (s.utc == nil)
        let rule = String(repeating:"─",count:columns)
        line(pair("PToB  /  TIME COMPARATOR"+(s.replay ? " / REPLAY" : ""),"RUN "+elapsed(s.elapsed)+" / "+(s.duration == 0 ? "SERVICE" : elapsed(s.duration))),primary)
        line(s.lossIndicator ?? rule,s.lossIndicator == nil ? secondary : warning)
        if help {
            line("INSTRUMENT CONTROLS",primary); line()
            for text in ["1 / Esc   Main instrument","c   Show / hide chrony statistics","d   Show / hide transport diagnostics","r   Reacquire source; run statistics stay cumulative.","h   Show / hide this help","q   Finish capture and write the report","Ctrl-C also closes the capture safely.","","UTC output stops when source permission expires.",s.transport == "USB" ? "OFFSET  captured UTC − local request interval" : "OFFSET / BIAS  served shadow − local macOS",s.transport == "USB" ? "RX RMS  corrected source, previous-fit prediction" : "RMS / JITTER σ  error RMS / centered error stddev",s.transport == "USB" ? "RX JITTER σ  centered residual; RMS retains bias" : "Run statistics remain cumulative.",s.transport == "USB" ? "MAP PHASE / RATE  USB source-map parameters" : "MAP Δ  served shadow − fitted source time","CHRONY NTP−SYS  signed correction added to local wall","EST RMS / EST DIST  chrony model, not UTC accuracy","NTP  local macOS − NTP server (± uncertainty)","NTP is checked independently every 60 seconds."] { line(text) }
        } else if chronyPage {
            for (text,color) in ChronyFace.lines(s.chrony,columns:columns) {line(text,color)}
        } else if details {
            line("SOURCE & TRANSPORT DIAGNOSTICS",primary); line()
            for value in s.details { line(value) }
        } else {
            let displayUTC = s.utc ?? s.heldReadout?.utc
            let stamp = utcReadout(displayUTC?.seconds)
            let clockText = displayUTC == nil ? "--:--:--.---" : String(stamp.dropFirst(11).prefix(12))
            let withheld = s.usbError != nil && !s.usbRunning ? "UTC withheld · USB input unavailable" : s.chrony.enabled ? "UTC withheld · chrony update/source required" : "UTC withheld · awaiting valid source permission"
            let date = displayUTC == nil ? withheld : String(stamp.prefix(10))+"  UTC"+(s.heldReadout != nil ? " / HELD <500ms" : "")
            let rmsValue = s.heldReadout.map(\.rmsNS) ?? s.rmsNS
            let rmsTitle = s.heldReadout?.rmsTitle ?? s.rmsTitle
            let rmsCaption = s.heldReadout != nil ? "last displayed estimate / HELD" : s.rmsCaption
            if columns >= 74 && rows >= 21 {
                let leftWidth = 47
                let rms = rmsDisplay(rmsValue)
                func split(_ left: String,_ right: String) -> String { String(left.prefix(leftWidth))+String(repeating:" ",count:max(1,leftWidth-left.count))+"  "+right }
                line(split(s.utcTitle,rmsTitle+"  ["+rms.unit+"]"),primary)
                let clock = digits(clockText), largeRMS = digits(rms.text)
                for i in 0..<5 { line(split(clock[i],largeRMS[i]),alert ? warning : primary) }
                line(split(date,rmsCaption ?? "\(s.valid) observations"))
            } else {
                line(s.utcTitle,primary)
                if columns >= 32 && rows >= 19 { for row in digits(String(clockText.prefix(8))) { line(row,alert ? warning : primary) } }
                else { line(clockText,alert ? warning : primary) }
                line(date)
                line(rmsTitle+"  "+rmsMeasurement(rmsValue)+" / "+(rmsCaption ?? "\(s.valid) samples"),primary)
            }
            line()
            let local = s.local.map {String(utcReadout($0.seconds).dropFirst(11).prefix(12))} ?? "unavailable"
            line(pair("LOCAL UTC  "+local,"OFFSET  "+(s.offsetText ?? measurement(s.offsetNS,signed:true))),primary)
            line(pair("BIAS  "+(s.biasText ?? measurement(s.biasNS,signed:true)),(s.transport == "USB" ? "MAX UPPER  " : "MAX  ")+measurement(s.maximumNS)))
            line(pair(s.jitterLabel+"  "+measurement(s.jitterNS),s.rateLabel+"  "+(s.ratePPM.map {String(format:"%+.3f ppm",$0)} ?? "warming")))
            if s.chrony.enabled {
                line(pair("vs LOCAL RMS  "+rmsMeasurement(s.chrony.localRMSNS),"EST DIST  "+measurement(s.chrony.fresh ? s.chrony.rootDistanceNS : nil)))
            } else {line(pair(s.phaseLabel+"  "+measurement(s.mapPhaseNS,signed:true),"AGE "+(s.mapAgeS.map {String(format:"%.1f s",$0)} ?? "---")+String(format:" / VALID %.0f ms",s.permissionMS)))}
            line(rule)
            line(pair("SOURCE  \(s.source) / \(s.transport) \(s.rate) Hz",s.state),alert ? warning : primary)
            if s.chrony.enabled {
                let right = s.chrony.fresh ? "NTP−SYS "+measurement(s.chrony.correctionNS,signed:true)+" / "+ChronyFace.seconds(s.chrony.queryAgeS) : "UTC WITHHELD / c details"
                line(pair("CHRONY  "+s.chrony.state+" / "+s.chrony.selected,right),alert ? warning : primary)
            } else {line(pair("REFERENCE  "+s.referenceLabel,s.gapLabel+"  \(s.missing)"))}
            let ntpReading = s.ntpOffsetNS.map { measurement($0,signed:true)+" ± "+measurement(s.ntpUncertaintyNS) }
            let ntpState = !s.ntpEnabled ? "OFF" : s.ntpError != nil ? "CHECK FAILED" : s.ntpFresh ? "RECENT" : "UNVERIFIED"
            line(pair("NTP  "+(ntpReading ?? (s.ntpEnabled ? "checking local clock…" : "not requested")),ntpState),s.ntpError != nil ? warning : secondary)
            if s.usbEnabled, let error = s.usbError {line("USB ERROR  "+error,alert ? warning : secondary)}
            else if s.usbEnabled { line(pair("USB  RELAY / RAW \(s.usbValid)"+(s.usbRunning ? "" : " / STOPPED"),s.transport == "USB" ? s.usbGateLabel : "UTC GATED"),secondary) }
            else { line("USB  not requested") }
            line(rule)
            line(s.qualification,warning)
        }
        let footer = columns < 60 ? "1 Main  c Chrony  q Finish" : "1 Main    c Chrony    d Details    h Help    q Finish"
        if lines.count > rows-1 { lines = Array(lines.prefix(rows-2)); line("Enlarge terminal for remaining readings",warning) }
        while lines.count < rows-1 { line() }
        line(footer,primary)
        return (ansi ? "\u{001B}[H"+background : "")+lines.map { text,color in
            let padded = text+String(repeating:" ",count:max(0,columns-text.count))
            return ansi ? color+padded : padded
        }.joined(separator:"\n")+(ansi ? "\u{001B}[0m" : "")
    }
}

func previewInstrument(_ path: String,width: Int,height: Int,available: Bool,page: InstrumentPage = .main) throws {
    let text = try String(contentsOfFile:path,encoding:.utf8)
    var origin: UInt64 = 0, selected: [String:Any]?, source = "RC", selectedSource = "RC", run = LocalClockComparison()
    for line in text.split(separator:"\n") {
        guard let row = try JSONSerialization.jsonObject(with:Data(line.utf8)) as? [String:Any] else { continue }
        if ["ucs1_start","usb_start"].contains(row["type"] as? String ?? "") { origin = (row["tick_ns"] as? String).flatMap {UInt64($0)} ?? 0 }
        if row["type"] as? String == "ucs1_state", let id = row["server"] as? String { source = displayName(id) }
        if ["ucs1_status","usb_status"].contains(row["type"] as? String ?? "") {
            var enriched = row
            if let wall = try MacWallSample.decode(row) {
                let served = (row["shadow_ns"] as? String).flatMap {Int64($0)}.map {SourceInstant(ns:$0)}
                run.observe(wall:wall,served:served,scope:row["scope"] as? String ?? "")
                enriched["run_comparison"] = run.json(wall.tick)
            }
            if !available || page == .chrony || row["shadow_ns"] is String || row["reported_device_utc_ns"] is String || row["chrony_user_utc_ns"] is String { selected = enriched; selectedSource = row["source_label"] as? String ?? source }
        }
    }
    guard let row = selected else { throw NSError(domain:"No matching instrument status",code:1) }
    var snapshot = InstrumentSnapshot.read(row,origin:origin,duration:600,source:selectedSource); snapshot.replay = true
    if row["transport"] as? String == "USB" {
        let source = row["source_snapshot"] as? [String:Any], bounds = row["periodic_utc_offset_intervals"] as? [String:Any]
        snapshot.transport = "USB"; snapshot.utcTitle = "DEVICE UTC / CAPTURED"; snapshot.rmsTitle = "UTC RMS UPPER"
        snapshot.utc = (row["reported_device_utc_ns"] as? String).flatMap {Int64($0)}.map {SourceInstant(ns:$0)}
        snapshot.source = (row["source_label"] as? String ?? usbDeviceLabel(row["serial"] as? String))+" ← "+displayName(source?["root"] as? String)
        snapshot.state = snapshot.utc == nil ? "SOURCE UNAVAILABLE" : "SOURCE VALID / SHADOW GATED"
        snapshot.rmsNS = bounds?["rms_upper_ns"] as? Double; snapshot.valid = bounds?["n"] as? Int ?? 0
        if let lo = bounds?["bias_low_ns"] as? Double, let hi = bounds?["bias_high_ns"] as? Double {snapshot.biasText = String(format:"[%+.3f,%+.3f] ms",lo/1e6,hi/1e6)}
        snapshot.permissionMS = (row["source_permission_remaining_ns"] as? String).flatMap {Double($0)}.map {$0/1e6} ?? 0
        snapshot.maximumNS = bounds?["max_abs_upper_ns"] as? Double; snapshot.biasNS = nil
        snapshot.jitterNS = bounds?["midpoint_jitter_diagnostic_ns"] as? Double
        if let a = bounds?["latest_low_ns"] as? String, let b = bounds?["latest_high_ns"] as? String, let lo = Double(a), let hi = Double(b) { snapshot.offsetText = String(format:"[%+.3f,%+.3f] ms",lo/1e6,hi/1e6) }
        snapshot.missing = row["source_unavailable_status_samples"] as? Int ?? 0; snapshot.gapLabel = "SOURCE GAPS"
        snapshot.rate = 32; snapshot.rateLabel = "MAP RATE"; snapshot.phaseLabel = "MAP PHASE"
        snapshot.ratePPM = (source?["map_ppb"] as? Double).map {$0/1000}; snapshot.mapPhaseNS = source?["map_phase_ns"] as? Double
        snapshot.qualification = "UTC request bounds / jitter includes USB / shadow gated"
        if let bridge = row["boundary_bridge"] as? [String:Any], let quality = bridge["rx_corrected_source"] as? [String:Any] {
            snapshot.applyRXSourceQuality(quality,sourceFresh:snapshot.utc != nil)
        }
    }
    snapshot.applyChronyClock()
    print(InstrumentFace.render(snapshot,width:width,height:height,details:page == .diagnostics,help:page == .help,chronyPage:page == .chrony))
}

func instrumentViewTests() -> Int {
    var n = 0
    func check(_ value: Bool,_ name: String) { if !value { fputs("Instrument test failed: \(name)\n",stderr); exit(1) }; n += 1 }
    var rx = InstrumentSnapshot()
    let quality: [String:Any] = ["available":true,"n":3522,"rmse_ns":257.0,"mean_ns":64.0,"sigma_ns":249.0,"maximum_absolute_ns":713.0]
    rx.applyRXSourceQuality(quality,sourceFresh:true)
    check(rx.rmsTitle=="RX USB RMS" && rx.rmsNS==257 && rx.valid==3522 && rx.jitterNS==249,"RX readout displays causal RMS and centered jitter separately")
    rx.applyRXSourceQuality(quality,sourceFresh:false)
    check(rx.rmsNS==nil && rx.rmsCaption?.contains("pre-chrony")==true,"expired source cannot present historical RX RMS as current")
    var s = InstrumentSnapshot(); s.source = "TC-02"; s.utc = SourceInstant(seconds:1_790_902_000,fraction:123_000_000); s.local = s.utc!.adding(0.04); s.rmsNS = 42_500_000; s.valid = 590; s.missing = 10
    s.ntpEnabled = true; s.ntpFresh = true; s.ntpOffsetNS = -9_000_000; s.ntpUncertaintyNS = 96_000_000
    s.usbEnabled = true; s.usbRunning = true
    for (width,height) in [(120,32),(80,24),(60,24),(40,16)] {
        let render = InstrumentFace.render(s,width:width,height:height,ansi:false), lines = render.split(separator:"\n",omittingEmptySubsequences:false)
        check(lines.count == height-1 && lines.allSatisfy {$0.count < width},"fits \(width)x\(height)")
        check(render.contains("RMS") && render.contains("q Finish"),"readings and exit remain visible")
    }
    let main = InstrumentFace.render(s,width:100,height:30,ansi:false)
    check(main.contains("RUN RMS  [ms]") && main.contains("590 observations") && main.contains("UTC GATED"),"large RMS population and USB gate")
    check(!main.contains("auth/malformed") && !main.contains("CAUSAL"),"transport internals stay off main face")
    s.utc = nil
    check(InstrumentFace.render(s,width:100,height:30,ansi:false).contains("UTC withheld"),"expired source never leaves a frozen valid clock")
    check(InstrumentFace.render(s,width:100,height:30,help:true,ansi:false).contains("Run statistics remain cumulative"),"controls describe reset scope")
    check(InstrumentFace.render(s,width:80,height:24,help:true,ansi:false).contains("served shadow − fitted source time"),"map phase direction is explained at default size")
    s.usbRunning = false;s.usbError = "USB helper missing; rebuild the CLI. Retrying."
    let failure = InstrumentFace.render(s,width:80,height:24,ansi:false)
    check(failure.contains("USB ERROR  USB helper missing; rebuild the CLI. Retrying."),"USB failure and recovery appear on the main face")
    check(failure.contains("USB input unavailable"),"transport failure is distinct from source permission loss")
    return n
}
