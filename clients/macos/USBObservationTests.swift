import Foundation

func usbObservationTests() -> Int {
    var n = 0, events: [[String:Any]] = []
    func check(_ ok: Bool,_ label: String) {precondition(ok,label);n += 1}
    let observer = USBObservation(owner:DispatchQueue(label:"USB.transport.test"),record:{events.append($0)})
    let failed: [String:Any] = ["precision_source":NSNull(),"precision_source_error_code":-536870186,
        "precision_source_error_domain":"IOUSBHostErrorDomain","precision_source_error":"Control request timed out"]
    observer.observeSourceTransport(failed,at:1_000_000_000)
    check(observer.error?.contains("USB control timeout") == true,"control timeout is visible, separate from source unavailability")
    check(observer.transportErrorCode == -536870186 && observer.sourceRequestFailures == 1,"numeric error and request failures survive diagnostics")
    observer.observeSourceTransport(failed,at:2_000_000_000)
    check(events.count == 1 && observer.sourceRequestFailures == 2,"repeated transport failure counts without repeated transition events")
    observer.observeSourceTransport(["device_source":["valid":false,"reason":1]],at:3_000_000_000)
    check(observer.error == nil && observer.transportFailure == nil,"decoded source-unavailable response proves transport recovery only")
    check(events.last?["type"] as? String == "usb_transport_recovered" && events.last?["outage_ns"] as? String == "2000000000","recovery retains the full transport outage")
    check(observer.sourceResponseCount == 1,"source-unavailable response is distinguished from no USB response")
    observer.observeSourceTransport(["precision_source":NSNull(),"precision_source_error":"Error Code=-536870186"],at:4_000_000_000)
    check(observer.error?.contains("USB control timeout") == true,"older helper error strings remain diagnosable")
    observer.observeSourceTransport(["reference_valid":true],at:5_000_000_000)
    check(observer.transportFailure != nil,"host frame references cannot falsely clear a device request outage")
    let feeder = ChronyRefclock(socket:"/tmp/unopened.sock",commandSocket:"/tmp/unopened-command.sock",binary:"/usr/bin/false",owner:DispatchQueue(label:"USB.fine-label.test"),fineInput:true,record:{_ in})
    check(feeder.inputMethod == "EXPERIMENTAL_FINE_UTC_ESTIMATE" && !feeder.ready && feeder.sent == 0,"fine mode is labelled correctly before any accepted feed")
    let silicon="1000000000000004", serial="TC-04-1000000000000004"
    func selected()->USBObservation {
        let value=USBObservation(owner:DispatchQueue(label:"USB.selection.test"),record:{_ in})
        value.ingest(["event":"manifest","serial":serial,"silicon":silicon],at:1_000_000_000)
        value.startedAt=1_000_000_000;value.running=true
        return value
    }
    var estimate:[String:Any]=["silicon":silicon,"counter_valid":true,"estimate_present":false,"estimate_utc_ns":"0","root":"0000000000000000"]
    let cold=selected();cold.ingest(["event":"sample","local_estimate":estimate],at:16_000_000_000)
    cold.rotateIfUnavailable(16_000_000_000)
    check(cold.lastSourceAt==0 && cold.rotating,"counter-only cold peer cannot pin selection")
    estimate["estimate_present"]=true;estimate["estimate_utc_ns"]="1700000000000000000";estimate["root"]="0000000000000011"
    estimate["session_current"]=false;estimate["root_envelope_current"]=false
    let held=selected();held.ingest(["event":"sample","local_estimate":estimate],at:16_000_000_000)
    held.rotateIfUnavailable(16_000_000_000)
    check(held.lastSourceAt==16_000_000_000 && !held.rotating,"acquired held clock retains selection without renewing authority")
    estimate["estimate_present"]=false;estimate["capture_epoch"]=2
    held.ingest(["event":"sample","local_estimate":estimate],at:32_000_000_000)
    held.rotateIfUnavailable(32_000_000_000)
    check(held.rotating,"new epoch without an estimate resumes ordinary source rotation")
    let recovering=selected();recovering.pinnedSerial=serial
    recovering.observeSourceTransport(failed,at:2_000_000_000)
    recovering.rotateIfUnavailable(3_999_999_999)
    check(!recovering.reconnecting,"one brief request failure does not reconnect")
    recovering.rotateIfUnavailable(4_000_000_000)
    check(recovering.reconnecting && recovering.reconnectSerial==serial && recovering.transportReconnects==1,"pinned transport failure requests same-serial rediscovery")
    recovering.rotateIfUnavailable(5_000_000_000)
    check(recovering.transportReconnects==1 && !recovering.rotating,"reconnect is idempotent and does not rotate to another peer")
    check(recovering.helperSerial(nil,at:33_999_999_999)==serial && recovering.helperSerial(nil,at:34_000_000_000)==nil,"same-device recovery window expires causally without a permanent automatic pin")
    check(recovering.helperSerial("TC-09-0000000000000009",at:5_000_000_000)=="TC-09-0000000000000009","explicit user pin overrides temporary reconnect target")
    let stale=selected()
    stale.ingest(["event":"transport_end","reason":"STALE_INTERFACE","serial":serial],at:3_000_000_000)
    check(stale.reconnecting && stale.transportReconnects==1,"positive stale helper termination immediately schedules reconnect")
    let wrong=selected()
    wrong.ingest(["event":"transport_end","reason":"STALE_INTERFACE","serial":"TC-99-0000000000000099"],at:3_000_000_000)
    check(!wrong.reconnecting,"foreign terminal event cannot reconnect this selected interface")
    let brief=selected();brief.observeSourceTransport(failed,at:2_000_000_000)
    brief.observeSourceTransport(["precision_source":["valid":false]],at:3_000_000_000)
    brief.rotateIfUnavailable(4_000_000_000)
    check(!brief.reconnecting,"a decoded source-unavailable reply clears transport failure without claiming source authority")
    return n
}
