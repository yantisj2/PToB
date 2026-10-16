import Foundation

/// Screen-only values. They never enter UTC delivery, statistics or chrony.
struct HeldMainReadout {
    let utc: SourceInstant, rmsNS: Double?, rmsTitle: String, rmsCaption: String?
}

struct MainReadoutHold {
    private var last: HeldMainReadout?, scope: String?, lastFreshAt: UInt64 = 0
    private var missingSince: UInt64?
    mutating func apply(to model: inout InstrumentSnapshot, scope next: String, now: UInt64, unavailableSince: UInt64?) {
        model.heldReadout = nil
        if scope != next {last = nil;missingSince = nil;scope = next}
        if let utc = model.utc {
            last = HeldMainReadout(utc:utc,rmsNS:model.rmsNS,rmsTitle:model.rmsTitle,rmsCaption:model.rmsCaption)
            lastFreshAt = now;missingSince = nil;return
        }
        guard let last = last, now >= lastFreshAt else {return}
        if missingSince == nil {missingSince = max(lastFreshAt,unavailableSince ?? lastFreshAt)}
        guard let since = missingSince, now >= since, now-since < DisplayHealth.holdNS else {return}
        model.heldReadout = last
    }
}

/// Presentation only: source permission and served UTC never use these timers.
struct DisplayHealth {
    static let holdNS: UInt64 = 500_000_000
    private(set) var unavailableSince: UInt64?, reason: String?, heldUntil: UInt64 = 0
    private var outageReason: String?, pendingReason: String?
    private(set) var losses = 0
    mutating func noteLoss(_ text: String, at now: UInt64) {
        losses += 1
        if reason == nil || now >= heldUntil {
            reason = text;heldUntil = now+Self.holdNS;pendingReason = nil
        } else if text == reason {heldUntil = now+Self.holdNS}
        else {pendingReason = text}
    }
    mutating func update(available: Bool, reason text: String, at now: UInt64) {
        if !available {
            if unavailableSince == nil {unavailableSince = now;noteLoss(text,at:now)}
            else if outageReason != text {noteLoss(text,at:now)}
            outageReason = text
        } else {unavailableSince = nil;outageReason = nil}
        if now >= heldUntil {
            if let next = pendingReason {reason = next;pendingReason = nil;heldUntil = now+Self.holdNS}
            else if available {reason = nil}
        }
    }
    func warning(_ now: UInt64) -> Bool {unavailableSince.map {now >= $0 && now-$0 >= Self.holdNS} ?? false}
    func indicator(_ now: UInt64) -> String? {
        guard let text = reason else {return nil}
        let label = unavailableSince == nil ? "LOSS OBSERVED" : warning(now) ? "OUTAGE" : "LOSS"
        return "\(label) / \(text) / events \(losses)"
    }
    func json(_ now: UInt64) -> [String:Any] {
        ["warning":warning(now),"outage_age_ns":unavailableSince.map {String(now >= $0 ? now-$0 : 0)} as Any? ?? NSNull(),
         "indicator":indicator(now) as Any? ?? NSNull(),"events":losses,"hold_ns":String(Self.holdNS),"presentation_only":true]
    }
}

