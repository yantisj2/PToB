import Foundation

/// A read-only presentation copy. Last successful tracking is explicitly
/// historical when current permission/freshness gates withhold user UTC.
struct ChronyDisplaySnapshot {
    var enabled = false, fresh = false, historical = false, feedEnabled = false, feedReady = false
    var outageWarning: Bool?
    var connected = false, synchronized = false, serviceState: String?, usbRelay: String?
    var reference: String?, refid: String?, stratum: Int?, utc: SourceInstant?
    var correctionNS: Double?, lastOffsetNS: Double?, estimatorRMSNS: Double?
    var frequencyPPM: Double?, residualPPM: Double?, skewPPM: Double?
    var rootDelayNS: Double?, dispersionNS: Double?, rootDistanceNS: Double?
    var referenceAgeS: Double?, queryAgeS: Double?, querySpanNS: Double?, updateS: Double?, feedAgeS: Double?
    var checks = 0, failures = 0, sent = 0, rejected = 0, feedFailures = 0, resets = 0
    var leap: String?, error: String?, feedError: String?, socket: String?
    var inputMethod: String?
    var localRMSNS: Double?, localBiasNS: Double?, localJitterNS: Double?, localMaximumNS: Double?
    var localSamples = 0, localGaps = 0
    var state: String {
        if !enabled {return "NOT CONNECTED"}
        if feedError != nil {return "FEED ERROR"}
        if error != nil {return "CHECK FAILED"}
        if fresh {return "TRACKING"}
        if serviceState == "STARTING" {return "STARTING"}
        if connected && !synchronized {return "WAITING FOR SOURCE"}
        return refid == nil ? "ACQUIRING" : "UTC WITHHELD"
    }
    var selected: String {
        let name = reference.flatMap {$0.isEmpty ? nil : $0} ?? refid ?? "pending"
        return String(name.prefix(18))+(stratum.map {" / S\($0)"} ?? "")
    }
    static func read(_ value: [String:Any], feeder: [String:Any], comparison: [String:Any], now: UInt64) -> ChronyDisplaySnapshot {
        var s = ChronyDisplaySnapshot()
        s.enabled = value["enabled"] as? Bool ?? false;s.fresh = value["fresh"] as? Bool ?? false
        let tracking = value["tracking"] as? [String:Any]
        s.connected = value["connected"] as? Bool ?? (tracking != nil)
        s.synchronized = value["synchronized"] as? Bool ?? (tracking != nil)
        s.usbRelay = (value["selected_usb_serial"] as? String).map {usbDeviceLabel($0)}
        s.serviceState = (value["service"] as? [String:Any])?["state"] as? String
        let last = tracking ?? value["last_tracking"] as? [String:Any]
        s.historical = last != nil && !s.fresh
        s.reference = last?["reference_name"] as? String;s.refid = last?["refid"] as? String
        if s.reference == nil, s.refid == "424C4500" {s.reference = "BLE"}
        s.stratum = last?["stratum"] as? Int
        s.utc = (value["user_utc_ns"] as? String).flatMap {Int64($0)}.map {SourceInstant(ns:$0)}
        func ns(_ key: String) -> Double? {(last?[key] as? Double).map {$0*1e9}}
        s.correctionNS = ns("ntp_minus_system_s");s.lastOffsetNS = ns("last_offset_s");s.estimatorRMSNS = ns("rms_offset_s")
        s.frequencyPPM = last?["frequency_ppm_fast"] as? Double;s.residualPPM = last?["residual_frequency_ppm"] as? Double
        s.skewPPM = last?["skew_ppm"] as? Double;s.rootDelayNS = ns("root_delay_s");s.dispersionNS = ns("root_dispersion_s")
        if let delay = s.rootDelayNS, let dispersion = s.dispersionNS {s.rootDistanceNS = delay/2+dispersion}
        s.referenceAgeS = value["reference_age_s"] as? Double;s.querySpanNS = (value["query_span_ns"] as? String).flatMap {Double($0)}
        s.queryAgeS = (value["query_age_ns"] as? String).flatMap {Double($0)}.map {$0/1e9}
        s.updateS = last?["update_interval_s"] as? Double;s.leap = value["leap"] as? String ?? last?["leap"] as? String
        s.checks = value["checks"] as? Int ?? 0;s.failures = value["failures"] as? Int ?? 0
        s.error = value["error"] as? String;s.socket = value["command_socket"] as? String
        s.feedEnabled = feeder["enabled"] as? Bool ?? false;s.feedReady = feeder["ready"] as? Bool ?? false
        s.sent = feeder["sent"] as? Int ?? 0;s.rejected = feeder["rejected"] as? Int ?? 0
        s.feedFailures = feeder["failures"] as? Int ?? 0;s.resets = feeder["resets"] as? Int ?? 0;s.feedError = feeder["error"] as? String
        s.inputMethod = feeder["input_method"] as? String
        if let at = (feeder["last_sample_tick_ns"] as? String).flatMap({UInt64($0)}), now >= at {s.feedAgeS = Double(now-at)/1e9}
        s.localRMSNS = comparison["rms_ns"] as? Double;s.localSamples = comparison["valid_status_samples"] as? Int ?? 0
        s.localBiasNS = comparison["mean_ns"] as? Double;s.localJitterNS = comparison["jitter_stddev_ns"] as? Double
        s.localMaximumNS = comparison["max_abs_ns"] as? Double
        s.localGaps = comparison["unavailable_status_samples"] as? Int ?? 0
        return s
    }
}

