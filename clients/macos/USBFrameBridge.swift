import Foundation

/// Phase measurements use hardware SOFs and Apple's frame reference exclusively.
/// Request brackets constrain integer identity; they never supply phase samples.
struct PhaseSeries {
    var n = 0, meanX = 0.0, meanY = 0.0, xx = 0.0, yy = 0.0, xy = 0.0
    var minimum = Double.infinity, maximum = -Double.infinity
    mutating func add(_ x: Double,_ y: Double) {
        n += 1; let dx = x-meanX, dy = y-meanY
        meanX += dx/Double(n); meanY += dy/Double(n)
        xx += dx*(x-meanX); yy += dy*(y-meanY); xy += dx*(y-meanY)
        minimum = min(minimum,y); maximum = max(maximum,y)
    }
    var slope: Double {xx > 0 ? xy/xx : 0}
    var rawSigma: Double {n > 1 ? sqrt(max(0,yy/Double(n))) : 0}
    var rmse: Double {n > 0 ? sqrt(max(0,yy/Double(n)+meanY*meanY)) : 0}
    var detrendedSigma: Double {n > 2 && xx > 0 ? sqrt(max(0,(yy-xy*xy/xx)/Double(n))) : 0}
    var json: [String:Any] {["n":n,"mean_ns":n > 0 ? meanY as Any : NSNull(),"rmse_ns":n > 0 ? rmse as Any : NSNull(),"raw_phase_stddev_ns":n > 1 ? rawSigma as Any : NSNull(),"detrended_phase_stddev_ns":n > 2 ? detrendedSigma as Any : NSNull(),"linear_trend_ppm":slope*1e6,"phase_range_ns":n > 0 ? maximum-minimum as Any : NSNull(),"method":"online population phase stddev; retrospective linear detrend; drift retained separately"]}
}

