import Foundation

/// Export the actual served-vs-local observations, including unavailable rows.
/// Never substitute a fit residual when a served clock was unavailable.
func exportClockTrial(_ path: String, destination: String?) throws {
    let input = URL(fileURLWithPath:path)
    let data = try Data(contentsOf:input)
    guard data.count <= 256*1024*1024, data.last == 10, let text = String(data:data,encoding:.utf8) else { throw NSError(domain:"Incomplete or oversized trial log",code:1) }
    var rows: [[String:Any]] = [], checks: [[String:Any]] = [], start: UInt64?, end: [String:Any]?
    var chronyEvents: [[String:Any]] = [], deviceEvents: [[String:Any]] = []
    let boundary = USBFrameBridge()
    var boundaryCSV = "reference_elapsed_s,hardware_segment,source_scope,hardware_phase_elapsed_ns,source_phase_elapsed_ns,counter_phase_ns,source_mono_phase_ns,reported_UTC_phase_ns,UTC_offset_phase_ns,frame_identity,source_valid\n"
    var boundaryPoints: [(Double,Double,Double,Double?,Double,Int,String)] = []
    var counterGroups: [Int:PhaseSeries] = [:], sourceGroups: [String:PhaseSeries] = [:]
    var usbEnd: [String:Any]?, hasStart = false, transport = "BLE"
    for line in text.split(separator:"\n") {
        guard line.utf8.count <= 1_048_576, let row = try JSONSerialization.jsonObject(with:Data(line.utf8)) as? [String:Any] else { throw NSError(domain:"Invalid trial JSONL",code:1) }
        if row["ignored_after_stop"] as? Bool != true, row["identity_verified"] as? Bool != false, row["type"] as? String == "usb_raw", let h = row["helper"] as? [String:Any] {
            if h["event"] as? String == "manifest" {boundary.configure(h)}
            if h["event"] as? String == "sample" {
                let before = boundary.accepted;boundary.ingest(h)
                if boundary.accepted > before, let phase = boundary.lastCounterPhase {
                    let rawContinuous = Double(h["continuous_before"] as? String ?? "") ?? 0
                    let machNanoseconds = rawContinuous*Double(clockScale.numer)/Double(clockScale.denom)
                    let elapsed = (machNanoseconds-Double(start ?? 0))/1e9
                    let sourceElapsed = boundary.lastElapsed-boundary.sourceElapsed0
                    boundaryPoints.append((elapsed,boundary.lastElapsed,phase,boundary.lastPhase,sourceElapsed,boundary.segments,boundary.sourceScope))
                    counterGroups[boundary.segments] = boundary.counter
                    if boundary.lastPhase != nil {sourceGroups[boundary.sourceScope] = boundary.source}
                    func f(_ x: Double?) -> String {x.map {String($0)} ?? ""}
                    boundaryCSV += [String(elapsed),String(boundary.segments),boundary.sourceScope,String(boundary.lastElapsed),String(sourceElapsed),String(phase),f(boundary.lastPhase),f(boundary.lastUTCPhase),f(boundary.lastOffsetPhase),boundary.identity,boundary.lastPhase == nil ? "false" : "true"].joined(separator:",")+"\n"
                }
            }
        }
        switch row["type"] as? String {
        case "ucs1_start", "usb_start": hasStart = true; if row["type"] as? String == "usb_start" {transport = row["transport"] as? String == "CHRONY" ? "CHRONY" : "USB"}; start = (row["tick_ns"] as? String).flatMap {UInt64($0)}
        case "ucs1_status", "usb_status": rows.append(row)
        case "ntp_check": checks.append(row)
        case "chrony_tracking", "chrony_refclock_sample": chronyEvents.append(row)
        case "ucs1_end", "usb_end": end = row
        case "usb_observer_end": usbEnd = row
        default:
            if let type = row["type"] as? String {
                if type.hasPrefix("chrony_service_") {chronyEvents.append(row)}
                if ["usb_device_selected","usb_source_rotation","usb_identity_rejection"].contains(type) {deviceEvents.append(row)}
            }
        }
    }
    guard hasStart, let origin = start, !rows.isEmpty else { throw NSError(domain:"No clock trial observations",code:1) }
    let out = destination.map {URL(fileURLWithPath:$0)} ?? input.deletingPathExtension().appendingPathExtension("report")
    guard !FileManager.default.fileExists(atPath:out.path) else { throw NSError(domain:"Report directory exists; choose a fresh --report-out",code:1) }
    var comparison = LocalClockComparison(), chronyComparison = LocalClockComparison(), points: [(Double,Double?)] = []
    var chronyCSV = "elapsed_s,chrony_user_utc_ns,chrony_minus_local_ns,refid,stratum,ntp_minus_system_s,chrony_rms_offset_s,root_delay_s,root_dispersion_s,fresh,precision_qualified\n"
    var intervals: [(Double,Double?,Double?)] = []
    var csv = "elapsed_s,source_scope,served_valid,served_minus_local_ns,source_permission_remaining_ns,source_age_us,causal_callback_rms_ns,ntp_server_minus_local_ns,ntp_uncertainty_ns,ntp_fresh,usb_raw_valid,usb_raw_unavailable,reported_device_utc_ns,root,source_epoch,map_generation,utc_offset_low_ns,utc_offset_high_ns,utc_rms_lower_ns,utc_rms_upper_ns,midpoint_jitter_includes_usb_ns\n"
    for row in rows {
        guard let wall = try MacWallSample.decode(row), wall.tick >= origin else { continue }
        let served = (row["shadow_ns"] as? String).flatMap {Int64($0)}.map {SourceInstant(ns:$0)}
        let scope = row["scope"] as? String ?? ""
        comparison.observe(wall:wall,served:served,scope:scope)
        let elapsed = Double(wall.tick-origin)/1e9, error = served.map {$0.distance(from:wall.instant)*1e9}
        if let chrony = row["chrony"] as? [String:Any], chrony["enabled"] as? Bool == true {
            let utc = (row["chrony_user_utc_ns"] as? String).flatMap {Int64($0)}.map {SourceInstant(ns:$0)}
            let tracking = chrony["tracking"] as? [String:Any]
            chronyComparison.observe(wall:wall,served:utc,scope:tracking?["refid"] as? String ?? "")
            func value(_ x: Any?) -> String {x.map {$0 is NSNull ? "" : String(describing:$0)} ?? ""}
            chronyCSV += [String(elapsed),value(utc.map {String($0.ns)}),value(utc.map {$0.distance(from:wall.instant)*1e9}),
                value(tracking?["refid"]),value(tracking?["stratum"]),value(tracking?["ntp_minus_system_s"]),
                value(tracking?["rms_offset_s"]),value(tracking?["root_delay_s"]),value(tracking?["root_dispersion_s"]),
                value(chrony["fresh"]),"false"].joined(separator:",")+"\n"
        }
        points.append((elapsed,error))
        let ntp = row["ntp_check"] as? [String:Any], latest = ntp?["latest"] as? [String:Any]
        let usb = row["usb_observation"] as? [String:Any], causal = row["causal_callback_diagnostics"] as? [String:Any]
        func field(_ value: Any?) -> String { value.map { $0 is NSNull ? "" : String(describing:$0) } ?? "" }
        let source = row["source_snapshot"] as? [String:Any]
        let bounds = row["periodic_utc_offset_intervals"] as? [String:Any]
        func number(_ value: Any?) -> Double? { if let s = value as? String {return Double(s)}; return value as? Double }
        intervals.append((elapsed,row["reported_device_utc_ns"] is String ? number(bounds?["latest_low_ns"]) : nil,row["reported_device_utc_ns"] is String ? number(bounds?["latest_high_ns"]) : nil))
        let columns = [String(format:"%.6f",elapsed),"\""+scope.replacingOccurrences(of:"\"",with:"\"\"")+"\"",served == nil ? "false" : "true",field(error),field(row["source_permission_remaining_ns"]),field(row["source_age_us"]),field(causal?["overall_rms_ns"]),field(latest?["server_minus_local_ns"]),field(latest?["reported_uncertainty_ns"]),field(ntp?["fresh"]),field(usb?["raw_valid"]),field(usb?["raw_unavailable"]),field(row["reported_device_utc_ns"]),field(source?["root"]),field(source?["source_epoch"]),field(source?["map_generation"]),field(bounds?["latest_low_ns"]),field(bounds?["latest_high_ns"]),field(bounds?["rms_lower_ns"]),field(bounds?["rms_upper_ns"]),field(bounds?["midpoint_jitter_diagnostic_ns"])]
        csv += columns.joined(separator:",")+"\n"
    }
    let finalTick = (end?["tick_ns"] as? String).flatMap {UInt64($0)} ?? comparison.last ?? origin
    let summary: [String:Any] = ["schema":1,"input":input.lastPathComponent,"capture_complete":end != nil,
        "elapsed_s":Double(finalTick-origin)/1e9,"comparison":comparison.json(finalTick),"ntp_checks":checks,
        "usb_device_events":deviceEvents,"usb_observer_end":usbEnd as Any? ?? NSNull(),"capture_end":end as Any? ?? NSNull(),
        "accuracy_qualified":false,"reference":"local macOS wall; read-only NTP checks retained separately",
        "chrony_user_clock":["comparison":chronyComparison.json(finalTick),"events":chronyEvents,
            "definition":"experimental chronyd user UTC minus local wall, separate from captured USB and precision shadow; same-source comparison is not independent accuracy"],
        "periodic_utc_offset_intervals":end?["periodic_utc_offset_intervals"] as Any? ?? rows.last?["periodic_utc_offset_intervals"] as Any? ?? NSNull(),
        "boundary_bridge_reprocessed":boundary.json,"boundary_counter_segments":counterGroups.mapValues {$0.json}.reduce(into:[String:Any]()) {$0[String($1.key)] = $1.value},"boundary_source_segments":sourceGroups.mapValues {$0.json},
        "transport":transport,"usb_utc":transport == "USB" ? "Source UTC association unqualified; boundary identity and mono stability reported separately" : "Historical BLE comparison with raw USB only",
        "limitations":["Local-vs-NTP intervals do not establish independent UTC accuracy",transport == "USB" ? "Captured device UTC is evidence at its counter tick; no arrival-time shadow" : "Historical BLE UTC uses software two-way timestamps","Status gaps are point-sampled; every unavailable status is retained","No system clock adjustment or local-wall feedback"]]
    func escaped(_ value: String) -> String { value.replacingOccurrences(of:"&",with:"&amp;").replacingOccurrences(of:"<",with:"&lt;").replacingOccurrences(of:">",with:"&gt;").replacingOccurrences(of:"\"",with:"&quot;") }
    let values = transport == "USB" ? intervals.flatMap {[$0.1,$0.2].compactMap {$0}.map {$0/1e6}} : points.compactMap {$0.1}.map {$0/1e6}
    let lo = min(0,values.min() ?? -1), hi = max(0,values.max() ?? 1), pad = max(0.01,(hi-lo)*0.1)
    let low = lo-pad, high = hi+pad, span = max(1,points.last?.0 ?? 1)
    func x(_ t: Double) -> Double { 65+t/span*1040 }
    func y(_ ms: Double) -> Double { 300-(ms-low)/(high-low)*260 }
    var graph = "", segment = ""
    func flush() { if !segment.isEmpty { graph += "<polyline fill='none' stroke='#126c9c' stroke-width='1.7' points='\(segment)'/>"; segment = "" } }
    for (t,error) in points where transport != "USB" {
        if let error = error {
            segment += String(format:"%.2f,%.2f ",x(t),y(error/1e6))
            graph += String(format:"<circle cx='%.2f' cy='%.2f' r='2' fill='#126c9c'><title>%.1fs: %+.6f ms</title></circle>",x(t),y(error/1e6),t,error/1e6)
        } else { flush(); graph += String(format:"<path d='M%.2f 312v8' stroke='#b13a3a'><title>%.1fs: source unavailable</title></path>",x(t),t) }
    }
    flush()
    if transport == "USB" {
        for (t,lower,upper) in intervals {
            if let lower = lower, let upper = upper {
                graph += String(format:"<path d='M%.2f %.2fV%.2f' stroke='#126c9c' stroke-width='2'><title>%.1fs: [%+.6f, %+.6f] ms</title></path>",x(t),y(lower/1e6),y(upper/1e6),t,lower/1e6,upper/1e6)
            } else { graph += String(format:"<path d='M%.2f 312v8' stroke='#b13a3a'><title>%.1fs: no periodic UTC interval yet</title></path>",x(t),t) }
        }
    }
    let intervalSummary = (end?["periodic_utc_offset_intervals"] ?? rows.last?["periodic_utc_offset_intervals"]) as? [String:Any]
    func metric(_ key: String) -> String { (intervalSummary?[key] as? Double).map {String(format:"%.3f ms",$0/1e6)} ?? "unavailable" }
    let reading = transport == "USB" ? "UTC RMS bounds: \(metric("rms_lower_ns")) … \(metric("rms_upper_ns")) · midpoint jitter: \(metric("midpoint_jitter_diagnostic_ns"))" : comparison.line
    let explanation = transport == "USB" ? "Blue bars: captured current-counter UTC minus the complete local-wall interval around its USB request. These are offset intervals, not served-clock samples. Midpoint jitter includes USB scheduling uncertainty. The precision Mach shadow is gated. A source loss does not extend a prior source permission; raw unavailable packets remain in the capture." : "Blue: served source shadow minus bracketed local wall time. Red: unavailable shadow observations."
    let ntpRows = checks.map { check -> String in
        let result = check["result"] as? [String:Any]
        let offset = result?["server_minus_local_ns"] as? Double, uncertainty = result?["reported_uncertainty_ns"] as? Double
        let at = (check["tick_ns"] as? String).flatMap {UInt64($0)} ?? origin
        return "<tr><td>\(String(format:"%.1f",Double(at-origin)/1e9)) s</td><td>\(escaped(result?["server"] as? String ?? "failed"))</td><td>\(offset.map {String(format:"%+.3f",-$0/1e6)} ?? "—") ms</td><td>± \(uncertainty.map {String(format:"%.3f",$0/1e6)} ?? "—") ms</td><td>\(escaped(check["error"] as? String ?? "observed; absolute accuracy unqualified"))</td></tr>"
    }.joined()
    let phaseValues = boundaryPoints.flatMap { point -> [Double] in
        var values: [Double] = []
        if let fit = counterGroups[point.5] {values.append((point.2-fit.meanY-fit.slope*(point.1-fit.meanX))/1000)}
        if let phase = point.3, let fit = sourceGroups[point.6] {values.append((phase-fit.meanY-fit.slope*(point.4-fit.meanX))/1000)}
        return values
    }
    let phaseExtent = max(1,phaseValues.map {abs($0)}.max() ?? 1)*1.1
    var phaseGraph = "",counterLine = "",sourceLine = "",lastSourceScope = ""
    func py(_ us: Double) -> Double {170-us/phaseExtent*125}
    let stride = max(1,boundaryPoints.count/1500)
    for (index,point) in boundaryPoints.enumerated() where index%stride == 0 {
        if let fit = counterGroups[point.5] {let v = (point.2-fit.meanY-fit.slope*(point.1-fit.meanX))/1000;counterLine += String(format:"%.2f,%.2f ",x(point.0),py(v))}
        if let phase = point.3, let fit = sourceGroups[point.6] {
            if lastSourceScope != point.6 && !sourceLine.isEmpty {phaseGraph += "<polyline fill='none' stroke='#b36e0c' stroke-width='1.6' points='\(sourceLine)'/>";sourceLine = ""}
            let v = (phase-fit.meanY-fit.slope*(point.4-fit.meanX))/1000;sourceLine += String(format:"%.2f,%.2f ",x(point.0),py(v));lastSourceScope = point.6
        } else if !sourceLine.isEmpty {phaseGraph += "<polyline fill='none' stroke='#b36e0c' stroke-width='1.6' points='\(sourceLine)'/>";sourceLine = ""}
    }
    phaseGraph += "<polyline fill='none' stroke='#126c9c' stroke-width='1.5' points='\(counterLine)'/><polyline fill='none' stroke='#b36e0c' stroke-width='1.6' points='\(sourceLine)'/>"
    let boundaryDescription = "Frame mapping: \(boundary.identity). Counter detrended σ \(String(format:"%.3f",boundary.counter.detrendedSigma/1000)) µs; drift \(String(format:"%+.3f",boundary.counter.slope*1e6)) ppm. Source-clock detrended σ \(String(format:"%.3f",boundary.source.detrendedSigma/1000)) µs (\(boundary.source.n) samples in current source scope)."
    let boundaryHTML = transport == "USB" ? """
    <h1>USB relay boundary clock stability</h1><p class="metric">\(escaped(boundaryDescription))</p>
    <svg viewBox="0 0 1140 350" role="img" aria-label="Detrended captured counter and source clock phase against USB/Mach boundaries in microseconds"><path d="M65 40V300H1105" fill="none" stroke="#879ba5"/><path d="M65 170H1105" stroke="#cbd4d9"/><text x="8" y="28">Phase µs</text><text x="4" y="52">+\(String(format:"%.2f",phaseExtent))</text><text x="4" y="300">−\(String(format:"%.2f",phaseExtent))</text><text x="65" y="340">0 s</text><text x="1010" y="340">\(String(format:"%.1f s",span))</text>\(phaseGraph)</svg>
    <p>Blue: captured16MHz counter. Amber: source monotonic clock, with its separately reported UTC offset removed. Both use hardware SOF counts and Apple's microframe reference; request arrival and midpoint are excluded. Linear trend is removed only in this post-run plot and sigma, while drift is retained separately. Every phase sample is in <a href="boundary-phase.csv">boundary-phase.csv</a>; full scopes and gates are in <a href="summary.json">summary.json</a>. Missing source UTC never becomes a zero-error sample. Timestamp bias and absolute UTC accuracy remain unqualified.</p>
    <h2>Coarse UTC versus local macOS time</h2>
    """ : "<h1>Source shadow versus local macOS time</h1>"
    let chronyHTML = chronyEvents.isEmpty ? "" : "<h2>Chrony user clock</h2><p>\(escaped(chronyComparison.line)). These are comparisons with the local wall clock, not independent UTC accuracy. Chrony's own RMS offset is a separate estimator statistic. <a href='chrony-user-clock.csv'>User UTC and offsets</a>; <a href='chrony-events.jsonl'>raw tracking and refclock outcomes</a>. Source gaps and resets are retained.</p>"
    let html = """
    <!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>PToB clock trial</title>
    <style>body{font:16px system-ui;max-width:1150px;margin:40px auto;padding:0 24px;color:#20323b;background:#f6f8fa}h1{font-size:30px}p{line-height:1.5}svg{width:100%;background:white;border:1px solid #cbd4d9}text{font:13px system-ui;fill:#334852}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}td,th{padding:10px;border-bottom:1px solid #d0d8de;text-align:left}.metric{font-size:21px;font-weight:600}.note{color:#614421}a{color:#126c9c}</style>
    \(boundaryHTML)<p class="metric">\(escaped(reading))</p>
    <p>\(String(format:"%.1f",Double(finalTick-origin)/1e9)) seconds · \(end != nil ? "capture closed" : "INCOMPLETE capture") · \(comparison.scopeChanges) source scope changes · \(comparison.wallSteps) local wall step observations.</p>
    <svg viewBox="0 0 1140 350" role="img" aria-label="Source versus local offset intervals in milliseconds; red marks indicate unavailable observations">
    <path d="M65 40V300H1105" fill="none" stroke="#879ba5"/><path d="M65 \(y(0))H1105" stroke="#cbd4d9" stroke-dasharray="4 4"/>
    <text x="8" y="28">Offset ms</text><text x="10" y="50">\(String(format:"%.3f",high))</text><text x="10" y="300">\(String(format:"%.3f",low))</text><text x="65" y="340">0 s</text><text x="1010" y="340">\(String(format:"%.1f s",span))</text>\(graph)</svg>
    <p>\(explanation) Hover on a sample for its value. All samples and missing intervals are retained in <a href="samples.csv">samples.csv</a> and <a href="summary.json">summary.json</a>.</p>
    \(chronyHTML)<h2>Local clock NTP checks</h2><table><thead><tr><th>Elapsed</th><th>Server</th><th>Local − NTP</th><th>Reported uncertainty</th><th>Result</th></tr></thead><tbody>\(ntpRows)</tbody></table>
    <p class="note">Accuracy remains unqualified. Internet NTP uncertainty and path asymmetry may exceed 100 µs. NTP never trains the source shadow. USB boundary mapping and source mono stability are reported separately. Source UTC offset noise and unknown timestamp bias still prevent absolute accuracy qualification. No macOS clock settings were changed.</p></html>
    """
    try FileManager.default.createDirectory(at:out,withIntermediateDirectories:true)
    try JSONSerialization.data(withJSONObject:summary,options:[.prettyPrinted,.sortedKeys]).write(to:out.appendingPathComponent("summary.json"))
    try boundaryCSV.write(to:out.appendingPathComponent("boundary-phase.csv"),atomically:true,encoding:.utf8)
    try csv.write(to:out.appendingPathComponent("samples.csv"),atomically:true,encoding:.utf8)
    try chronyCSV.write(to:out.appendingPathComponent("chrony-user-clock.csv"),atomically:true,encoding:.utf8)
    let chronyLines = try chronyEvents.map {String(data:try JSONSerialization.data(withJSONObject:$0,options:[.sortedKeys]),encoding:.utf8)!}.joined(separator:"\n")
    try (chronyLines.isEmpty ? "" : chronyLines+"\n").write(to:out.appendingPathComponent("chrony-events.jsonl"),atomically:true,encoding:.utf8)
    try html.write(to:out.appendingPathComponent("report.html"),atomically:true,encoding:.utf8)
    print((transport == "USB" ? boundaryDescription+"\n" : "")+reading+"\nReport: "+out.appendingPathComponent("report.html").path)
}