enum ChronyFace {
    static func seconds(_ value: Double?) -> String {value.map {String(format:"%.2f s",$0)} ?? "---"}
    static func ppm(_ value: Double?) -> String {value.map {String(format:"%+.3f ppm",$0)} ?? "---"}
    static func lines(_ s: ChronyDisplaySnapshot, columns: Int) -> [(String,String)] {
        var result: [(String,String)] = []
        func line(_ text: String = "",_ color: String = InstrumentFace.secondary) {result.append((text,color))}
        func pair(_ left: String,_ right: String) -> String {
            let room = max(0,columns-right.count-2), kept = String(left.prefix(room))
            return kept+String(repeating:" ",count:max(2,columns-kept.count-right.count))+right
        }
        func measure(_ value: Double?, signed: Bool = false) -> String {InstrumentFace.measurement(value,signed:signed)}
        line(pair("CHRONY / STATISTICS",s.state),(s.outageWarning ?? !s.fresh) ? InstrumentFace.warning : InstrumentFace.primary)
        if !s.enabled {
            line();line("Chrony is not connected.",InstrumentFace.primary)
            line("USB measurements continue independently.")
            line();line("Normal launch starts a private user instance.")
            if columns >= 70 {
                line("Restart ptob-cli without --no-chrony.")
                line("Or attach using --chrony-command-socket and --chronyc.")
            } else {line("Restart without --no-chrony.");line("Setup: clients/macos/chrony/README.md")}
            line();line("This page does not start or change a daemon.")
            return result
        }
        let fineEstimate = s.inputMethod == "EXPERIMENTAL_FINE_UTC_ESTIMATE"
        line(s.feedEnabled ? "BLE via "+(s.usbRelay ?? "USB relay")+(fineEstimate ? " → fine estimate → chronyd -x" : " → USB → chronyd -x") : "Attached local instance / read-only monitoring")
        line("USER UTC  "+(s.fresh ? utcReadout(s.utc?.seconds) : "WITHHELD"),(s.outageWarning ?? !s.fresh) ? InstrumentFace.warning : InstrumentFace.primary)
        if columns < 70 {
            line("SOURCE  "+s.selected+(s.historical ? " (last)" : ""))
            line("NTP−SYS  "+measure(s.fresh ? s.correctionNS : nil,signed:true))
            line("EST RMS  "+InstrumentFace.rmsMeasurement(s.estimatorRMSNS)+(s.historical ? " (last)" : ""))
            line("FREQ  "+ppm(s.frequencyPPM)+(s.historical ? " (last)" : ""))
            line("REF AGE "+seconds(s.referenceAgeS)+" / QUERY "+seconds(s.queryAgeS))
            line(s.feedEnabled ? "FEED sent \(s.sent) / errors \(s.feedFailures)" : "CHECKS \(s.checks) / failed \(s.failures)")
            line(s.error != nil || s.feedError != nil ? "Latest check/feed failed; d for details." : "UTC accuracy remains unqualified.",InstrumentFace.warning)
            line("80×24 shows the full statistics.")
            return result
        }
        line(pair("SOURCE  "+s.selected,"REFID  "+(s.refid ?? "---")))
        line(s.historical ? "LAST SUCCESSFUL SNAPSHOT / CURRENT UTC WITHHELD" : "CURRENT TRACKING SNAPSHOT",s.historical ? InstrumentFace.warning : InstrumentFace.secondary)
        line(pair("NTP−SYS  "+measure(s.correctionNS,signed:true),"REF AGE  "+seconds(s.referenceAgeS)))
        line(pair("LAST OFFSET  "+measure(s.lastOffsetNS,signed:true),"EST RMS  "+InstrumentFace.rmsMeasurement(s.estimatorRMSNS)))
        line(pair("FREQ  "+ppm(s.frequencyPPM)+" fast","RESIDUAL  "+ppm(s.residualPPM)))
        line(pair("SKEW  "+ppm(s.skewPPM),"UPDATE  "+seconds(s.updateS)))
        line(pair("ROOT DELAY  "+measure(s.rootDelayNS),"DISPERSION  "+measure(s.dispersionNS)))
        line(pair("EST ROOT DIST  "+measure(s.rootDistanceNS),"QUERY  "+measure(s.querySpanNS)))
        line(pair("CHECKS \(s.checks) / FAILED \(s.failures)","QUERY AGE  "+seconds(s.queryAgeS)))
        line(pair("LEAP  "+(s.leap ?? "unknown"),"vs LOCAL RMS  "+InstrumentFace.rmsMeasurement(s.localRMSNS)))
        line(String(repeating:"─",count:columns))
        if s.feedEnabled {
            line(pair((fineEstimate ? "FINE ESTIMATE / " : "REFCLOCK BLE / ")+(s.feedReady ? "READY" : "WAITING"),"LAST SAMPLE  "+seconds(s.feedAgeS)))
            line(pair("SENT \(s.sent) / REJECTED \(s.rejected)","ERRORS \(s.feedFailures) / RESETS \(s.resets)"))
        } else {line("REFCLOCK FEED  not configured; attached monitor only.");line("LOCAL COMPARISON  \(s.localSamples) observations / \(s.localGaps) gaps")}
        if s.error != nil || s.feedError != nil {line("Latest check/feed failed; d shows the full diagnostic.",InstrumentFace.warning)}
        else {line("Root distance and RMS are estimator statistics.")}
        line("UTC accuracy unqualified / macOS clock settings unchanged",InstrumentFace.warning)
        return result
    }
}