func displayHealthTests() -> Int {
    var n = 0
    func check(_ ok: Bool,_ label: String) {precondition(ok,label);n += 1}
    var h = DisplayHealth()
    h.update(available:true,reason:"unused",at:0)
    check(!h.warning(0) && h.indicator(0) == nil,"healthy start has no loss indicator")
    h.update(available:false,reason:"source permission expired",at:1_000_000_000)
    check(!h.warning(1_499_999_999) && h.indicator(1_000_000_000)!.contains("permission expired"),"immediate indicator; no amber before 500ms")
    check(h.warning(1_500_000_000),"continuous 500ms outage becomes amber")
    h.update(available:true,reason:"unused",at:1_100_000_000)
    check(!h.warning(1_100_000_000) && h.indicator(1_499_999_999) != nil,"brief loss recovers immediately while reason remains readable")
    h.update(available:true,reason:"unused",at:1_500_000_000)
    check(h.indicator(1_500_000_000) == nil,"recovered reason clears after minimum hold")
    h.noteLoss("USB request failed",at:2_000_000_000)
    h.noteLoss("source unavailable",at:2_100_000_000)
    h.update(available:true,reason:"unused",at:2_200_000_000)
    check(h.indicator(2_200_000_000)!.contains("USB request failed"),"new reason cannot flash over one held for less than 500ms")
    h.update(available:true,reason:"unused",at:2_500_000_000)
    check(h.indicator(2_500_000_000)!.contains("source unavailable"),"pending reason is retained for inspection")
    h.update(available:true,reason:"unused",at:2_999_999_999)
    check(h.indicator(2_999_999_999) != nil,"pending reason also holds a complete 500ms")
    h.update(available:true,reason:"unused",at:3_000_000_000)
    check(h.indicator(3_000_000_000) == nil,"reason expires after recovery and hold")
    h.update(available:false,reason:"gap",at:4_000_000_000)
    h.update(available:true,reason:"unused",at:4_400_000_000)
    h.update(available:false,reason:"gap",at:4_450_000_000)
    check(!h.warning(4_900_000_000),"separate short gaps cannot accumulate into a sustained outage")
    var s = InstrumentSnapshot();s.utc = nil;s.outageWarning = false;s.lossIndicator = h.indicator(4_450_000_000)
    let before = InstrumentFace.render(s,width:80,height:24)
    check(before.contains("LOSS / gap") && !before.contains(InstrumentFace.warning+InstrumentFace.digits("--:--:--.---")[0]),"large clock stays green during debounce and exposes the loss")
    s.outageWarning = true
    let after = InstrumentFace.render(s,width:80,height:24)
    check(after.contains(InstrumentFace.warning+InstrumentFace.digits("--:--:--.---")[0]),"large clock is amber for a qualified outage")
    check(s.utc == nil,"display timers never fabricate a clock value")
    var hold = MainReadoutHold(), model = InstrumentSnapshot()
    model.utc = SourceInstant(ns:1_790_000_000_123_000_000);model.rmsNS = 79;model.rmsTitle = "CHRONY EST RMS"
    hold.apply(to:&model,scope:"source A",now:1_000_000_000,unavailableSince:nil)
    let actualUTC = model.utc
    model.utc = nil;model.rmsNS = nil;model.outageWarning = false;model.lossIndicator = "LOSS / packet gap"
    hold.apply(to:&model,scope:"source A",now:1_100_000_000,unavailableSince:1_050_000_000)
    check(model.utc == nil && model.heldReadout?.utc == actualUTC && model.heldReadout?.rmsNS == 79,"short glitch holds exact displayed values without granting UTC")
    for (width,height) in [(80,24),(120,32),(60,24)] {
        let face = InstrumentFace.render(model,width:width,height:height,ansi:false)
        check(face.contains("HELD <500ms") && face.contains("LOSS / packet gap") && !face.contains("UTC withheld"),"held readings and immediate loss remain visible at \(width) columns")
        check(face.split(separator:"\n",omittingEmptySubsequences:false).allSatisfy {$0.count < width},"held labels fit \(width) columns")
    }
    hold.apply(to:&model,scope:"source A",now:1_549_999_999,unavailableSince:1_050_000_000)
    check(model.heldReadout != nil,"reading survives a glitch strictly below 500ms")
    hold.apply(to:&model,scope:"source A",now:1_550_000_000,unavailableSince:1_050_000_000)
    check(model.heldReadout == nil && model.utc == nil,"500ms outage removes display hold without extending source permission")
    model.utc = actualUTC;model.rmsNS = 81
    hold.apply(to:&model,scope:"source A",now:1_600_000_000,unavailableSince:nil)
    check(model.heldReadout == nil && model.rmsNS == 81,"recovered readings replace held values immediately")
    model.utc = nil;model.rmsNS = nil
    hold.apply(to:&model,scope:"source A",now:1_700_000_000,unavailableSince:1_650_000_000)
    check(model.heldReadout?.rmsNS == 81,"separate brief glitches use the latest recovered value")
    hold.apply(to:&model,scope:"source B",now:1_710_000_000,unavailableSince:1_650_000_000)
    check(model.heldReadout == nil,"a different source cannot display the previous source's cached clock")
    var startup = MainReadoutHold()
    startup.apply(to:&model,scope:"source B",now:1_720_000_000,unavailableSince:1_650_000_000)
    check(model.heldReadout == nil,"startup without a valid reading never invents a hold")
    check(InstrumentFace.rmsDisplay(79).text == "79" && InstrumentFace.rmsDisplay(79).unit == "ns","submicrosecond large RMS uses whole nanoseconds")
    check(InstrumentFace.rmsDisplay(999.6).text == "1000" && InstrumentFace.rmsDisplay(999.6).unit == "ns","nanoseconds round honestly at the unit boundary")
    check(InstrumentFace.rmsDisplay(1000).text == "1.00" && InstrumentFace.rmsDisplay(1000).unit == "µs","one microsecond switches units without scaling error")
    check(InstrumentFace.rmsMeasurement(79.4) == "79 ns" && InstrumentFace.rmsMeasurement(nil) == "---","small RMS text uses integer nanoseconds and preserves missing data")
    return n
}
