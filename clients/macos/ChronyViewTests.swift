import Foundation

func chronyViewTests() -> Int {
    var n = 0
    func check(_ ok: Bool,_ label: String) {if !ok {fputs("Chrony view test failed: \(label)\n",stderr);exit(1)};n += 1}
    let tracking: [String:Any] = ["refid":"424C4500","reference_name":"BLE","stratum":1,
        "ntp_minus_system_s":0.025,"last_offset_s":-0.001,"rms_offset_s":0.002,"frequency_ppm_fast":10.0,
        "residual_frequency_ppm":-0.1,"skew_ppm":0.2,"root_delay_s":0.2,"root_dispersion_s":0.05,
        "update_interval_s":1.0,"leap":"Normal"]
    var value: [String:Any] = ["enabled":true,"fresh":true,"tracking":tracking,"last_tracking":tracking,
        "user_utc_ns":"1790000000025000000","query_span_ns":"2000000","query_age_ns":"250000000",
        "reference_age_s":0.5,"checks":42,"failures":0]
    let feeder: [String:Any] = ["enabled":true,"ready":true,"sent":40,"rejected":3,"failures":0,"resets":2,"last_sample_tick_ns":"9500000000"]
    let comparison: [String:Any] = ["rms_ns":26_000_000.0,"mean_ns":25_000_000.0,"jitter_stddev_ns":1_000_000.0,
        "max_abs_ns":28_000_000.0,"valid_status_samples":30,"unavailable_status_samples":12]
    var s = InstrumentSnapshot();s.local = SourceInstant(ns:1_790_000_000_000_000_000);s.usbEnabled = true;s.usbRunning = true
    s.chrony = ChronyDisplaySnapshot.read(value,feeder:feeder,comparison:comparison,now:10_000_000_000)
    s.applyChronyClock()
    check(s.utc?.ns == 1_790_000_000_025_000_000 && s.offsetNS == 25_000_000,"main clock uses separately gated chrony UTC and correct sign")
    check(s.chrony.rootDistanceNS == 150_000_000 && s.chrony.feedAgeS == 0.5,"root distance model and feed age keep their units")
    check(s.rmsNS == 2_000_000 && s.chrony.localRMSNS == 26_000_000 && s.rmsTitle == "CHRONY EST RMS","large readout follows chrony estimator; local comparison stays separate")
    for (width,height) in [(120,32),(80,24),(60,24),(40,16)] {
        for page in [InstrumentPage.main,.chrony,.help] {
            let output = InstrumentFace.render(s,width:width,height:height,help:page == .help,chronyPage:page == .chrony,ansi:false)
            let lines = output.split(separator:"\n",omittingEmptySubsequences:false)
            check(lines.count == height-1 && lines.allSatisfy {$0.count < width},"\(page.rawValue) fits \(width)×\(height)")
            check(output.contains("c Chrony") && output.contains("q Finish"),"chrony discovery and exit visible at \(width)×\(height)")
        }
    }
    let main = InstrumentFace.render(s,width:80,height:24,ansi:false)
    check(main.contains("CHRONY  TRACKING / BLE / S1") && main.contains("NTP−SYS +25.000 ms"),"main essentials show actual source/stratum and signed correction")
    check(main.contains("vs LOCAL RMS  26.000 ms") && main.contains("EST DIST  150.000 ms"),"main estimator readings are labelled")
    let full = InstrumentFace.render(s,width:80,height:24,chronyPage:true,ansi:false)
    check(full.contains("LAST OFFSET  -1.000 ms") && full.contains("RESIDUAL  -0.100 ppm"),"chrony page includes measured offset and residual frequency")
    check(full.contains("SENT 40 / REJECTED 3") && full.contains("ERRORS 0 / RESETS 2"),"feed acceptance/failure/reset counters retained")
    check(full.contains("CHECKS 42 / FAILED 0") && full.contains("QUERY AGE  0.25 s"),"query health is visible")
    check(!full.contains("Enlarge terminal") && full.contains("UTC accuracy unqualified"),"full page fits default terminal with qualification visible")
    var fineFeed = feeder;fineFeed["input_method"] = "EXPERIMENTAL_FINE_UTC_ESTIMATE"
    s.chrony = ChronyDisplaySnapshot.read(value,feeder:fineFeed,comparison:comparison,now:10_000_000_000)
    let finePage = InstrumentFace.render(s,width:80,height:24,chronyPage:true,ansi:false)
    check(finePage.contains("fine estimate") && finePage.contains("FINE ESTIMATE / READY"),"fine feed is explicitly distinguished from coarse USB")
    check(finePage.contains("UTC accuracy unqualified"),"fine estimator mode preserves visible absolute UTC limit")
    check(finePage.split(separator:"\n",omittingEmptySubsequences:false).allSatisfy {$0.count < 80},"fine labels fit existing default terminal")
    value["fresh"] = false;value["tracking"] = NSNull()
    s.chrony = ChronyDisplaySnapshot.read(value,feeder:feeder,comparison:comparison,now:10_000_000_000);s.applyChronyClock()
    check(s.utc == nil && s.chrony.historical,"stale tracking cannot leave a live main clock")
    let stale = InstrumentFace.render(s,width:80,height:24,chronyPage:true,ansi:false)
    check(stale.contains("LAST SUCCESSFUL SNAPSHOT") && stale.contains("USER UTC  WITHHELD"),"historical statistics are explicit")
    check(s.rmsTitle == "CHRONY RMS (last)" && s.rmsNS == 2_000_000,"withheld UTC retains explicitly historical estimator, never live time")
    let staleMain = InstrumentFace.render(s,width:80,height:24,ansi:false)
    check(!staleMain.contains("NTP−SYS +25.000 ms") && staleMain.contains("UTC WITHHELD / c details"),"main hides stale correction")
    value["error"] = "chronyc failed"
    s.chrony = ChronyDisplaySnapshot.read(value,feeder:feeder,comparison:comparison,now:10_000_000_000)
    check(InstrumentFace.render(s,width:80,height:24,chronyPage:true,ansi:false).contains("CHECK FAILED"),"query failure is distinct from acquiring")
    s.chrony = ChronyDisplaySnapshot()
    let off = InstrumentFace.render(s,width:80,height:24,chronyPage:true,ansi:false)
    check(off.contains("NOT CONNECTED") && off.contains("--chrony-command-socket") && off.contains("does not start or change a daemon"),"disconnected page offers setup without fabricated data or daemon mutation")
    s.chrony = ChronyDisplaySnapshot.read(["enabled":true,"fresh":false],feeder:[:],comparison:[:],now:10_000_000_000)
    check(s.chrony.state == "ACQUIRING" && s.chrony.stratum == nil,"startup never invents Stratum1")
    let waiting = ChronyDisplaySnapshot.read(["enabled":true,"fresh":false,"connected":true,"synchronized":false,"leap":"Not synchronised"],feeder:[:],comparison:[:],now:10_000_000_000)
    check(waiting.state == "WAITING FOR SOURCE" && waiting.utc == nil && waiting.stratum == nil,"connected but unsynchronized source is visible without a fake clock")
    var page = InstrumentPage.main
    for (key,expected): (UInt8,InstrumentPage) in [(99,.chrony),(99,.main),(100,.diagnostics),(99,.chrony),(104,.help),(49,.main),(99,.chrony),(27,.main)] {
        page.navigate(key);check(page == expected,"page navigation is exclusive and returns to main")
    }
    return n
}
