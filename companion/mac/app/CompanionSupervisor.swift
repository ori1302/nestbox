import Foundation

/// Runs the companion helper (`nestbox --print --machine ...`), restarts it when it dies, and tracks what it
/// reports.
@MainActor
final class CompanionSupervisor {
    private static let connectedWindow: TimeInterval = 5
    private static let stableRun: TimeInterval = 10      // a run this long resets the backoff
    private static let maxBackoff: Double = 30

    let helper: URL
    private let prefs: Preferences
    private let deviceOverride: URL?
    private let nameOverride: String?
    private let environment: [String: String]?
    private let log: EventLog

    var onChange: (() -> Void)?
    var onChildrenChanged: (() -> Void)?

    private(set) var process: ChildProcess?
    private(set) var petURL: URL?
    private(set) var note = "starting"
    /// The pet answered that activity needs its key: the user pairs with the pet's button.
    private(set) var needsKey = false
    private var lastStatus: Int?
    private var lastSentAt: Date?
    private var generation = 0    // a scheduled restart runs only if no start or stop came after it
    private var failures = 0

    init(helper: URL, prefs: Preferences, deviceOverride: URL?, nameOverride: String?,
         environment: [String: String]?, log: EventLog) {
        self.helper = helper
        self.prefs = prefs
        self.deviceOverride = deviceOverride
        self.nameOverride = nameOverride
        self.environment = environment
        self.log = log
    }

    var isRunning: Bool { process?.isRunning ?? false }

    /// The Mac woke or changed network: the helper drops its connection and finds the pet again at once.
    func reachAgain() {
        guard let pid = process?.pid else { return }
        log.append("-- the Mac woke or changed network: reaching the pet again")
        kill(pid, SIGUSR1)
    }

    /// The pet accepted a report within the last few seconds.
    var isConnected: Bool {
        guard let at = lastSentAt, lastStatus == HTTPStatus.ok else { return false }
        return Date().timeIntervalSince(at) < Self.connectedWindow
    }

    var arguments: [String] {
        var args = ["--print", "--machine"]
        if let device = deviceOverride {
            args += ["--device", device.absoluteString]
        } else if let name = nameOverride {
            args += ["--name", name]
        } else if let id = prefs.pairedID {
            args += ["--id", id]
        }
        for signal in ActivitySignal.allCases where !prefs.isEnabled(signal) { args.append(signal.disableFlag) }
        return args
    }

    func start() {
        generation += 1
        guard FileManager.default.isExecutableFile(atPath: helper.path) else {
            note = "not found: \(helper.path)"
            onChange?()
            return
        }
        let child = ChildProcess(executable: helper, arguments: arguments)
        child.environment = environment
        child.onLine = { [weak self] line in self?.handle(line) }
        child.onExit = { [weak self, weak child] status, requested, ran in
            guard let self, let child, self.process === child, !requested else { return }
            self.scheduleRestart(afterExit: status, ran: ran)
        }
        do {
            try child.start()
            process = child
            note = "looking for a pet"
            lastStatus = nil
            log.append("-- companion started: nestbox " + child.arguments.joined(separator: " "))
        } catch {
            note = "could not start: \(error.localizedDescription)"
        }
        onChildrenChanged?()
        onChange?()
    }

    func restart() {
        process?.stop()
        process = nil
        start()
    }

    /// Stops the helper and cancels any scheduled restart.
    func stop() {
        generation += 1
        process?.stop()
        process = nil
    }

    /// For quitting: returns once the helper has exited.
    func stopAndWait() {
        generation += 1
        process?.stopAndWait()
        process = nil
    }

    func forgetPet() { petURL = nil }

    // After 1 s when it had run a while; otherwise backs off exponentially while it keeps failing.
    private func scheduleRestart(afterExit status: Int32, ran: TimeInterval) {
        failures = ran > Self.stableRun ? 0 : failures + 1
        let delay = failures == 0 ? 1.0 : min(Self.maxBackoff, pow(2, Double(failures)))
        note = "stopped (exit \(status)), restarting in \(Int(delay)) s"
        log.append("-- companion stopped (exit \(status)); restarting in \(Int(delay)) s")
        onChange?()
        let scheduled = generation
        Task { [weak self] in
            try? await Task.sleep(for: .seconds(delay))
            guard let self, self.generation == scheduled else { return }
            self.start()
        }
    }

    // Tagged lines drive the state; the rest is the log "What It Sends" shows.
    private func handle(_ line: String) {
        guard let event = MachineLine.decode(HelperEvent.self, from: line) else {
            log.append(line)
            return
        }
        switch event {
        case .sent(let status):
            lastStatus = status
            lastSentAt = Date()
            if status == HTTPStatus.ok { needsKey = false }
            note = status == HTTPStatus.ok ? "connected" : needsKey ? "needs the pet's key" : "the pet does not answer"
        case .needsKey:
            needsKey = true
            note = "needs the pet's key"
        case .pairWaiting, .pairDone, .pairFailed:
            break
        case .found(let url, _):
            petURL = url
        case .paired(let id):
            prefs.pairedID = id
        case .connected(let url):
            petURL = url
            note = "connected"
        case .lost:
            note = "lost the pet, looking again"
        }
    }
}
