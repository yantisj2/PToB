import Foundation
import Darwin

func managedChronyConfiguration(_ directory: String) -> String {
    // Same isolated SOCK-only policy as chrony/run.py. No public ports,
    // persistent sample logs, system clock controls or invented local source.
    // Apply the DC trial's 250ms update cadence to both USB source profiles;
    // retain filter4/maxsamples64 and every source/expiry admission fence.
    return """
    # Experimental BLE-derived UTC; configured allowances are not measured bounds.
    refclock SOCK \(directory)/BLE.sock refid BLE stratum 0 poll -2 filter 4 precision 0.05 delay 0.2
    minsamples 4
    maxsamples 64
    maxclockerror 10
    maxdistance 1
    maxupdateskew 1000
    port 0
    cmdport 0
    bindcmdaddress \(directory)/command.sock
    pidfile \(directory)/chronyd.pid
    driftfile \(directory)/chrony.drift

    """
}

/// One private child, owned by this foreground client. No service registration,
/// global daemon discovery, privilege escalation, network source or USB access.
final class ManagedChronySession {
    enum State: String {case idle, starting, running, failed, stopped}
    let record: ([String:Any])->Void
    let daemonOverride: String?, clientOverride: String?
    private let reader = DispatchQueue(label:"org.ptob.chrony.daemon-output",qos:.utility)
    private let readerDone = DispatchGroup(), outputLock = NSLock()
    private var output = Data(), outputBytes = 0
    private var task: Process?, exitSignal: DispatchSemaphore?
    private(set) var state = State.idle, directory: String?, daemonPath: String?, clientPath: String?
    private(set) var error: String?, attempts = 0, failures = 0, lastExit: Int32?
    private var readyDeadline: UInt64 = 0, retryAt: UInt64 = 0
    var commandSocket: String? {directory.map {$0+"/command.sock"}}
    var refclockSocket: String? {directory.map {$0+"/BLE.sock"}}
    init(daemon: String? = nil, client: String? = nil, record: @escaping ([String:Any])->Void) {
        daemonOverride = daemon;clientOverride = client;self.record = record
    }
    private func diagnostic() -> (String,Int) {
        outputLock.lock();defer {outputLock.unlock()}
        return (String(decoding:output,as:UTF8.self),max(0,outputBytes-output.count))
    }
    private func fail(_ message: String,_ now: UInt64) {
        state = .failed;error = message;failures += 1
        let seconds = min(30,5*failures)
        retryAt = now+UInt64(seconds)*1_000_000_000
        record(["type":"chrony_service_error","tick_ns":String(now),"error":message,"retry_seconds":seconds,
            "diagnostic":diagnostic().0,"attempt":attempts,"exit_code":lastExit as Any? ?? NSNull()])
    }
    @discardableResult func start(_ now: UInt64) -> Bool {
        guard state != .stopped, state != .running, state != .starting, now >= retryAt else {return false}
        guard cleanup() else {fail("Previous owned chronyd has not exited; retrying cleanup.",now);return false}
        attempts += 1;error = nil
        outputLock.lock();output.removeAll(keepingCapacity:true);outputBytes = 0;outputLock.unlock()
        do {
            let home = try usbHelperExecutableURL().deletingLastPathComponent().deletingLastPathComponent()
            let daemon = daemonOverride ?? home.appendingPathComponent("chrony/bin/chronyd").path
            let client = clientOverride ?? home.appendingPathComponent("chrony/bin/chronyc").path
            daemonPath = daemon;clientPath = client
            guard FileManager.default.isExecutableFile(atPath:daemon),FileManager.default.isExecutableFile(atPath:client) else {
                throw NSError(domain:"Chrony tools missing; run clients/macos/chrony/build.sh.",code:1)
            }
            guard let user = getpwuid(geteuid()) else {throw NSError(domain:"Cannot determine chrony user",code:1)}
            let name = String(cString:user.pointee.pw_name), group = user.pointee.pw_gid
            var template = Array("/private/tmp/ptob-ch-\(geteuid())-XXXXXX".utf8CString)
            guard let made = mkdtemp(&template) else {throw NSError(domain:"Create private chrony runtime",code:Int(errno))}
            let path = String(cString:made);directory = path
            guard chown(path,geteuid(),group) == 0 else {throw NSError(domain:"Set private chrony runtime group",code:Int(errno))}
            _ = try chronySocketAddress(path+"/command.sock")
            try managedChronyConfiguration(path).write(toFile:path+"/chrony.conf",atomically:true,encoding:.utf8)
            let child = Process(), pipe = Pipe(), exited = DispatchSemaphore(value:0)
            child.executableURL = URL(fileURLWithPath:daemon)
            child.arguments = ["-x","-U","-u",name,"-d","-4","-f",path+"/chrony.conf"]
            child.standardInput = FileHandle.nullDevice;child.standardOutput = pipe;child.standardError = pipe
            child.terminationHandler = {_ in exited.signal()}
            try child.run()
            task = child;exitSignal = exited
            readerDone.enter()
            reader.async {
                defer {try? pipe.fileHandleForReading.close();self.readerDone.leave()}
                while true {
                    let data = pipe.fileHandleForReading.availableData;if data.isEmpty {break}
                    self.outputLock.lock()
                    self.outputBytes += data.count;self.output.append(data)
                    if self.output.count > 8192 {self.output = Data(self.output.suffix(8192))}
                    self.outputLock.unlock()
                }
            }
            state = .starting;readyDeadline = now+5_000_000_000
            record(["type":"chrony_service_start","tick_ns":String(now),"pid":child.processIdentifier,
                "runtime":path,"chronyd":daemon,"chronyc":client,"arguments":child.arguments!,
                "attempt":attempts,"configuration":managedChronyConfiguration(path),"system_clock_control":false])
            return true
        } catch {
            _ = cleanup()
            record(["type":"chrony_service_setup_failure","tick_ns":String(now),"error":String(describing:error)])
            fail((error as NSError).domain,now);return false
        }
    }
    func poll(_ now: UInt64) {
        guard state == .starting || state == .running, let task = task else {return}
        if !task.isRunning {
            lastExit = task.terminationStatus;_ = cleanup()
            fail("Owned chronyd exited \(lastExit ?? -1); retrying.",now);return
        }
        if state == .starting, let command = commandSocket, let refclock = refclockSocket,
           FileManager.default.fileExists(atPath:command),FileManager.default.fileExists(atPath:refclock) {
            state = .running
            record(["type":"chrony_service_ready","tick_ns":String(now),"runtime":directory!,"pid":task.processIdentifier])
        } else if state == .starting && now >= readyDeadline {
            _ = cleanup();fail("Chrony did not create its sockets; retrying. Press d for details.",now)
        }
    }
    private func cleanup() -> Bool {
        if let child = task {
            if child.isRunning {
                _ = kill(child.processIdentifier,SIGTERM)
                if exitSignal?.wait(timeout:.now()+0.75) != .success, child.isRunning {
                    kill(child.processIdentifier,SIGKILL)
                    _ = exitSignal?.wait(timeout:.now()+0.75)
                }
            }
            guard !child.isRunning else {return false}
            lastExit = child.terminationStatus
        }
        guard readerDone.wait(timeout:.now()+1) == .success else {return false}
        task = nil;exitSignal = nil
        if let path = directory {
            do {try FileManager.default.removeItem(atPath:path);directory = nil}
            catch {return false}
        }
        return true
    }
    /// reset sources does not clear chrony 4.9's pre-poll refclock filter.
    /// Recreate only our private process and sockets at authority boundaries.
    func recycle(reason: String, now: UInt64) {
        guard state != .stopped else {return}
        let old = directory, closed = cleanup()
        record(["type":"chrony_service_epoch_end","tick_ns":String(now),"reason":reason,
            "runtime":old as Any? ?? NSNull(),"cleanup_complete":closed,"exit_code":lastExit as Any? ?? NSNull()])
        if closed {state = .idle;retryAt = 0;error = nil}
        else {fail("Source changed; previous owned chronyd cleanup incomplete.",now)}
    }
    func stop() {
        state = .stopped
        let closed = cleanup(), diagnostic = diagnostic()
        record(["type":"chrony_service_end","tick_ns":String(tick()),"cleanup_complete":closed,
            "exit_code":lastExit as Any? ?? NSNull(),"diagnostic":diagnostic.0,
            "diagnostic_bytes_discarded":diagnostic.1,"attempts":attempts,"failures":failures])
    }
    var snapshot: [String:Any] {
        ["managed":true,"state":state.rawValue.uppercased(),"runtime":directory as Any? ?? NSNull(),
            "pid":task.map {$0.processIdentifier} as Any? ?? NSNull(),"attempts":attempts,"failures":failures,
            "error":error as Any? ?? NSNull(),"diagnostic":diagnostic().0,"system_clock_control":false]
    }
}