final class USBFrameBridge {
    var source = PhaseSeries(), counter = PhaseSeries(), utc = PhaseSeries(), offset = PhaseSeries()
    var directFullSpeedXHCI = false, layoutChecks = 0, layoutFailures = 0
    var sourceScope = "", sourceElapsed0 = 0.0
    var accepted = 0, rejected = 0, segments = 0, anchors = 0, anchorRejected = 0
    var counterWindow: [(Double,Double)] = [], counterWindowFit = PhaseSeries(), counterInnovations = PhaseSeries()
    var lastCounterInnovation: Double?
    var lastAnchorSequence: UInt32 = 0
    var consecutiveFrameFailures = 0
    var ordinal: Int64 = 0, scope = "", lastFrame: Int?, lastCounter: UInt64?, lastSequence: UInt32 = 0
    var firstCounter: UInt64 = 0, firstMono: Int64 = 0, firstUTC: Int64 = 0, firstOffset: Int64 = 0
    var firstReference: UInt64 = 0, firstMicroframe: UInt64 = 0, lastReference: UInt64 = 0, lastMicroframe: UInt64 = 0
    var lowerIdentity = -Double.infinity, upperIdentity = Double.infinity
    var lastPhase: Double?, lastCounterPhase: Double?, lastUTCPhase: Double?, lastOffsetPhase: Double?, lastElapsed = 0.0, lastAge = 0.0
    var frameOffset: Int64? {
        guard lowerIdentity.isFinite, upperIdentity.isFinite else {return nil}
        let lo = ceil(lowerIdentity/1_000_000), hi = floor(upperIdentity/1_000_000)
        return lo == hi && abs(lo) < Double(Int64.max/8) ? Int64(lo)*8 : nil
    }
    var identity: String {
        guard anchors > 0 else {return "WAITING_FOR_CAUSAL_MARKERS"}
        guard directFullSpeedXHCI else {return "TOPOLOGY_NOT_QUALIFIED"}
        guard layoutFailures == 0, layoutChecks >= 8 else {return "WAITING_FOR_HOST_FRAME_LAYOUT"}
        guard anchors >= 16 else {return "COLLECTING_CAUSAL_MARKERS"}
        return frameOffset != nil ? "FRAME_TO_MACH_CAUSALLY_IDENTIFIED" : "FRAME_IDENTITY_AMBIGUOUS"
    }
    func configure(_ manifest: [String:Any]) {
        directFullSpeedXHCI = false
        guard let topology = manifest["topology"] as? [[String:Any]],topology.count >= 4 else {return}
        directFullSpeedXHCI = topology[1]["speed"] as? Int == 1 && topology[1]["registry_class"] as? String == "IOUSBHostDevice" &&
            (topology[2]["registry_class"] as? String ?? "").contains("XHCI") && (topology[2]["registry_class"] as? String ?? "").contains("Port") &&
            (topology[3]["registry_class"] as? String ?? "").contains("XHCI")
    }
    func reset() {scope = "";lastFrame = nil;lastCounter = nil;lastSequence = 0;lowerIdentity = -.infinity;upperIdentity = .infinity;anchors = 0;lastAnchorSequence = 0;consecutiveFrameFailures = 0;layoutChecks = 0;layoutFailures = 0;ordinal = 0;source = PhaseSeries();counter = PhaseSeries();utc = PhaseSeries();offset = PhaseSeries();lastPhase = nil;sourceScope = "";counterWindow.removeAll(keepingCapacity:true);counterWindowFit = PhaseSeries();counterInnovations = PhaseSeries();lastCounterInnovation = nil}
    func ingest(_ h: [String:Any]) {
        func u(_ key: String) -> UInt64? {(h[key] as? String).flatMap {UInt64($0)}}
        guard let packet = h["precision_source"] as? [String:Any], let p = USBSourceRecord(packet), p.kind == 1,
              let frame = packet["frame"] as? Int, frame >= 0, frame < 2048,
              p.current >= p.counter, p.current-p.counter < 16000,
              h["reference_valid"] as? Bool == true, h["absolute_continuous_intervals_overlap"] as? Bool == true,
              let reference = u("reference_absolute"), let microframe = u("reference_microframe") else {rejected += 1;return}
        let newScope = "\(p.silicon):\(p.boot):\(p.usbEpoch):\(p.captureEpoch)"
        if scope != newScope {reset();scope = newScope;segments += 1}
        guard p.sequence > lastSequence else {rejected += 1;return}
        if let old = lastCounter, let f = lastFrame {
            let frames = (frame-f+2048)%2048
            guard p.counter > old, frames > 0, frames <= 1000, p.counter-old <= 16_000_000,
                  abs(Double(p.counter-old)-Double(frames)*16000) < 4000,
                  reference >= lastReference, microframe >= lastMicroframe else {rejected += 1;consecutiveFrameFailures += 1;if consecutiveFrameFailures >= 3 || p.counter > old+16_000_000 {reset()};return}
            consecutiveFrameFailures = 0;ordinal += Int64(frames)
        } else {firstCounter = p.counter;firstReference = reference;firstMicroframe = microframe}
        let referenceDelta = Double(reference-firstReference)*Double(clockScale.numer)/Double(clockScale.denom)
        let hostPhase = referenceDelta-Double(microframe-firstMicroframe)*125000
        let elapsed = Double(ordinal)*1_000_000+hostPhase
        lastCounterPhase = Double(p.counter-firstCounter)*62.5-elapsed
        if counterWindowFit.n >= 64 {
            let predictedPhase = counterWindowFit.meanY+counterWindowFit.slope*(elapsed-counterWindowFit.meanX)
            lastCounterInnovation = lastCounterPhase!-predictedPhase
            counterInnovations.add(elapsed,lastCounterInnovation!)
        }
        counter.add(elapsed,lastCounterPhase!)
        counterWindow.append((elapsed,lastCounterPhase!));if counterWindow.count > 1024 {counterWindow.removeFirst()}
        counterWindowFit = PhaseSeries();for point in counterWindow {counterWindowFit.add(point.0,point.1)}
        lastPhase = nil;lastUTCPhase = nil;lastOffsetPhase = nil
        if let instant = p.utc, let offsetNS = (packet["utc_offset_ns"] as? String).flatMap({Int64($0)}), offsetNS > 0, instant.ns >= offsetNS {
            if sourceScope != p.scope {
                sourceScope = p.scope;source = PhaseSeries();utc = PhaseSeries();offset = PhaseSeries()
                firstMono = instant.ns-offsetNS;firstUTC = instant.ns;firstOffset = offsetNS;sourceElapsed0 = elapsed
            }
            let localElapsed = elapsed-sourceElapsed0
            let phase = Double((instant.ns-offsetNS)-firstMono)-localElapsed
            source.add(localElapsed,phase);lastUTCPhase = Double(instant.ns-firstUTC)-localElapsed;lastOffsetPhase = Double(offsetNS-firstOffset)
            utc.add(localElapsed,lastUTCPhase!);offset.add(localElapsed,lastOffsetPhase!)
            lastPhase = phase
        }
        lastElapsed = elapsed;lastAge = Double(p.current-p.counter)*62.5
        lastFrame = frame;lastCounter = p.counter;lastSequence = p.sequence;lastReference = reference;lastMicroframe = microframe;accepted += 1
        if h["frame_anchor"] is [String:Any], let current = u("current_microframe"), let at = u("current_microframe_absolute"), let frameAPI = u("bus_frame_number"), let frameTime = u("bus_frame_absolute"), frameTime >= at {
            let delta = Double(frameTime-at)*Double(clockScale.numer)/Double(clockScale.denom)-(Double(frameAPI)*8-Double(current))*125000
            if delta >= -50000 && delta <= 1_050_000 {layoutChecks += 1} else {layoutFailures += 1}
        }
        if let a = h["frame_anchor"] as? [String:Any] {anchor(a,h:h,packet:p,frame:frame,reference:reference,microframe:microframe)}
    }
    private func anchor(_ a: [String:Any],h: [String:Any],packet: USBSourceRecord,frame: Int,reference: UInt64,microframe: UInt64) {
        func value(_ d: [String:Any],_ k: String) -> UInt64? {(d[k] as? String).flatMap {UInt64($0)}}
        guard a["valid"] as? Bool == true,
              let sequence = a["sequence"] as? UInt32,sequence > lastAnchorSequence,
              let token = a["token"] as? UInt32,token != 0,
              (h["anchor_request_token"] == nil || token == h["anchor_request_token"] as? UInt32), a["silicon"] as? String == packet.silicon, a["boot"] as? String == packet.boot,
              a["usb_epoch"] as? UInt32 == packet.usbEpoch, a["capture_epoch"] as? UInt32 == packet.captureEpoch,
              let f = a["frame"] as? Int, f >= 0, f < 2048, let t = value(a,"tick"), let current = value(a,"current_tick"),
              current >= t, current-t < 16000, t <= packet.counter,
              let before = value(h,"anchor_out_absolute_before"), let after = value(h,"anchor_out_absolute_after"), after >= before else {anchorRejected += 1;return}
        let back = (frame-f+2048)%2048
        guard back <= 1000, abs(Double(packet.counter-t)-Double(back)*16000) < 4000 else {anchorRejected += 1;return}
        let anchorOrdinal = ordinal-Int64(back)
        let base = Double(reference)*Double(clockScale.numer)/Double(clockScale.denom)+(Double(8*anchorOrdinal)-Double(microframe))*125000
        let age = Double(current-t)*62.5, scale = Double(clockScale.numer)/Double(clockScale.denom)
        // Explicit 1000ppm counter envelope over <1ms; API/PHY absolute bias is
        // still uncalibrated. These are causal identity candidates, not UTC accuracy.
        let lo = Double(before)*scale-base-age-1000, hi = Double(after)*scale-base-age+1000
        guard lo <= hi, hi-lo < 100_000_000 else {anchorRejected += 1;return}
        lastAnchorSequence = sequence;lowerIdentity = max(lowerIdentity,lo);upperIdentity = min(upperIdentity,hi);anchors += 1
    }
    func counterToMachAbsoluteNS(_ tick: UInt64) -> Double? {
        guard identity == "FRAME_TO_MACH_CAUSALLY_IDENTIFIED",counterWindowFit.n >= 64,
              let previous = lastCounter,tick >= previous,tick-previous <= 8_000_000,let offset = frameOffset else {return nil}
        let base = Double(firstReference)*Double(clockScale.numer)/Double(clockScale.denom)+(Double(offset)-Double(firstMicroframe))*125000
        let counterNS = Double(tick-firstCounter)*62.5
        let elapsed = (counterNS-counterWindowFit.meanY+counterWindowFit.slope*counterWindowFit.meanX)/(1+counterWindowFit.slope)
        return base+elapsed
    }
    /// Invert the frozen hardware fit at a caller's actual Mach instant.
    /// Subtract the integer Mach origin before conversion; never fit arrivals
    /// or convert a complete source/UTC epoch to floating point.
    func counter(atMachAbsolute absolute: UInt64) -> (whole: UInt64, fraction: Double)? {
        guard identity == "FRAME_TO_MACH_CAUSALLY_IDENTIFIED", counterWindowFit.n >= 64,
              let offset = frameOffset, absolute >= firstReference, let last = lastCounter else {return nil}
        let elapsed = Double(absolute-firstReference)*Double(clockScale.numer)/Double(clockScale.denom)
            - Double(offset-Int64(firstMicroframe))*125000
        guard elapsed >= lastElapsed, elapsed-lastElapsed <= 500_000_000 else {return nil}
        let nominal = elapsed+counterWindowFit.meanY+counterWindowFit.slope*(elapsed-counterWindowFit.meanX)
        let delta = nominal/62.5
        guard delta.isFinite, delta >= 0, delta < Double(UInt64.max-firstCounter) else {return nil}
        let whole = firstCounter+UInt64(delta.rounded(.down))
        guard whole >= last, whole-last <= 8_000_000 else {return nil}
        return (whole,delta-delta.rounded(.down))
    }
    var json: [String:Any] { ["source_clock_phase":source.json,"counter_mapping_window":counterWindowFit.json,"causal_counter_innovations":counterInnovations.json,"last_causal_counter_innovation_ns":lastCounterInnovation as Any? ?? NSNull(),"counter_to_Mach_slope_ns_per_tick":62.5/(1+counterWindowFit.slope),"latest_counter_mapped_Mach_absolute_ns":lastCounter.flatMap {counterToMachAbsoluteNS($0)}.map {String(format:"%.3f",$0)} as Any? ?? NSNull(),"counter_phase":counter.json,"reported_UTC_phase":utc.json,"UTC_offset_phase":offset.json,
        "scope":scope,"source_scope":sourceScope,"segment":segments,"accepted":accepted,"rejected":rejected,"last_phase_ns":lastPhase as Any? ?? NSNull(),"last_counter_phase_ns":lastCounterPhase as Any? ?? NSNull(),"last_UTC_phase_ns":lastUTCPhase as Any? ?? NSNull(),"last_UTC_offset_phase_ns":lastOffsetPhase as Any? ?? NSNull(),"elapsed_s":lastElapsed/1e9,"SOF_capture_age_ns":lastAge,
        "identity":identity,"direct_full_speed_XHCI":directFullSpeedXHCI,"host_frame_layout_checks":layoutChecks,"host_frame_layout_failures":layoutFailures,"host_microframe_offset":frameOffset.map {String($0)} as Any? ?? NSNull(),"identity_model":"direct full-speed XHCI; host frame omits3 microframe bits; causal OUT-marker brackets identify origin independently","anchor_n":anchors,"anchor_rejected":anchorRejected,"identity_low_microframes":lowerIdentity.isFinite ? lowerIdentity/125000 as Any : NSNull(),"identity_high_microframes":upperIdentity.isFinite ? upperIdentity/125000 as Any : NSNull(),
        "phase_reference":"hardware SOF ordinal and Apple microframe reference; no request arrival or midpoint phase samples",
        "absolute_timestamp_bias":"UNKNOWN","absolute_UTC_qualified":false,"served_precision_UTC":false] }
}

