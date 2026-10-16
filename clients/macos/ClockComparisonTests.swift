import Foundation

func clockComparisonTests() -> Int {
    var count = 0
    func check(_ value: Bool, _ label: String) { precondition(value,label); count += 1 }
    let ntp = NTPCheck.parse("+0.010350 +/- 0.108216 pool.ntp.org 64.131.47.178\n",at:100,server:"pool.ntp.org",exitCode:0)
    check(ntp != nil && ntp!.offset == 0.010350 && ntp!.uncertainty == 0.108216,"keep NTP sign and reported uncertainty")
    check(ntp!.json["supports_100us_check"] as? Bool == false,"broad NTP interval cannot qualify 100us")
    check(NTPCheck.parse("+0.000001 +/- 0.000002 pool.ntp.org x\n",at:100,server:"pool.ntp.org",exitCode:69) == nil,"failed exchange cannot look accurate")
    check(NTPCheck.parse("+0.000001 +/- 0.000002 other x\n",at:100,server:"pool.ntp.org",exitCode:0) == nil,"server mismatch")
    check(!LocalNTPMonitor.validServer("--set") && !LocalNTPMonitor.validServer("bad host") && LocalNTPMonitor.validServer("pool.ntp.org"),"NTP arguments cannot introduce options")
    var run = LocalClockComparison()
    func wall(_ t: UInt64, step: Int64 = 0) -> MacWallSample { MacWallSample(before:t,after:t+100,instant:SourceInstant(ns:1_700_000_000_000_000_000+Int64(t)+step)) }
    let a = wall(1_000_000_000), b = wall(2_000_000_000), c = wall(3_000_000_000), d = wall(4_000_000_000,step:10_000_000)
    run.observe(wall:a,served:a.instant.adding(0.002),scope:"first")
    run.observe(wall:b,served:nil,scope:"")
    run.observe(wall:c,served:nil,scope:"")
    run.observe(wall:d,served:d.instant.adding(-0.004),scope:"second")
    let result = run.json(d.tick)
    check(run.count == 2 && run.missing == 2 && run.scopeChanges == 1,"source changes retain lifetime population")
    check(abs(run.sum+0.002) < 1e-12 && run.maximum == 0.004,"retain signed bias and worst error")
    check(run.wallSteps == 1 && result["longest_observed_unavailable_ns"] as? String == "2000000000","local wall steps and missing intervals remain visible")
    for i in 5...605 { let sample = wall(UInt64(i)*1_000_000_000,step:10_000_000); run.observe(wall:sample,served:sample.instant,scope:"second") }
    check(run.count == 603 && run.maximum == 0.004,"ten-minute maximum survives a 256-sample window")
    let usb = USBObservation(owner:DispatchQueue(label:"test.usb"),record:{_ in})
    usb.ingest(["event":"manifest","serial":"TC-02-0000000000000002","silicon":"0000000000000002"],at:50)
    let raw: [String:Any] = ["silicon":"0000000000000002","boot":"boot","usb_epoch":1,"capture_epoch":2,"valid":true,"tick":"1234","current_tick":"1244","sequence":1]
    usb.ingest(["event":"sample","reference_valid":true,"device_sof":raw],at:100)
    usb.ingest(["event":"sample","reference_valid":false,"device_sof":NSNull()],at:200)
    let diagnostic = usb.snapshot(300)
    check(usb.valid == 1 && usb.requestsFailed == 1 && usb.referencesFailed == 1,"USB failures stay separate from raw capture availability")
    check(diagnostic["served_utc"] as? Bool == false && diagnostic["frame_identity"] as? String == "UNRESOLVED","raw USB evidence never fabricates UTC")
    var foreign = raw;foreign["silicon"] = "0000000000000003"
    usb.ingest(["event":"sample","device_sof":foreign],at:400)
    check(usb.identityRejections == 1 && usb.valid == 1,"foreign identity refused before timing admission")
    usb.ingest(["event":"manifest","serial":"TC-03-0000000000000003","silicon":"0000000000000003"],at:500)
    usb.ingest(["event":"sample","device_sof":raw],at:600)
    check(usb.identityRejections == 3 && usb.valid == 1,"compromised session cannot rebind or readmit later packets")
    return count
}
