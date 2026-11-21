import AppKit
import Network

/// Calls `onChange` shortly after the Mac wakes or its network changes (another Wi-Fi, a cable, a VPN), so the
/// app can reach the pet again at once instead of waiting for a report to fail.
@MainActor
final class NetworkWatch {
    /// A change arrives as several path updates, and Wi-Fi needs a moment to rejoin after a wake.
    private static let settle: TimeInterval = 2

    var onChange: (() -> Void)?

    private let monitor = NWPathMonitor()
    private var seenFirstPath = false
    private var pending: Timer?
    private var wakeObserver: NSObjectProtocol?

    func start() {
        wakeObserver = NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didWakeNotification, object: nil, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated { self?.schedule() }
        }
        monitor.pathUpdateHandler = { [weak self] _ in
            DispatchQueue.main.async {
                MainActor.assumeIsolated { self?.pathChanged() }
            }
        }
        monitor.start(queue: .global(qos: .utility))
    }

    func stop() {
        monitor.cancel()
        pending?.invalidate()
        if let wakeObserver { NSWorkspace.shared.notificationCenter.removeObserver(wakeObserver) }
        wakeObserver = nil
    }

    // The monitor reports the current path once when it starts; that is not a change.
    private func pathChanged() {
        if seenFirstPath { schedule() }
        seenFirstPath = true
    }

    private func schedule() {
        pending?.invalidate()
        let timer = Timer(timeInterval: Self.settle, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated { self?.onChange?() }
        }
        RunLoop.main.add(timer, forMode: .common)
        pending = timer
    }
}
