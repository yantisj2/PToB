import Foundation

/// Capacity includes work already executing. Slow observers cannot enqueue an
/// unlimited chain of closures, and never hold the receiver's ingress lock.
final class ObserverQueue {
    private let queue: DispatchQueue
    private let lock = NSLock()
    private let capacity: Int
    private var pending = 0
    private var peak = 0
    private var rejected = 0
    private var wallNS: UInt64 = 0
    private var failure: String?
    init(_ label: String, capacity: Int) {
        precondition(capacity > 0)
        self.capacity = capacity
        queue = DispatchQueue(label:label,qos:.utility)
    }
    @discardableResult func submit(units: Int = 1, _ work: @escaping () throws -> Void) -> Bool {
        lock.lock()
        guard units > 0, units <= capacity-pending, failure == nil else {
            rejected += max(0,units); lock.unlock(); return false
        }
        pending += units; peak = max(peak,pending)
        // Serialize enqueue with admission so concurrent producers retain FIFO.
        queue.async {
            self.lock.lock(); let failed = self.failure != nil; self.lock.unlock()
            let begin = tick()
            if !failed {
                do { try work() }
                catch { self.lock.lock(); self.failure = String(describing:error); self.lock.unlock() }
            }
            self.lock.lock(); self.wallNS += tick()-begin; self.pending -= units; self.lock.unlock()
        }
        lock.unlock(); return true
    }
    func waitUntilIdle() { queue.sync {} }
    var error: String? { lock.lock(); defer { lock.unlock() }; return failure }
    var diagnostics: [String:Any] {
        lock.lock(); defer { lock.unlock() }
        return ["pending_units":pending,"peak_units":peak,"capacity_units":capacity,
                "rejected_units":rejected,"work_wall_ns":wallNS,"error":failure as Any? ?? NSNull()]
    }
}