func usbFrameBridgeTests() -> Int {
    var checks = 0
    func check(_ ok: Bool,_ label: String) {if !ok {fputs("USB boundary test failed: \(label)\n",stderr);exit(1)};checks += 1}
    func decoded(_ d: [String:Any]) -> [String:Any] {try! JSONSerialization.jsonObject(with:JSONSerialization.data(withJSONObject:d)) as! [String:Any]}
    let firstTicks: UInt64 = 4_294_000_000, abs0: UInt64 = 120_000_000, mf0: UInt64 = 100000
    func absolute(_ ns: Int64) -> UInt64 {UInt64(ns)*UInt64(clockScale.denom)/UInt64(clockScale.numer)}
    func sample(_ i: Int,_ requestDelay: Int = 0) -> [String:Any] {
        let counter = firstTicks+UInt64(i)*512000, off: Int64 = 1_790_000_000_000_000_000+(i%2==0 ? 1_000_000 : -1_000_000)
        let mono: Int64 = 1_000_000_000+Int64(i)*32_000_064+(i%2==0 ? 100 : -100)
        let packet: [String:Any] = ["silicon":tc04Silicon,"version":1,"timescale":"POSIX_UTC_ESTIMATE","precision_qualified":false,"kind":1,"boot":"123456789abcdef0","usb_epoch":1,"capture_epoch":1,"sequence":i+1,"tick":String(counter),"current_tick":String(counter+3000),"valid":true,"until_tick":String(counter+8_000_000),"active_lo_tick":String(counter-16000),"active_hi_tick":String(counter+8_000_000),"utc_ns":String(mono+off),"utc_offset_ns":String(off),"root":"0000000000000001","root_boot":"1111111111111111","source_epoch":1,"map_generation":i+1,"bound_ns":UInt32.max,"rate_bound_ppb":UInt32.max,"capture_bound_ns":UInt32.max,"reason":0,"map_ppb":100,"path_scope":"29f6bcc80dccc04d425072fb6ac3a7dd","frame":(2047+i*32)%2048]
        return decoded(["precision_source":packet,"reference_valid":true,"absolute_continuous_intervals_overlap":true,"reference_absolute":String(abs0+absolute(Int64(i)*32_000_000)),"reference_microframe":String(mf0+UInt64(i)*256),"precision_source_absolute_after":String(requestDelay)])
    }
    let a = USBFrameBridge(), b = USBFrameBridge()
    for i in 0..<200 {a.ingest(sample(i));b.ingest(sample(i,i*9000000))}
    check(a.accepted==200 && a.rejected==0 && a.segments==1,"frame and64-bit counter wraps remain continuous")
    check(abs(a.source.slope*1e6-2)<0.001,"source clock drift retained independently")
    check(a.source.detrendedSigma<101 && a.source.detrendedSigma>99,"source mono jitter excludes exported UTC offset changes")
    check(a.utc.detrendedSigma>900000,"noisy UTC translation remains visible")
    check(a.source.json["detrended_phase_stddev_ns"] as? Double == b.source.json["detrended_phase_stddev_ns"] as? Double,"arbitrary request scheduling cannot train phase or lower jitter")
    var slip=sample(200);var p=slip["precision_source"] as! [String:Any];p["frame"]=(p["frame"] as! Int)+1;slip["precision_source"]=p;a.ingest(decoded(slip))
    check(a.rejected==1 && a.source.n==200,"a mismatched frame is rejected before fitting; the last good boundary remains the anchor")
    let c=USBFrameBridge();var h=sample(0),q=h["precision_source"] as! [String:Any]
    let marker:[String:Any]=["sequence":1,"token":1,"valid":true,"silicon":tc04Silicon,"boot":q["boot"]!,"usb_epoch":1,"capture_epoch":1,"frame":2046,"tick":String(firstTicks-16000),"current_tick":String(firstTicks-13000)]
    h["frame_anchor"]=marker;h["anchor_request_token"]=1
    let baseNS=Int64(Double(abs0)*Double(clockScale.numer)/Double(clockScale.denom))-1_000_000+187500
    h["anchor_out_absolute_before"]=String(absolute(baseNS-15000));h["anchor_out_absolute_after"]=String(absolute(baseNS+15000));c.ingest(decoded(h))
    check(c.anchors==1 && c.frameOffset==Int64(mf0),"narrow causal bracket identifies one microframe candidate without a fit")
    check(c.json["served_precision_UTC"] as? Bool == false,"identity candidate never fabricates qualified UTC")
    q["usb_epoch"]=2;h["precision_source"]=q;h.removeValue(forKey:"frame_anchor");c.ingest(decoded(h))
    check(c.segments==2 && c.anchors==0,"USB epoch revokes the old identity")
    let model=USBFrameBridge()
    model.configure(["topology":[[:],["speed":1,"registry_class":"IOUSBHostDevice"],["registry_class":"AppleUSB20XHCIARMPort"],["registry_class":"AppleT8122USBXHCI"]]])
    for i in 0..<200 {
        var row=sample(i);let packet=row["precision_source"] as! [String:Any]
        let ct=firstTicks+UInt64(i)*512000
        row["frame_anchor"]=decoded(["sequence":i+1,"token":i+1,"valid":true,"silicon":tc04Silicon,"boot":packet["boot"]!,"usb_epoch":1,"capture_epoch":1,"frame":((packet["frame"] as! Int)+2047)%2048,"tick":String(ct-16000),"current_tick":String(ct-13000)])
        row["anchor_request_token"]=i+1
        let markerTime=baseNS+Int64(i)*32_000_000
        row["anchor_out_absolute_before"]=String(absolute(markerTime-15000));row["anchor_out_absolute_after"]=String(absolute(markerTime+15000))
        row["current_microframe"]=String(mf0+UInt64(i)*256);row["current_microframe_absolute"]=String(abs0+absolute(Int64(i)*32_000_000))
        row["bus_frame_number"]=String((mf0+UInt64(i)*256)/8);row["bus_frame_absolute"]=String(abs0+absolute(Int64(i)*32_000_000+10000))
        model.ingest(decoded(row))
    }
    check(model.identity=="FRAME_TO_MACH_CAUSALLY_IDENTIFIED","direct full-speed topology, host frame layout and independent markers resolve numbering")
    let last=firstTicks+UInt64(199)*512000
    let expected=Double(abs0)*Double(clockScale.numer)/Double(clockScale.denom)+199*32_000_000.0
    check(abs((model.counterToMachAbsoluteNS(last) ?? 0)-expected)<1,"bounded counter-to-Mach mapping uses boundary evidence")
    check(abs((model.counterToMachAbsoluteNS(last+16000) ?? 0)-expected-1_000_000)<1,"forward counter projection")
    check(model.counterToMachAbsoluteNS(last+8_000_001)==nil,"mapping holdover bound is finite")
    check(model.counterInnovations.n==136 && model.counterInnovations.rawSigma<1,"held-out causal innovations are separate from fitted residuals")
    model.configure(["topology":[[:],["speed":1,"registry_class":"IOUSBHostDevice"],["registry_class":"IOUSBHostDevice"],["registry_class":"Hub"]]])
    check(model.counterToMachAbsoluteNS(last)==nil,"a hub topology cannot silently inherit direct-port qualification")
    return checks
}
