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
    return n
}
