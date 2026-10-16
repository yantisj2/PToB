import Foundation

/// Retained capture verification uses the shipping decoder and map arithmetic.
/// It opens no device/daemon and does not invent physical truth between tuples.
func replayPrecisionCapture(_ path: String) throws -> [String:Any] {
    let file = try FileHandle(forReadingFrom:URL(fileURLWithPath:path));defer {try? file.close()}
    let bridge = USBFrameBridge()
    var buffer = Data(), lines = 0, records = 0, valid = 0, unavailable = 0, invalidMaps = 0
    var forecastErrors = PhaseSeries(), forecastSkipped = 0, rejectedForecasts = 0
    func consume(_ bytes: Data) throws {
        guard !bytes.isEmpty else {return};lines += 1
        guard let row = try JSONSerialization.jsonObject(with:bytes) as? [String:Any],
              row["type"] as? String == "usb_raw", row["ignored_after_stop"] as? Bool != true,
              let h = row["helper"] as? [String:Any],
              row["identity_verified"] as? Bool != false else {return}
        if h["event"] as? String == "manifest" {bridge.configure(h);return}
        guard h["event"] as? String == "sample" else {return}
        var forecast: Double?
        // Freeze and evaluate before ingesting the next hardware observation.
        if let packet = h["precision_source"] as? [String:Any],let r = USBSourceRecord(packet),
           let frame = packet["frame"] as? Int, let last = bridge.lastFrame,
           let ref = (h["reference_absolute"] as? String).flatMap(UInt64.init), ref <= UInt64(Int64.max),
           let microframe = (h["reference_microframe"] as? String).flatMap(UInt64.init), microframe < UInt64(Int64.max/1_000_000),
           let offset = bridge.frameOffset,
           bridge.scope == "\(r.silicon):\(r.boot):\(r.usbEpoch):\(r.captureEpoch)" {
            let frames = (frame-last+2048)%2048
            let ordinal = bridge.ordinal+Int64(frames)
            let displacement = (offset+ordinal*8-Int64(microframe))*125000
            let units = displacement*Int64(clockScale.denom)/Int64(clockScale.numer)
            let absolute = Int64(ref).addingReportingOverflow(units)
            if frames > 0 && frames <= 1000, h["reference_valid"] as? Bool == true,
               h["absolute_continuous_intervals_overlap"] as? Bool == true,
               !absolute.overflow, absolute.partialValue >= 0,
               let prediction = bridge.counter(atMachAbsolute:UInt64(absolute.partialValue)) {
                let ticks = r.counter >= prediction.whole ? Double(r.counter-prediction.whole) : -Double(prediction.whole-r.counter)
                forecast = (ticks-prediction.fraction)*62.5
            } else {forecastSkipped += 1}
        } else {forecastSkipped += 1}
        let acceptedBefore = bridge.accepted
        bridge.ingest(h)
        if let error = forecast {
            if bridge.accepted > acceptedBefore {forecastErrors.add(Double(forecastErrors.n),error)}
            else {rejectedForecasts += 1}
        }
        for key in ["precision_source","device_source"] {
            guard let raw = h[key] as? [String:Any], let r = USBSourceRecord(raw) else {continue}
            records += 1
            if r.utc == nil {unavailable += 1;continue}
            valid += 1;if USBImmutableSourceMap(r) == nil {invalidMaps += 1}
        }
    }
    while let data = try file.read(upToCount:65536), !data.isEmpty {
        buffer.append(data)
        while let newline = buffer.firstIndex(of:10) {try consume(Data(buffer[..<newline]));buffer.removeSubrange(...newline)}
        guard buffer.count <= 1_048_576 else {throw NSError(domain:"Capture row exceeds 1 MiB",code:1)}
    }
    if !buffer.isEmpty {try consume(buffer)}
    return ["schema":1,"type":"precision_capture_replay","input":URL(fileURLWithPath:path).standardized.path,
        "lines":lines,"decoded_source_records":records,"valid_source_records":valid,"unavailable_source_records":unavailable,
        "source_map_mismatches":invalidMaps,"qualified_previous_fit_counter_errors":["n":forecastErrors.n,
            "mean_ns":forecastErrors.n > 0 ? forecastErrors.meanY as Any : NSNull(),
            "rmse_ns":forecastErrors.n > 0 ? forecastErrors.rmse as Any : NSNull(),
            "sigma_ns":forecastErrors.n > 0 ? forecastErrors.rawSigma as Any : NSNull(),
            "minimum_ns":forecastErrors.n > 0 ? forecastErrors.minimum as Any : NSNull(),
            "maximum_ns":forecastErrors.n > 0 ? forecastErrors.maximum as Any : NSNull(),"detrended":false],
        "forecasts_rejected_by_existing_hardware_validation":rejectedForecasts,
        "forecast_skipped_or_unqualified":forecastSkipped,"boundary_bridge":bridge.json,
        "physical_UTC_qualified":false,"between_update_physical_truth_available":false,
        "method":"shipping immutable-map arithmetic and pre-ingest inverse hardware fit; every finite forecast error retained; identity/topology qualification required",
        "limitation":"accepted observed tuples only; no independent UTC truth, intersample truth or source coverage inferred from hardware coverage"]
}
